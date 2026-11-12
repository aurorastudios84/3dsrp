#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace relay {
std::wstring wide(const std::string &s);
std::string utf8(const std::wstring &s);
std::wstring data_dir();
std::string new_id();
void write_private(const std::wstring &path, const std::vector<uint8_t> &bytes);
std::vector<uint8_t> read_private(const std::wstring &path);
bool account_id(const std::string &text, std::array<uint8_t, 8> &out);
std::wstring addresses(bool localOnly = false);
struct DiscoveryTargets {
  std::vector<uint32_t> broadcasts, neighbors;
  uint32_t error = 0;
};
DiscoveryTargets discovery_targets();
struct Console {
  std::string id, name, local, remote, account;
  bool ps5 = true;
};
bool has_pairing(const Console &console);
struct Settings {
  int height = 360, bitrate = 3000, fps = 30, image = 1, quality = 70,
      selected = 0;
  bool remote = false, preview = true;
  std::vector<Console> consoles;
  void load();
  void save() const;
};
struct StartOptions {
  Console console;
  std::string pin;
  int height = 360, bitrate = 3000, fps = 30, image = 1, quality = 70;
  bool remote = false, pattern = false;
};
class Engine {
  struct Impl;
  std::unique_ptr<Impl> p;

public:
  Engine();
  ~Engine();
  void start(const StartOptions &options);
  void stop(bool rest = false);
  void wake(const Console &console, bool remote);
  bool active() const;
  bool connected() const;
  std::string console_state(const Console *console);
  bool peer() const;
  bool failed() const;
  void preview(bool enabled);
  std::vector<uint8_t> preview_jpeg();
  std::string status();
  std::string diagnostics();
};
struct UsageJob {
  uint32_t run, serial;
  uint64_t epoch;
  std::string body;
};
class Usage {
  struct Impl;
  std::unique_ptr<Impl> p;

public:
  Usage(std::function<void(uint32_t, uint32_t, int)> reply);
  ~Usage();
  void reset(uint32_t run);
  void report(const uint8_t *data, size_t n);
  void cancel(const uint8_t *data, size_t n);
  std::string json();
};
} // namespace relay
