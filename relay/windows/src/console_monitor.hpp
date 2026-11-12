#pragma once
#include <chiaki/discovery.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace relay {
// Read-only local status queries. A missed response never means "asleep".
class ConsoleMonitor {
  struct Target { std::string id, host; bool ps5 = true; } target;
  struct Probe {
    std::string address;
    bool ps5;
    std::atomic<int> state{CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN};
  };
  std::mutex mutex;
  std::condition_variable changed;
  bool stopping = false, checked = false;
  unsigned generation = 0;
  int observed = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
  ULONGLONG observedAt = 0;
  std::thread worker;
  static void reply(ChiakiDiscoveryHost *host, void *data) {
    auto &probe = *static_cast<Probe *>(data);
    if (host->host_addr && probe.address == host->host_addr &&
        chiaki_discovery_host_is_ps5(host) == probe.ps5)
      probe.state = host->state;
  }
  int probe(const Target &selected, unsigned revision) {
    addrinfo hints{}, *addresses = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(selected.host.c_str(), nullptr, &hints, &addresses))
      return CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
    sockaddr_in destination = *reinterpret_cast<sockaddr_in *>(addresses->ai_addr);
    freeaddrinfo(addresses);
    destination.sin_port = htons(selected.ps5 ? CHIAKI_DISCOVERY_PORT_PS5 : CHIAKI_DISCOVERY_PORT_PS4);
    char ip[INET_ADDRSTRLEN]{};
    InetNtopA(AF_INET, &destination.sin_addr, ip, sizeof(ip));
    Probe result{ip, selected.ps5};
    ChiakiLog log{};
    chiaki_log_init(&log, 0, nullptr, nullptr);
    ChiakiDiscovery discovery{};
    if (chiaki_discovery_init(&discovery, &log, AF_INET) != CHIAKI_ERR_SUCCESS)
      return CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
    ChiakiDiscoveryThread reader{};
    if (chiaki_discovery_thread_start(&reader, &discovery, reply, &result) != CHIAKI_ERR_SUCCESS) {
      chiaki_discovery_fini(&discovery);
      return CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
    }
    ChiakiDiscoveryPacket packet{};
    packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;
    packet.protocol_version = const_cast<char *>(selected.ps5 ? CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5 : CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4);
    if (chiaki_discovery_send(&discovery, &packet,
          reinterpret_cast<sockaddr *>(&destination), sizeof(destination)) == CHIAKI_ERR_SUCCESS) {
      std::unique_lock<std::mutex> lock(mutex);
      changed.wait_for(lock, std::chrono::milliseconds(800),
                       [&] { return stopping || generation != revision; });
    }
    chiaki_discovery_thread_stop(&reader);
    chiaki_discovery_fini(&discovery);
    return result.state;
  }
  void run() {
    std::unique_lock<std::mutex> lock(mutex);
    while (!stopping) {
      if (target.host.empty()) {
        changed.wait(lock, [&] { return stopping || !target.host.empty(); });
        continue;
      }
      auto selected = target;
      auto revision = generation;
      lock.unlock();
      int result = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
      try { result = probe(selected, revision); } catch (...) {}
      lock.lock();
      if (generation == revision) {
        checked = true;
        if (result == CHIAKI_DISCOVERY_HOST_STATE_STANDBY || result == CHIAKI_DISCOVERY_HOST_STATE_READY) {
          observed = result;
          observedAt = GetTickCount64();
        }
      }
      changed.wait_for(lock, std::chrono::seconds(3),
                       [&] { return stopping || generation != revision; });
    }
  }
public:
  ConsoleMonitor() : worker([this] { run(); }) {}
  ~ConsoleMonitor() {
    { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
    changed.notify_all();
    if (worker.joinable()) worker.join();
  }
  std::string state(const Console &console) {
    std::lock_guard<std::mutex> lock(mutex);
    if (target.id != console.id || target.host != console.local || target.ps5 != console.ps5) {
      target = {console.id, console.local, console.ps5};
      ++generation;
      checked = false;
      observed = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
      observedAt = 0;
      changed.notify_all();
    }
    if (observedAt && GetTickCount64() - observedAt < 15000) {
      if (observed == CHIAKI_DISCOVERY_HOST_STATE_STANDBY) return "Rest mode";
      if (observed == CHIAKI_DISCOVERY_HOST_STATE_READY) return "On";
    }
    return checked || target.host.empty() ? "Status unavailable" : "Checking...";
  }
};
}
