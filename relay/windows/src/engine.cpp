#include "app.hpp"
#include "connection_retry.hpp"
#include "console_monitor.hpp"
#include "controller_wire.h"
#include "ctrl_failure.hpp"
#include "jpeg_encode.h"
#include "stream_profile.h"
#include "wire.h"
#include <bcrypt.h>
#include <chiaki/discovery.h>
#include <chiaki/log.h>
#include <chiaki/regist.h>
#include <mmsystem.h>
#include <mswsock.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <opus.h>
}
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace relay {
using Clock = std::chrono::steady_clock;
static uint64_t micros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             Clock::now().time_since_epoch())
      .count();
}
static uint64_t millis() { return micros() / 1000; }
static void pause_us(uint64_t n) {
  std::this_thread::sleep_for(std::chrono::microseconds(n));
}
struct Adaptive {
  double fps = 20, service = 0, display = 0, age = 0;
  uint64_t resetAt = millis(), feedbackAt = 0;
  uint32_t stamp = 0;
  int healthy = 0;
  bool seen = false;
  void reset() { *this = Adaptive{}; }
  void feedback(const uint8_t *b, size_t n) {
    if (n != 80 || r32(b + 20) != 48 || r16(b + 16) || r16(b + 18) != 1 ||
        r32(b + 32) != 1)
      return;
    uint32_t s = r32(b + 28);
    if (seen && (int32_t)(s - stamp) <= 0)
      return;
    uint32_t v[12];
    for (int i = 0; i < 12; i++)
      v[i] = r32(b + 32 + 4 * i);
    double ms = v[1], decoded = v[2], shown = v[4], received = v[6],
           missing = v[7];
    if (ms < 500 || ms > 5000 || decoded > 600 || shown > 600 ||
        received > 1000 || missing > 10000 || v[3] > 10000000 ||
        v[5] > 10000000 || v[9] != shown || v[11] > 1000)
      return;
    stamp = s;
    feedbackAt = millis();
    seen = true;
    display = shown * 1000 / ms;
    age = shown ? v[8] / shown : 0;
    if (decoded < 3 || shown < 3 || !v[3]) {
      healthy = 0;
      return;
    }
    double sample = v[3] / decoded + v[5] / shown;
    service = service ? service * .7 + sample * .3 : sample;
    double capacity = std::clamp(880000 / service, 5., 30.);
    if (received >= 8 && decoded >= 8 &&
        shown < std::min(received, decoded) * .85)
      capacity = std::max(5., std::min(capacity, display * .9));
    bool pressure =
        (received + missing >= 5 && missing / (received + missing) > .10) ||
        age > 150000;
    double target = pressure ? std::min(capacity, fps * .85) : capacity;
    if (target < fps * .95) {
      fps = std::max(5., target);
      healthy = 0;
    } else if (!pressure && ++healthy >= 3) {
      fps = std::min(capacity, std::min(30., fps + 1));
      healthy = 0;
    } else if (pressure)
      healthy = 0;
  }
  uint64_t interval() {
    if (millis() - (seen ? feedbackAt : resetAt) > 3500) {
      fps = std::min(fps, 15.);
      healthy = 0;
    }
    return (uint64_t)ceil(1000000 / fps);
  }
};
struct PcmQueue {
  std::mutex mutex;
  std::deque<std::array<uint8_t, 480>> slots;
  std::array<uint8_t, 480> partial{};
  size_t used = 0;
  int startup = 2;
  uint64_t next = 0, dropped = 0;
  void clear() {
    std::lock_guard<std::mutex> g(mutex);
    slots.clear();
    used = 0;
    startup = 2;
    next = 0;
  }
  void offer(const std::vector<uint8_t> &b) {
    std::lock_guard<std::mutex> g(mutex);
    for (size_t at = 0; at < b.size();) {
      size_t n = std::min(480 - used, b.size() - at);
      memcpy(partial.data() + used, b.data() + at, n);
      used += n;
      at += n;
      if (used == 480) {
        if (slots.size() == 3) {
          slots.pop_front();
          dropped++;
        }
        slots.push_back(partial);
        used = 0;
      }
    }
  }
  bool poll(std::array<uint8_t, 480> &out) {
    std::lock_guard<std::mutex> g(mutex);
    auto now = micros();
    if (slots.empty() || now < next || (startup == 2 && slots.size() < 2))
      return false;
    out = slots.front();
    slots.pop_front();
    if (startup > 0) {
      startup--;
      next = now + (startup ? 0 : 20000);
    } else
      next = std::max(next + 20000, now + 10000);
    return true;
  }
};
struct Resampler {
  int step, channels, cursor = 0, phase = 0;
  double taps[63]{}, history[63]{};
  Resampler(int rate, int ch) : step(rate / 12000), channels(ch) {
    if (rate < 12000 || rate % 12000 || ch < 1 || ch > 2)
      throw std::runtime_error("Unsupported source audio format");
    double cutoff = .45 / step, sum = 0;
    for (int i = 0; i < 63; i++) {
      int n = i - 31;
      double sinc =
          n ? sin(2 * 3.141592653589793 * cutoff * n) / (3.141592653589793 * n)
            : 2 * cutoff;
      taps[i] = sinc * (.5 - .5 * cos(2 * 3.141592653589793 * i / 62));
      sum += taps[i];
    }
    for (auto &v : taps)
      v /= sum;
  }
  std::vector<uint8_t> convert(const int16_t *b, int frames) {
    std::vector<uint8_t> out;
    out.reserve((phase + frames) / step * 2);
    for (int i = 0; i < frames; i++) {
      int sum = 0;
      for (int ch = 0; ch < channels; ch++)
        sum += b[i * channels + ch];
      history[cursor] = (double)sum / channels;
      cursor = (cursor + 1) % 63;
      if (++phase == step) {
        phase = 0;
        double v = 0;
        int pos = cursor;
        for (int k = 0; k < 63; k++) {
          if (--pos < 0)
            pos = 62;
          v += history[pos] * taps[k];
        }
        int n = std::clamp((int)std::lround(v), -32768, 32767);
        out.push_back((uint8_t)n);
        out.push_back((uint8_t)(n >> 8));
      }
    }
    return out;
  }
};
struct Pair {
  ChiakiRegisteredHost host{};
  std::array<uint8_t, 8> account{};
};
static std::wstring pair_path(const Console &c) {
  if (c.id.size() != 32 ||
      c.id.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw std::runtime_error("Invalid saved console identifier");
  return data_dir() + L"\\console-" + wide(c.id) + L".dat";
}
static bool load_pair(const Console &c, Pair &p) {
  auto b = read_private(pair_path(c));
  std::array<uint8_t, 8> a{};
  if (!account_id(c.account, a))
    return false;
  if (b.size() != 12 + sizeof(ChiakiRegisteredHost) ||
      r32(b.data()) != 0x57525031 || memcmp(b.data() + 4, a.data(), 8))
    return false;
  memcpy(&p.host, b.data() + 12, sizeof(p.host));
  p.account = a;
  return chiaki_target_is_ps5(p.host.target) == c.ps5;
}
bool has_pairing(const Console &console) {
  Pair pair;
  return load_pair(console, pair);
}
static void save_pair(const Console &c, const Pair &p) {
  std::vector<uint8_t> b(12 + sizeof(p.host));
  w32(b.data(), 0x57525031);
  memcpy(b.data() + 4, p.account.data(), 8);
  memcpy(b.data() + 12, &p.host, sizeof(p.host));
  write_private(pair_path(c), b);
}
static std::string host_for(const Console &c, bool remote) {
  std::string host = remote ? c.remote : c.local;
  if (host.empty() || host.size() > 253 ||
      host.find_first_of(" \t\r\n/\\") != std::string::npos)
    throw std::runtime_error("Enter a valid console IP address or hostname");
  return host;
}
struct Engine::Impl {
  std::atomic<bool> alive{true}, streaming{false}, consoleConnected{false},
      fatal{false}, previewEnabled{true};
  std::atomic<bool> patternMode{false};
  std::mutex configMutex;
  std::atomic<int> source{1};
  SOCKET socket = INVALID_SOCKET;
  uint32_t session = 1;
  bool broadcastEnabled = false;
  std::atomic<uint64_t> discoveryPackets{0}, receivePackets{0}, sendErrors{0};
  std::atomic<uint32_t> discoveryTargetCount{0}, discoveryError{0},
      lastSendError{0};
  std::mutex peerMutex;
  sockaddr_in endpoint{};
  bool havePeer = false;
  uint32_t clientRun = 0;
  uint64_t peerSeen = 0;
  ConsoleMonitor consoleMonitor;
  std::mutex messageMutex;
  std::string message = "Ready. Open the 3DS app and enter this PC's address.";
  std::mutex coreMutex;
  ChiakiSession core{};
  bool initialized = false, started = false;
  ChiakiLog log{};
  RelayCtrlFailure failure;
  std::mutex eventMutex;
  std::condition_variable eventCv;
  struct Event {
    bool connected;
    std::string text;
  };
  std::deque<Event> events;
  std::thread networkThread, discoveryThread, consoleThread, videoThread,
      jpegThread, audioThread, pcmThread;
  std::mutex queueMutex;
  std::condition_variable queueCv;
  struct Compressed {
    std::vector<uint8_t> b;
    uint64_t stamp;
  };
  std::deque<Compressed> videoQueue;
  std::deque<std::vector<uint8_t>> audioQueue;
  std::atomic<int> audioRate{0}, audioChannels{0}, audioFrame{0};
  std::mutex imageMutex;
  std::vector<uint8_t> pending;
  uint64_t pendingStamp = 0;
  std::vector<uint8_t> previewImage;
  std::mutex adaptiveMutex;
  Adaptive adaptive;
  std::mutex cropMutex;
  uint16_t cropX = 32768, cropY = 32768, cropZoom = 4;
  PcmQueue pcm;
  std::unique_ptr<Usage> usage;
  StartOptions options;
  std::atomic<unsigned> width{320}, height{180};
  uint32_t vseq = 0, aseq = 0;
  std::atomic<uint64_t> pictures{0}, decoded{0}, sent{0}, videoDrops{0},
      audioDrops{0}, fullControls{0}, sentBytes{0}, lost{0}, jpegUs{0},
      ageUs{0}, shown{0}, decodeErrors{0}, badFrames{0};
  std::atomic<uint64_t> queueAgeUs{0}, decodeAgeUs{0}, maxAgeUs{0};
  std::atomic<unsigned> mtuIn{0}, mtuOut{0}, rttUs{0};
  std::mutex ratesMutex;
  uint64_t ratesAt = 0, ratesInput = 0, ratesDecoded = 0, ratesSent = 0;
  double inputFps = 0, decodedFps = 0, sentFps = 0;
  Impl() {
    uint32_t random = 0;
    if (BCryptGenRandom(nullptr, (PUCHAR)&random, 4,
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
      throw std::runtime_error("Secure random generator unavailable");
    session = random | 1;
    chiaki_lib_init();
    chiaki_log_init(&log, CHIAKI_LOG_ERROR, log_cb, this);
    socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET)
      throw std::runtime_error("Could not create relay socket");
    // Prevent another process from sharing the relay endpoint on Windows.
    BOOL exclusive = TRUE;
    if (setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   (const char *)&exclusive,
                   sizeof(exclusive)) == SOCKET_ERROR) {
      closesocket(socket);
      socket = INVALID_SOCKET;
      throw std::runtime_error("Could not reserve the relay UDP endpoint");
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(RELAY_PORT);
    if (bind(socket, (sockaddr *)&local, sizeof(local)) == SOCKET_ERROR) {
      closesocket(socket);
      socket = INVALID_SOCKET;
      throw std::runtime_error(
          "UDP port 29870 is unavailable. Close any other companion instance.");
    }
    // Discovery peers without this service may return ICMP port-unreachable.
    // Those packets must not turn the shared UDP receiver into reset errors.
    BOOL reportResets = FALSE;
    DWORD returned = 0;
    WSAIoctl(socket, SIO_UDP_CONNRESET, &reportResets, sizeof(reportResets),
             nullptr, 0, &returned, nullptr, nullptr);
    DWORD timeout = 100;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
               sizeof(timeout));
    BOOL broadcast = TRUE;
    broadcastEnabled = setsockopt(socket, SOL_SOCKET, SO_BROADCAST,
                                  reinterpret_cast<const char *>(&broadcast),
                                  sizeof(broadcast)) != SOCKET_ERROR;
    int buffer = 256 * 1024;
    setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&buffer, sizeof(buffer));
    usage = std::make_unique<Usage>(
        [this](uint32_t run, uint32_t serial, int outcome) {
          uint8_t b[8];
          w32(b, run);
          w32(b + 4, (uint32_t)outcome);
          std::lock_guard<std::mutex> g(peerMutex);
          if (havePeer && run == clientRun)
            send_to(endpoint, RELAY_USAGE_ACK, 0, 0, 1, 8, 0, 0, serial, b, 8);
        });
    try {
      networkThread = std::thread([this] { network(); });
      discoveryThread = std::thread([this] { discover(); });
    } catch (...) {
      alive = false;
      if (networkThread.joinable())
        networkThread.join();
      usage.reset();
      closesocket(socket);
      socket = INVALID_SOCKET;
      throw;
    }
  }
  ~Impl() {
    stop(false);
    alive = false;
    if (networkThread.joinable())
      networkThread.join();
    if (discoveryThread.joinable())
      discoveryThread.join();
    usage.reset();
    if (socket != INVALID_SOCKET)
      closesocket(socket);
  }
  void status(const std::string &s) {
    std::lock_guard<std::mutex> g(messageMutex);
    if (!fatal)
      message = s;
  }
  void fail(const std::string &s) {
    {
      std::lock_guard<std::mutex> g(messageMutex);
      if (!fatal)
        message = s;
      fatal = true;
    }
    source = 4;
    link();
    eventCv.notify_all();
    queueCv.notify_all();
  }
  bool peer() {
    std::lock_guard<std::mutex> g(peerMutex);
    return havePeer && millis() - peerSeen < 3000;
  }
  bool target(sockaddr_in &out) {
    std::lock_guard<std::mutex> g(peerMutex);
    if (!havePeer)
      return false;
    out = endpoint;
    return true;
  }
  bool send_to(const sockaddr_in &to, int type, uint32_t seq, unsigned index,
               unsigned count, unsigned total, unsigned w, unsigned h,
               uint32_t stamp, const uint8_t *data, size_t n) {
    if (n > RELAY_CHUNK)
      return false;
    uint8_t b[RELAY_HEADER + RELAY_CHUNK];
    relay_header(b, type, session, seq, index, count, total, w, h, stamp);
    if (n)
      memcpy(b + RELAY_HEADER, data, n);
    int result = sendto(socket, (char *)b, (int)(RELAY_HEADER + n), 0,
                        (sockaddr *)&to, sizeof(to));
    if (result > 0)
      sentBytes += result;
    if (result != static_cast<int>(RELAY_HEADER + n)) {
      lastSendError = result == SOCKET_ERROR ? WSAGetLastError() : WSAEMSGSIZE;
      ++sendErrors;
      return false;
    }
    return true;
  }
  void send(int type, uint32_t seq, unsigned index, unsigned count,
            unsigned total, unsigned w, unsigned h, uint32_t stamp,
            const uint8_t *data, size_t n) {
    sockaddr_in to;
    if (target(to))
      send_to(to, type, seq, index, count, total, w, h, stamp, data, n);
  }
  void link() {
    uint8_t b[4];
    w32(b, source);
    send(RELAY_LINK, 0, 0, 1, 4, 0, 0, (uint32_t)millis(), b, 4);
  }
  uint64_t interval() {
    std::lock_guard<std::mutex> g(adaptiveMutex);
    return adaptive.interval();
  }
  void reset_adaptive() {
    std::lock_guard<std::mutex> g(adaptiveMutex);
    adaptive.reset();
  }
  bool controls(ChiakiControllerState state) {
    std::unique_lock<std::mutex> g(coreMutex, std::try_to_lock);
    return g.owns_lock() && started && streaming &&
           chiaki_session_set_controller_state(&core, &state) ==
               CHIAKI_ERR_SUCCESS;
  }
  bool neutral() {
    ChiakiControllerState s;
    chiaki_controller_state_set_idle(&s);
    return controls(s);
  }
  static void log_cb(ChiakiLogLevel level, const char *text, void *u) {
    if (level == CHIAKI_LOG_ERROR)
      ((Impl *)u)->failure.observe(text);
  }
  static void event_cb(ChiakiEvent *e, void *u) {
    auto &s = *(Impl *)u;
    if (!s.streaming)
      return;
    Event event{};
    if (e->type == CHIAKI_EVENT_CONNECTED) {
      s.consoleConnected = true;
      s.mtuIn = s.core.mtu_in;
      s.mtuOut = s.core.mtu_out;
      s.rttUs = s.core.rtt_us;
      s.source = 2;
      s.link();
      event.connected = true;
      event.text = "Console connected";
    } else if (e->type == CHIAKI_EVENT_QUIT) {
      s.consoleConnected = false;
      s.source = 1;
      s.link();
      event.text = std::string("Disconnected: ") +
                   chiaki_quit_reason_string(e->quit.reason);
      auto detail = s.failure.detail();
      if (!detail.empty())
        event.text += " (" + detail + ")";
      auto app = s.failure.application();
      if (app == CHIAKI_RP_APPLICATION_REASON_CRASH ||
          e->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH)
        event.text += ". Restart the console and try again.";
      else if (app == CHIAKI_RP_APPLICATION_REASON_IN_USE ||
               e->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE)
        event.text += ". Disconnect other Remote Play sessions.";
      else if (app == CHIAKI_RP_APPLICATION_REASON_REGIST_FAILED ||
               app == CHIAKI_RP_APPLICATION_REASON_INVALID_PSN_ID)
        event.text += ". Sign in to the intended console user and pair again "
                      "with a fresh PIN.";
      else if (app == CHIAKI_RP_APPLICATION_REASON_RP_VERSION)
        event.text += ". Check console and companion updates.";
    } else if (e->type == CHIAKI_EVENT_LOGIN_PIN_REQUEST) {
      s.status("Sign in on the console to enter its login passcode.");
      return;
    } else
      return;
    {
      std::lock_guard<std::mutex> g(s.eventMutex);
      s.events.push_back(event);
    }
    s.eventCv.notify_all();
  }
  static void display_cb(void *u, bool unavailable) {
    if (unavailable)
      ((Impl *)u)->status("Console temporarily cannot share this screen.");
  }
  static bool video_cb(uint8_t *b, size_t n, int32_t lostFrames, bool,
                       void *u) {
    auto &s = *(Impl *)u;
    if (!s.streaming || s.fatal || !b || !n || n > 2 * 1024 * 1024)
      return false;
    s.pictures++;
    if (lostFrames > 0)
      s.lost += lostFrames;
    bool accepted;
    {
      std::lock_guard<std::mutex> g(s.queueMutex);
      accepted = s.videoQueue.size() < 32;
      if (accepted)
        s.videoQueue.push_back({std::vector<uint8_t>(b, b + n), micros()});
      else
        s.videoDrops++;
    }
    s.queueCv.notify_all();
    if (!accepted)
      chiaki_session_request_idr(&s.core);
    return accepted;
  }
  static void audio_header(ChiakiAudioHeader *h, void *u) {
    auto &s = *(Impl *)u;
    s.audioChannels = h->channels;
    s.audioFrame = h->frame_size;
    s.audioRate = h->rate;
  }
  static void audio_cb(uint8_t *b, size_t n, void *u) {
    auto &s = *(Impl *)u;
    if (!s.streaming || !b || !n || n > 4096)
      return;
    {
      std::lock_guard<std::mutex> g(s.queueMutex);
      if (s.audioQueue.size() < 32)
        s.audioQueue.emplace_back(b, b + n);
      else
        s.audioDrops++;
    }
    s.queueCv.notify_all();
  }
  // This is normal outbound LAN discovery. The 3DS only accepts a greeting
  // from the PC address selected on the handheld. Once it replies, unicast
  // keepalives maintain the same UDP flow without a Windows firewall rule.
  void discover() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    DiscoveryTargets candidates;
    uint64_t refreshed = 0;
    while (alive) {
      try {
        sockaddr_in known{};
        bool connected = peer();
        if (target(known) &&
            send_to(known, RELAY_HELLO, RELAY_COMPAT_TAG, 0, 1, 0, width,
                    height, RELAY_RELEASE, nullptr, 0))
          ++discoveryPackets;
        if (!connected) {
          uint64_t now = millis();
          if (!refreshed || now - refreshed >= 5000) {
            candidates = discovery_targets();
            discoveryTargetCount =
                candidates.broadcasts.size() + candidates.neighbors.size();
            discoveryError = candidates.error;
            refreshed = now;
          }
          sockaddr_in destination{};
          destination.sin_family = AF_INET;
          destination.sin_port = htons(29871);
          for (const auto *addresses :
               {&candidates.broadcasts, &candidates.neighbors}) {
            if (addresses == &candidates.broadcasts && !broadcastEnabled)
              continue;
            for (auto address : *addresses) {
              if (!alive || peer())
                break;
              destination.sin_addr.s_addr = address;
              if (send_to(destination, RELAY_HELLO, RELAY_COMPAT_TAG, 0, 1, 0,
                          width, height, RELAY_RELEASE, nullptr, 0))
                ++discoveryPackets;
            }
          }
        }
      } catch (...) {
        discoveryError = ERROR_NOT_ENOUGH_MEMORY;
      }
      for (int delay = 0; delay < 20 && alive; ++delay)
        pause_us(50000);
    }
  }
  void network() {
    bool isNeutral = true, haveInput = false;
    uint32_t lastInput = 0;
    uint64_t controlSeen = 0;
    uint8_t b[RELAY_HEADER + RELAY_CHUNK + 1];
    while (alive) {
      if (!isNeutral && (!streaming || !peer() || millis() - controlSeen > 500))
        isNeutral = neutral();
      sockaddr_in from{};
      int fromSize = sizeof(from);
      int n = recvfrom(socket, (char *)b, sizeof(b), 0, (sockaddr *)&from,
                       &fromSize);
      if (n == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAETIMEDOUT && error != WSAEWOULDBLOCK &&
            error != WSAECONNRESET && alive) {
          status("Relay receive failed (Windows error " +
                 std::to_string(error) + ")");
          pause_us(100000);
        }
        continue;
      }
      ++receivePackets;
      if (!relay_valid(b, n))
        continue;
      int type = b[4];
      if (type == RELAY_HELLO && n == RELAY_HEADER) {
        bool other = false, changed = false;
        uint32_t run = r32(b + 28);
        {
          std::lock_guard<std::mutex> g(peerMutex);
          bool same = havePeer &&
                      endpoint.sin_addr.s_addr == from.sin_addr.s_addr &&
                      endpoint.sin_port == from.sin_port;
          other = havePeer && !same && millis() - peerSeen < 3000;
          if (!other && r32(b + 12) == RELAY_COMPAT_TAG) {
            changed = !same || run != clientRun;
            endpoint = from;
            havePeer = true;
            peerSeen = millis();
            clientRun = run;
          } else if (!other && same)
            havePeer = false;
        }
        if (other)
          continue;
        if (r32(b + 12) != RELAY_COMPAT_TAG) {
          std::string text = "App versions do not match. Update both apps to "
                             "release 42 together.";
          send_to(from, RELAY_ERROR, RELAY_COMPAT_TAG, 0, 1,
                  (unsigned)text.size(), width, height, RELAY_RELEASE,
                  (uint8_t *)text.data(), text.size());
          status(text);
          continue;
        }
        if (changed) {
          usage->reset(run);
          haveInput = false;
          reset_adaptive();
          {
            std::lock_guard<std::mutex> g(cropMutex);
            cropX = cropY = 32768;
            cropZoom = 4;
          }
          shown = decodeErrors = badFrames = 0;
          if (!fatal)
            status("3DS connected");
        }
        send(RELAY_HELLO, RELAY_COMPAT_TAG, 0, 1, 0, width, height,
             RELAY_RELEASE, nullptr, 0);
        link();
        continue;
      }
      bool valid = false;
      {
        std::lock_guard<std::mutex> g(peerMutex);
        valid = havePeer && endpoint.sin_addr.s_addr == from.sin_addr.s_addr &&
                endpoint.sin_port == from.sin_port && r32(b + 8) == session;
      }
      if (!valid)
        continue;
      if (type == RELAY_USAGE_REPORT) {
        usage->report(b, n);
        continue;
      }
      if (type == RELAY_USAGE_CANCEL) {
        usage->cancel(b, n);
        continue;
      }
      bool refresh = false;
      if (type == RELAY_STATUS && n == 44 && r32(b + 20) == 12 &&
          !r16(b + 16) && r16(b + 18) == 1) {
        shown = r32(b + 32);
        decodeErrors = r32(b + 36);
        badFrames = r32(b + 40);
        refresh = true;
      }
      if (type == RELAY_FEEDBACK) {
        std::lock_guard<std::mutex> g(adaptiveMutex);
        adaptive.feedback(b, n);
      }
      if (type == RELAY_PING && n == RELAY_HEADER) {
        refresh = true;
        send(RELAY_PONG, r32(b + 12), 0, 1, 0, 0, 0, r32(b + 28), nullptr, 0);
        auto d = diagnostics();
        if (d.size() > RELAY_CHUNK)
          d = diagnostics(true);
        if (d.size() <= RELAY_CHUNK)
          send(8, 0, 0, 1, (unsigned)d.size(), 0, 0, 0, (uint8_t *)d.data(),
               d.size());
        link();
      }
      if ((type == RELAY_INPUT && n == 42) ||
          (type == RELAY_INPUT_FULL && n == 100 && r32(b + 20) == 68 &&
           !r16(b + 16) && r16(b + 18) == 1)) {
        uint32_t seq = r32(b + 28);
        if (!haveInput || (int32_t)(seq - lastInput) > 0) {
          ChiakiControllerState state;
          chiaki_controller_state_set_idle(&state);
          bool good = true;
          if (type == RELAY_INPUT_FULL)
            good = relay_control_decode(b + 32, 68, &state);
          else {
            state.buttons = r32(b + 32);
            state.left_x = (int16_t)r16(b + 36);
            state.left_y = (int16_t)r16(b + 38);
            state.l2_state = b[40];
            state.r2_state = b[41];
          }
          if (good && (patternMode || controls(state))) {
            if (type == RELAY_INPUT_FULL)
              fullControls++;
            lastInput = seq;
            haveInput = true;
            controlSeen = millis();
            refresh = true;
            isNeutral = false;
          }
        }
      }
      if (type == RELAY_CROP && n == 38 && r32(b + 20) == 6 && !r16(b + 16) &&
          r16(b + 18) == 1) {
        uint16_t z = r16(b + 36);
        if (z >= 4 && z <= 32) {
          std::lock_guard<std::mutex> g(cropMutex);
          cropX = r16(b + 32);
          cropY = r16(b + 34);
          cropZoom = z;
        }
      }
      if (type == RELAY_BYE && n == RELAY_HEADER) {
        {
          std::lock_guard<std::mutex> g(peerMutex);
          havePeer = false;
        }
        reset_adaptive();
        isNeutral = neutral();
        status("3DS stream closed. Choose Disconnect or Rest mode.");
      }
      if (refresh) {
        std::lock_guard<std::mutex> g(peerMutex);
        peerSeen = millis();
      }
    }
  }
  bool wait_ms(uint64_t ms) {
    auto end = millis() + ms;
    while (streaming && !fatal && millis() < end)
      pause_us(std::min<uint64_t>(50000, (end - millis() + 1) * 1000));
    return streaming && !fatal;
  }
  bool responds(const std::string &host) {
    addrinfo hint{}, *list = nullptr;
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), "9295", &hint, &list))
      return false;
    bool ready = false;
    for (auto *p = list; p && streaming; p = p->ai_next) {
      SOCKET s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
      if (s == INVALID_SOCKET)
        continue;
      u_long mode = 1;
      ioctlsocket(s, FIONBIO, &mode);
      int result = connect(s, p->ai_addr, (int)p->ai_addrlen);
      if (!result)
        ready = true;
      else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(s, &set);
        timeval timeout{0, 700000};
        if (select(0, nullptr, &set, nullptr, &timeout) > 0) {
          int error = 0, len = sizeof(error);
          ready =
              getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&error, &len) == 0 &&
              !error;
        }
      }
      closesocket(s);
      if (ready)
        break;
    }
    freeaddrinfo(list);
    return ready;
  }
  void wake(const Console &c, bool remote) {
    Pair pair;
    if (!load_pair(c, pair))
      throw std::runtime_error("Pair this console locally before using Wake.");
    char key[9]{};
    size_t n = 0;
    while (n < 8 && pair.host.rp_regist_key[n]) {
      char ch = (char)pair.host.rp_regist_key[n];
      if (!isxdigit((unsigned char)ch))
        throw std::runtime_error("Saved wake credential is invalid");
      key[n++] = ch;
    }
    for (size_t i = n; i < 16; i++)
      if (pair.host.rp_regist_key[i])
        throw std::runtime_error("Saved wake credential is invalid");
    if (!n)
      throw std::runtime_error("Saved wake credential is empty");
    auto host = host_for(c, remote);
    if (chiaki_discovery_wakeup(&log, nullptr, host.c_str(),
                                strtoull(key, nullptr, 16),
                                c.ps5) != CHIAKI_ERR_SUCCESS)
      throw std::runtime_error(
          "Could not send wake request. Check the address and network.");
    status("Wake request sent");
  }
  struct Registration {
    std::atomic<bool> done{false};
    bool success = false;
    ChiakiRegisteredHost host{};
  };
  static void regist_cb(ChiakiRegistEvent *e, void *u) {
    auto &r = *(Registration *)u;
    if (e->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS) {
      r.success = true;
      r.host = *e->registered_host;
    }
    r.done.store(true, std::memory_order_release);
  }
  bool prepare_pair(Pair &pair, const std::string &host) {
    if (options.pin.empty() && load_pair(options.console, pair))
      return true;
    if (options.remote)
      throw std::runtime_error(
          "Pair locally with the console before using its Internet address.");
    if (options.pin.size() != 8 ||
        options.pin.find_first_not_of("0123456789") != std::string::npos)
      throw std::runtime_error("Enter the console's eight-digit Link Device "
                               "PIN for first registration.");
    status("Registering with console...");
    Registration r;
    ChiakiRegist reg{};
    ChiakiRegistInfo info{};
    info.target =
        options.console.ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;
    info.host = host.c_str();
    info.pin = (uint32_t)strtoul(options.pin.c_str(), nullptr, 10);
    if (!account_id(options.console.account, pair.account))
      throw std::runtime_error(
          "Account ID must be a Base64 ID decoding to eight bytes");
    memcpy(info.psn_account_id, pair.account.data(), 8);
    auto error = chiaki_regist_start(&reg, &log, &info, regist_cb, &r);
    if (error != CHIAKI_ERR_SUCCESS)
      throw std::runtime_error("Could not start console registration");
    for (int i = 0;
         i < 3000 && streaming && !r.done.load(std::memory_order_acquire); i++)
      pause_us(10000);
    if (!r.done.load(std::memory_order_acquire))
      chiaki_regist_stop(&reg);
    chiaki_regist_fini(&reg);
    if (!streaming)
      return false;
    if (!r.success)
      throw std::runtime_error("Registration failed. Check address, Account ID "
                               "and a fresh Link Device PIN.");
    pair.host = r.host;
    save_pair(options.console, pair);
    status("Console paired");
    return true;
  }
  void cleanup_core() {
    std::lock_guard<std::mutex> g(coreMutex);
    if (started) {
      chiaki_session_stop(&core);
      chiaki_session_join(&core);
      started = false;
    }
    if (initialized) {
      chiaki_session_fini(&core);
      initialized = false;
    }
    consoleConnected = false;
  }
  void connect_console() {
    struct Awake {
      Awake() { SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED); }
      ~Awake() { SetThreadExecutionState(ES_CONTINUOUS); }
    } awake;
    try {
      auto host = host_for(options.console, options.remote);
      Pair pair;
      bool cached = options.pin.empty() && load_pair(options.console, pair);
      auto running = [this] { return streaming && !fatal; };
      auto report = [this](const std::string &text) { status(text); };
      auto wait = [this](unsigned ms) { return wait_ms(ms); };
      if (!wake_before_connect(cached, running, [this, &host] { return responds(host); },
                               [this] { wake(options.console, options.remote); }, report, wait))
        return;
      if (!prepare_pair(pair, host))
        return;
      connect_with_retries(running, [this, &pair, &host]() -> ConnectionEnd {
        failure.reset();
        {
          std::lock_guard<std::mutex> g(eventMutex);
          events.clear();
        }
        ChiakiConnectInfo info{};
        info.ps5 = options.console.ps5;
        info.host = host.c_str();
        memcpy(info.psn_account_id, pair.account.data(), 8);
        memcpy(info.regist_key, pair.host.rp_regist_key, 16);
        memcpy(info.morning, pair.host.rp_key, 16);
        if (!relay_stream_profile(&info.video_profile, options.height,
                                  options.bitrate, options.fps))
          throw std::runtime_error("Invalid stream profile");
        info.packet_loss_max = .05;
        info.enable_idr_on_fec_failure = false;
        {
          std::lock_guard<std::mutex> g(coreMutex);
          if (!streaming || fatal)
            return {};
          if (chiaki_session_init(&core, &info, &log) != CHIAKI_ERR_SUCCESS)
            throw std::runtime_error("Console session initialization failed");
          initialized = true;
          ChiakiCtrlDisplaySink display{};
          display.cantdisplay_cb = display_cb;
          display.user = this;
          chiaki_session_ctrl_set_display_sink(&core, &display);
          chiaki_session_set_event_cb(&core, event_cb, this);
          chiaki_session_set_video_sample_cb(&core, video_cb, this);
          ChiakiAudioSink sink{};
          sink.header_cb = audio_header;
          sink.frame_cb = audio_cb;
          sink.user = this;
          chiaki_session_set_audio_sink(&core, &sink);
          if (chiaki_session_start(&core) != CHIAKI_ERR_SUCCESS)
            throw std::runtime_error("Could not start console session");
          started = true;
        }
        bool established = false;
        std::string ended;
        uint64_t deadline = millis() + 60000;
        while (streaming && !fatal) {
          Event e;
          bool got = false;
          {
            std::unique_lock<std::mutex> g(eventMutex);
            eventCv.wait_for(g, std::chrono::milliseconds(100), [&] {
              return !events.empty() || !streaming || fatal;
            });
            if (!events.empty()) {
              e = events.front();
              events.pop_front();
              got = true;
            }
          }
          if (got) {
            if (e.connected) {
              established = true;
              status(e.text);
            } else {
              ended = e.text;
              break;
            }
          }
          if (!established && millis() >= deadline) {
            ended = "Console connection timed out after 60 seconds";
            break;
          }
        }
        return {established, ended};
      }, [this] { cleanup_core(); }, report, wait,
         [this](const std::string &text) { fail(text); });
    } catch (const std::exception &e) {
      if (streaming)
        fail(e.what());
    }
    cleanup_core();
  }
  void start(const StartOptions &o) {
    if (streaming)
      throw std::runtime_error("Disconnect the current session first");
    {
      std::lock_guard<std::mutex> g(configMutex);
      options = o;
    }
    patternMode = o.pattern;
    ChiakiConnectVideoProfile profile{};
    if (!relay_stream_profile(&profile, o.height, o.bitrate, o.fps) ||
        o.image < 0 || o.image > 2 || o.quality < 1 || o.quality > 100)
      throw std::runtime_error("Invalid stream settings");
    static const unsigned widths[] = {240, 320, 400},
                          heights[] = {135, 180, 225};
    width = widths[o.image];
    height = heights[o.image];
    fatal = false;
    source = o.pattern ? 3 : 1;
    {
      std::lock_guard<std::mutex> guard(ratesMutex);
      pictures = decoded = sent = videoDrops = audioDrops = fullControls =
          sentBytes = lost = jpegUs = ageUs = 0;
      ratesAt = millis();
      ratesInput = ratesDecoded = ratesSent = 0;
      inputFps = decodedFps = sentFps = 0;
    }
    audioRate = audioChannels = audioFrame = 0;
    queueAgeUs = decodeAgeUs = maxAgeUs = 0;
    mtuIn = mtuOut = rttUs = 0;
    {
      std::lock_guard<std::mutex> guard(ratesMutex);
      ratesAt = millis();
      ratesInput = ratesDecoded = ratesSent = 0;
      inputFps = decodedFps = sentFps = 0;
    }
    vseq = aseq = 0;
    reset_adaptive();
    {
      std::lock_guard<std::mutex> g(queueMutex);
      videoQueue.clear();
      audioQueue.clear();
    }
    {
      std::lock_guard<std::mutex> g(imageMutex);
      pending.clear();
      previewImage.clear();
    }
    pcm.clear();
    streaming = true;
    link();
    try {
      if (o.pattern) {
        status("Connection test running");
        videoThread = std::thread([this] { pattern(); });
        audioThread = std::thread([this] { tone(); });
      } else {
        videoThread = std::thread([this] { decode_video(); });
        jpegThread = std::thread([this] { encode_video(); });
        audioThread = std::thread([this] { decode_audio(); });
        pcmThread = std::thread([this] { send_audio(); });
        consoleThread = std::thread([this] { connect_console(); });
      }
    } catch (...) {
      stop(false);
      throw;
    }
  }
  void stop(bool rest) {
    bool wasActive = streaming;
    bool request = false;
    if (rest && consoleConnected) {
      std::lock_guard<std::mutex> g(coreMutex);
      request = started && chiaki_session_goto_bed(&core) == CHIAKI_ERR_SUCCESS;
    }
    if (request)
      for (int i = 0; i < 200 && consoleConnected; i++)
        pause_us(10000);
    streaming = false;
    source = 5;
    link();
    queueCv.notify_all();
    eventCv.notify_all();
    if (consoleThread.joinable())
      consoleThread.join();
    else
      cleanup_core();
    for (auto *t : {&videoThread, &jpegThread, &audioThread, &pcmThread})
      if (t->joinable())
        t->join();
    source = 1;
    link();
    pcm.clear();
    if (wasActive && !fatal)
      status(rest ? (request
                         ? "Rest mode requested. Relay disconnected."
                         : "Could not request Rest mode. Relay disconnected.")
                  : "Relay disconnected. Ready.");
  }
  void decode_video() {
    AVCodecContext *ctx = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    try {
      const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
      if (!codec)
        throw std::runtime_error("H.264 decoder is unavailable");
      ctx = avcodec_alloc_context3(codec);
      if (!ctx)
        throw std::bad_alloc();
      ctx->thread_count = 2;
      ctx->thread_type = FF_THREAD_SLICE;
      ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
      if (avcodec_open2(ctx, codec, nullptr) < 0)
        throw std::runtime_error("Cannot initialize H.264 decoder");
      frame = av_frame_alloc();
      packet = av_packet_alloc();
      if (!frame || !packet)
        throw std::bad_alloc();
      uint64_t next = 0;
      while (streaming && !fatal) {
        Compressed input;
        if (packet->size == 0) {
          {
            std::unique_lock<std::mutex> g(queueMutex);
            queueCv.wait_for(g, std::chrono::milliseconds(10), [&] {
              return !videoQueue.empty() || !streaming || fatal;
            });
            if (videoQueue.empty())
              continue;
            input = std::move(videoQueue.front());
            videoQueue.pop_front();
          }
          if (av_new_packet(packet, (int)input.b.size()) < 0)
            throw std::bad_alloc();
          memcpy(packet->data, input.b.data(), input.b.size());
          packet->pts = (int64_t)input.stamp;
        }
        const auto receivedAt = (uint64_t)packet->pts;
        queueAgeUs = micros() - std::min(micros(), receivedAt);
        int result = avcodec_send_packet(ctx, packet);
        if (result != AVERROR(EAGAIN)) {
          av_packet_unref(packet);
          if (result < 0) {
            videoDrops++;
            std::unique_lock<std::mutex> guard(coreMutex, std::try_to_lock);
            if (guard.owns_lock() && started)
              chiaki_session_request_idr(&core);
            continue;
          }
        }
        while (avcodec_receive_frame(ctx, frame) == 0) {
          decoded++;
          uint64_t now = micros();
          const uint64_t stamp =
              frame->pts > 0 ? (uint64_t)frame->pts : receivedAt;
          decodeAgeUs = now - std::min(now, stamp);
          bool hasPeer = peer();
          if (!hasPeer && !previewEnabled) {
            next = 0;
            av_frame_unref(frame);
            continue;
          }
          uint64_t period = hasPeer ? interval() : 100000;
          if (now < next) {
            videoDrops++;
            av_frame_unref(frame);
            continue;
          }
          bool busy;
          {
            std::lock_guard<std::mutex> g(imageMutex);
            busy = !pending.empty();
          }
          if (busy) {
            videoDrops++;
            av_frame_unref(frame);
            continue;
          }
          if (frame->format != AV_PIX_FMT_YUV420P &&
              frame->format != AV_PIX_FMT_YUVJ420P)
            throw std::runtime_error(
                "Unsupported H.264 pixel format. Use H.264 SDR.");
          int cx, cy, cw = frame->width, ch = frame->height;
          uint16_t x, y, z;
          {
            std::lock_guard<std::mutex> g(cropMutex);
            x = cropX;
            y = cropY;
            z = cropZoom;
          }
          if (z > 4) {
            cw = std::max(2, (cw * 4 / z) & ~1);
            ch = std::max(2, (ch * 4 / z) & ~1);
          }
          cx = ((frame->width - cw) * (uint64_t)x / 65535) & ~1;
          cy = ((frame->height - ch) * (uint64_t)y / 65535) & ~1;
          int layout[13] = {};
          const uint8_t *planes[3];
          size_t sizes[3];
          for (int i = 0; i < 3; i++) {
            planes[i] = frame->data[i];
            if (frame->linesize[i] <= 0)
              throw std::runtime_error("Unsupported decoded picture stride");
            layout[i * 3 + 1] = frame->linesize[i];
            layout[i * 3 + 2] = 1;
            sizes[i] = (size_t)frame->linesize[i] *
                       (i ? (frame->height + 1) / 2 : frame->height);
          }
          layout[9] = cx;
          layout[10] = cy;
          layout[11] = cw;
          layout[12] = ch;
          std::vector<uint8_t> nv(relay_nv21_bytes(width, height));
          if (!relay_scale_nv21(planes, sizes, layout, nv.data(), nv.size(),
                                width, height))
            throw std::runtime_error("Picture scaling failed");
          {
            std::lock_guard<std::mutex> g(imageMutex);
            pending = std::move(nv);
            pendingStamp = frame->pts > 0 ? (uint64_t)frame->pts : receivedAt;
          }
          next = now + period;
          av_frame_unref(frame);
        }
      }
    } catch (const std::exception &e) {
      if (streaming)
        fail(std::string("Video decode failed: ") + e.what());
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
  }
  void jpeg(const std::vector<uint8_t> &nv, uint64_t stamp) {
    unsigned long n = 0;
    auto begin = micros();
    uint8_t *b =
        relay_encode_jpeg(nv.data(), options.quality, &n, width, height);
    if (!b)
      throw std::runtime_error("JPEG encoding failed");
    std::vector<uint8_t> bytes(b, b + n);
    free(b);
    jpegUs += micros() - begin;
    {
      std::lock_guard<std::mutex> g(imageMutex);
      if (previewEnabled)
        previewImage = bytes;
    }
    send_jpeg(bytes);
    const uint64_t now = micros();
    ageUs = now - std::min(now, stamp);
    auto old = maxAgeUs.load();
    while (ageUs > old && !maxAgeUs.compare_exchange_weak(old, ageUs)) {
    }
  }
  void encode_video() {
    try {
      while (streaming && !fatal) {
        std::vector<uint8_t> nv;
        uint64_t stamp = 0;
        {
          std::lock_guard<std::mutex> g(imageMutex);
          nv.swap(pending);
          stamp = pendingStamp;
        }
        if (nv.empty()) {
          pause_us(1000);
          continue;
        }
        jpeg(nv, stamp);
      }
    } catch (const std::exception &e) {
      if (streaming)
        fail(e.what());
    }
  }
  void send_jpeg(const std::vector<uint8_t> &b) {
    if (!peer())
      return;
    if (b.size() > RELAY_JPEG_MAX) {
      videoDrops++;
      return;
    }
    unsigned count = (b.size() + RELAY_CHUNK - 1) / RELAY_CHUNK;
    uint32_t seq = ++vseq, stamp = (uint32_t)millis();
    uint64_t gap =
                 count <= 1 ? 0 : std::min<uint64_t>(1000, 12000 / (count - 1)),
             next = 0;
    for (unsigned i = 0; i < count && streaming && peer(); i++) {
      uint64_t now = micros();
      if (now < next)
        pause_us(next - now);
      size_t off = i * RELAY_CHUNK,
             n = std::min<size_t>(RELAY_CHUNK, b.size() - off);
      send(RELAY_JPEG, seq, i, count, (unsigned)b.size(), width, height, stamp,
           b.data() + off, n);
      next = micros() + gap;
    }
    sent++;
  }
  void decode_audio() {
    OpusDecoder *decoder = nullptr;
    try {
      while (streaming && !fatal && !audioRate)
        pause_us(5000);
      if (!streaming || fatal)
        return;
      int rate = audioRate, channels = audioChannels, error = 0;
      decoder = opus_decoder_create(rate, channels, &error);
      if (!decoder || error)
        throw std::runtime_error("Cannot initialize Opus decoder");
      Resampler resampler(rate, channels);
      std::vector<int16_t> samples((size_t)rate * 120 / 1000 * channels);
      while (streaming && !fatal) {
        std::vector<uint8_t> b;
        {
          std::unique_lock<std::mutex> g(queueMutex);
          queueCv.wait_for(g, std::chrono::milliseconds(10), [&] {
            return !audioQueue.empty() || !streaming || fatal;
          });
          if (audioQueue.empty())
            continue;
          b = std::move(audioQueue.front());
          audioQueue.pop_front();
        }
        int frames = opus_decode(decoder, b.data(), (opus_int32)b.size(),
                                 samples.data(), rate * 120 / 1000, 0);
        if (frames < 0) {
          audioDrops++;
          continue;
        }
        if (peer())
          pcm.offer(resampler.convert(samples.data(), frames));
      }
    } catch (const std::exception &e) {
      if (streaming)
        fail(std::string("Sound decode failed: ") + e.what());
    }
    if (decoder)
      opus_decoder_destroy(decoder);
  }
  void send_pcm(const uint8_t *b) {
    send(RELAY_PCM, ++aseq, 0, 1, 480, 12000, 1, (uint32_t)millis(), b, 480);
  }
  void send_audio() {
    std::array<uint8_t, 480> b;
    while (streaming && !fatal) {
      if (!peer()) {
        pcm.clear();
        pause_us(5000);
        continue;
      }
      if (pcm.poll(b))
        send_pcm(b.data());
      else
        pause_us(1000);
    }
  }
  void pattern() {
    try {
      uint64_t frame = 0;
      while (streaming && !fatal) {
        auto begin = micros();
        std::vector<uint8_t> nv(relay_nv21_bytes(width, height), 128);
        for (unsigned y = 0; y < height; y++)
          for (unsigned x = 0; x < width; x++)
            nv[y * width + x] = (uint8_t)((x + frame * 3) % 200 + 28);
        unsigned pos = (unsigned)(frame * 5 % (width - 40));
        for (unsigned y = height / 3;
             y < std::min(height.load(), height / 3 + 40); y++)
          for (unsigned x = pos; x < pos + 40; x++)
            nv[y * width + x] = 235;
        jpeg(nv, begin);
        frame++;
        pictures++;
        decoded++;
        uint64_t period = interval(), used = micros() - begin;
        if (used < period)
          pause_us(period - used);
      }
    } catch (const std::exception &e) {
      if (streaming)
        fail(e.what());
    }
  }
  void tone() {
    uint64_t sample = 0;
    while (streaming && !fatal) {
      std::array<uint8_t, 480> b;
      for (int i = 0; i < 240; i++) {
        int16_t v =
            (int16_t)(sin(2 * 3.141592653589793 * 440 * sample++ / 12000) *
                      1800);
        b[i * 2] = (uint8_t)v;
        b[i * 2 + 1] = (uint8_t)(v >> 8);
      }
      if (peer())
        send_pcm(b.data());
      pause_us(20000);
    }
  }
  std::string diagnostics(bool compact = false) {
    StartOptions configuration;
    {
      std::lock_guard<std::mutex> g(configMutex);
      configuration = options;
    }
    double targetFps, service, display, age;
    {
      std::lock_guard<std::mutex> g(adaptiveMutex);
      adaptive.interval();
      targetFps = adaptive.fps;
      service = adaptive.service;
      display = adaptive.display;
      age = adaptive.age;
    }
    double currentInput, currentDecoded, currentSent;
    {
      std::lock_guard<std::mutex> guard(ratesMutex);
      auto now = millis();
      if (now - ratesAt >= 1000) {
        double seconds = (now - ratesAt) / 1000.;
        auto a = pictures.load(), b = decoded.load(), c = sent.load();
        inputFps = (a - ratesInput) / seconds;
        decodedFps = (b - ratesDecoded) / seconds;
        sentFps = (c - ratesSent) / seconds;
        ratesAt = now;
        ratesInput = a;
        ratesDecoded = b;
        ratesSent = c;
      }
      currentInput = inputFps;
      currentDecoded = decodedFps;
      currentSent = sentFps;
    }
    size_t queuedVideo;
    {
      std::lock_guard<std::mutex> guard(queueMutex);
      queuedVideo = videoQueue.size();
    }
    uint64_t pcmDropped;
    size_t pcmQueued;
    {
      std::lock_guard<std::mutex> guard(pcm.mutex);
      pcmDropped = pcm.dropped;
      pcmQueued = pcm.slots.size();
    }
    std::ostringstream s;
    s << std::fixed << std::setprecision(1);
    if (compact) {
      s << "{\"app_version\":42,\"desktop_version\":\"" RELAY_DESKTOP_VERSION
           "\",\"compact\":true,\"input_fps\":"
        << currentInput << ",\"decoded_fps\":" << currentDecoded
        << ",\"sent_fps\":" << currentSent << ",\"adaptive\":["
        << targetFps * 100 << "," << service << "," << display * 100 << ","
        << age << "]" << ",\"phone_age_us\":[" << queueAgeUs << ","
        << decodeAgeUs << "," << ageUs << "," << maxAgeUs << "," << queuedVideo
        << "]" << ",\"video_dropped\":" << videoDrops
        << ",\"audio_dropped\":" << audioDrops << "," << usage->json() << "}";
      return s.str();
    }
    s << "{\"app_version\":42,\"desktop_version\":\"" RELAY_DESKTOP_VERSION
         "\",\"platform\":\"windows\",\"core_backend\":"
         "\"chiaki-ng-v1.10.0\",\"image_size\":["
      << width << "," << height
      << "],\"network\":{\"received\":" << receivePackets
      << ",\"send_errors\":" << sendErrors
      << ",\"last_error\":" << lastSendError
      << ",\"discovery_targets\":" << discoveryTargetCount
      << ",\"discovery_sent\":" << discoveryPackets
      << ",\"discovery_error\":" << discoveryError
      << ",\"broadcast\":" << (broadcastEnabled ? "true" : "false")
      << "},\"ps5_request\":["
      << (configuration.height == 360   ? 640
          : configuration.height == 540 ? 960
          : configuration.height == 720 ? 1280
                                        : 1920)
      << "," << configuration.height << "," << configuration.fps << ","
      << configuration.bitrate << "],\"source_audio\":[" << audioRate << ","
      << audioChannels << "," << audioFrame << "],\"input\":" << pictures
      << ",\"decoded\":" << decoded << ",\"sent\":" << sent
      << ",\"input_fps\":" << currentInput
      << ",\"decoded_fps\":" << currentDecoded
      << ",\"sent_fps\":" << currentSent << ",\"video_dropped\":" << videoDrops
      << ",\"audio_dropped\":" << audioDrops << ",\"reported_lost\":" << lost
      << ",\"full_controls\":" << fullControls << ",\"jpeg_us\":" << jpegUs
      << ",\"adaptive\":[" << targetFps * 100 << "," << service << ","
      << display * 100 << "," << age << "],\"phone_age_us\":[" << queueAgeUs
      << "," << decodeAgeUs << "," << ageUs << "," << maxAgeUs << ","
      << queuedVideo << "],\"connection\":[" << mtuIn << "," << mtuOut << ","
      << rttUs << "],\"pcm_queue\":[" << pcmDropped << "," << pcmQueued
      << "],\"client\":[" << shown << "," << decodeErrors << "," << badFrames
      << "]," << usage->json() << "}";
    return s.str();
  }
};
Engine::Engine() : p(new Impl) {}
Engine::~Engine() = default;
void Engine::start(const StartOptions &o) { p->start(o); }
void Engine::stop(bool rest) { p->stop(rest); }
void Engine::wake(const Console &c, bool remote) {
  if (!p->streaming)
    p->fatal = false;
  p->wake(c, remote);
}
bool Engine::active() const { return p->streaming; }
bool Engine::connected() const { return p->consoleConnected; }
std::string Engine::console_state(const Console *console) {
  auto observed = p->consoleMonitor.state(console ? *console : Console{});
  return console ? observed : "No console selected";
}
bool Engine::peer() const { return p->peer(); }
bool Engine::failed() const { return p->fatal; }
void Engine::preview(bool b) { p->previewEnabled = b; }
std::vector<uint8_t> Engine::preview_jpeg() {
  std::lock_guard<std::mutex> g(p->imageMutex);
  return p->previewImage;
}
std::string Engine::status() {
  std::lock_guard<std::mutex> g(p->messageMutex);
  return p->message;
}
std::string Engine::diagnostics() { return p->diagnostics(); }
} // namespace relay
