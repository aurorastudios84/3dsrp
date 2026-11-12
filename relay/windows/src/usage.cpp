#include "app.hpp"
#include "wire.h"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <winhttp.h>
namespace relay {
namespace {
struct HttpHandle {
  HINTERNET h = nullptr;
  ~HttpHandle() {
    if (h)
      WinHttpCloseHandle(h);
  }
  operator HINTERNET() const { return h; }
};
struct PostResult {
  int status = 0;
  DWORD error = 0;
};
PostResult post(const std::string &body) {
  HttpHandle session{
      WinHttpOpen(L"3DSRP-Usage", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.h)
    return {0, GetLastError()};
  WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000);
  HttpHandle connection{
      WinHttpConnect(session, L"3dsrp-usage.aurorastudios84.workers.dev",
                     INTERNET_DEFAULT_HTTPS_PORT, 0)};
  if (!connection.h)
    return {0, GetLastError()};
  HttpHandle request{WinHttpOpenRequest(
      connection, L"POST", L"/v3/session", nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
  if (!request.h)
    return {0, GetLastError()};
  DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                   sizeof(policy));
  if (!WinHttpSendRequest(request, L"Content-Type: application/json\r\n",
                          (DWORD)-1, (void *)body.data(), (DWORD)body.size(),
                          (DWORD)body.size(), 0) ||
      !WinHttpReceiveResponse(request, nullptr))
    return {0, GetLastError()};
  DWORD status = 0, n = sizeof(status);
  if (!WinHttpQueryHeaders(
          request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status, &n, WINHTTP_NO_HEADER_INDEX))
    return {0, GetLastError()};
  return {(int)status, 0};
}
bool uuid(const std::string &s) {
  if (s.size() != 36)
    return false;
  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-')
        return false;
    } else if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
      return false;
  }
  return s[14] == '4' &&
         (s[19] == '8' || s[19] == '9' || s[19] == 'a' || s[19] == 'b');
}
bool date(const std::string &s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-')
    return false;
  for (int i = 0; i < 10; i++)
    if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9'))
      return false;
  int y = atoi(s.substr(0, 4).c_str()), m = atoi(s.substr(5, 2).c_str()),
      d = atoi(s.substr(8, 2).c_str());
  int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (y < 1 || m < 1 || m > 12)
    return false;
  if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))
    days[1] = 29;
  return d >= 1 && d <= days[m - 1];
}
} // namespace
struct Usage::Impl {
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<UsageJob> queue;
  std::set<uint32_t> cancelled;
  std::set<uint32_t> pending;
  uint32_t run = 0;
  uint64_t epoch = 0, received = 0, rejected = 0, confirmed = 0, failed = 0;
  int http = 0;
  bool stopping = false;
  std::string error;
  std::function<void(uint32_t, uint32_t, int)> reply;
  std::thread worker;
  Impl(std::function<void(uint32_t, uint32_t, int)> r) : reply(std::move(r)) {
    try {
      auto b = read_private(data_dir() + L"\\usage-diagnostics.dat");
      if (b.size() == 40) {
        received = ((uint64_t)r32(b.data()) << 32) | r32(b.data() + 4);
        rejected = ((uint64_t)r32(b.data() + 8) << 32) | r32(b.data() + 12);
        confirmed = ((uint64_t)r32(b.data() + 16) << 32) | r32(b.data() + 20);
        failed = ((uint64_t)r32(b.data() + 24) << 32) | r32(b.data() + 28);
        http = (int)r32(b.data() + 32);
      }
    } catch (...) {
    }
    worker = std::thread([this] { loop(); });
  }
  void save() {
    std::vector<uint8_t> b(40);
    uint64_t values[] = {received, rejected, confirmed, failed};
    for (int i = 0; i < 4; i++) {
      w32(b.data() + 8 * i, (uint32_t)(values[i] >> 32));
      w32(b.data() + 8 * i + 4, (uint32_t)values[i]);
    }
    w32(b.data() + 32, (uint32_t)http);
    try {
      write_private(data_dir() + L"\\usage-diagnostics.dat", b);
    } catch (...) {
    }
  }
  ~Impl() {
    {
      std::lock_guard<std::mutex> g(mutex);
      stopping = true;
      queue.clear();
    }
    cv.notify_all();
    if (worker.joinable())
      worker.join();
    save();
  }
  void loop() {
    // Android's delivery worker uses minimum priority. Keep HTTPS off the
    // media/network threads while allowing saved reports during gameplay.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    for (;;) {
      UsageJob job;
      {
        std::unique_lock<std::mutex> g(mutex);
        cv.wait(g, [&] { return stopping || !queue.empty(); });
        if (stopping)
          return;
        job = queue.front();
        queue.pop_front();
        if (job.epoch != epoch || cancelled.count(job.serial)) {
          pending.erase(job.serial);
          continue;
        }
      }
      auto result = post(job.body);
      int code = result.status;
      {
        std::lock_guard<std::mutex> g(mutex);
        if (stopping)
          return;
        if (job.epoch != epoch)
          continue;
        pending.erase(job.serial);
        if (cancelled.erase(job.serial) || job.run != run)
          continue;
        http = code;
        error.clear();
        if (!code) {
          if (result.error == ERROR_WINHTTP_NAME_NOT_RESOLVED)
            error = "DNS lookup failed";
          else if (result.error == ERROR_WINHTTP_TIMEOUT)
            error = "HTTPS request timed out";
          else if (result.error == ERROR_WINHTTP_SECURE_FAILURE)
            error = "TLS validation failed";
          else if (result.error == ERROR_WINHTTP_CANNOT_CONNECT)
            error = "HTTPS connection failed";
          else
            error = "HTTPS transport error " + std::to_string(result.error);
        }
        if (code == 204)
          confirmed++;
        else
          failed++;
        save();
        reply(job.run, job.serial, code == 204 ? 1 : code == 410 ? 2 : -1);
      }
    }
  }
};
Usage::Usage(std::function<void(uint32_t, uint32_t, int)> r)
    : p(new Impl(std::move(r))) {}
Usage::~Usage() = default;
void Usage::reset(uint32_t r) {
  std::lock_guard<std::mutex> g(p->mutex);
  p->run = r;
  p->epoch++;
  p->queue.clear();
  p->pending.clear();
  p->cancelled.clear();
}
void Usage::report(const uint8_t *b, size_t n) {
  std::lock_guard<std::mutex> g(p->mutex);
  p->received++;
  bool valid = relay_valid(b, n) && n == 128 && r32(b + 20) == 96 &&
               r16(b + 16) == 0 && r16(b + 18) == 1 && r32(b + 32) == p->run &&
               b[118] == 1 && b[119] == 0 && r32(b + 120) <= 86400 &&
               r32(b + 124) <= 0xffffff;
  std::string id, session, day;
  if (valid) {
    id = std::string((char *)b + 36, 36);
    session = std::string((char *)b + 72, 36);
    day = std::string((char *)b + 108, 10);
    valid = uuid(id) && uuid(session) && date(day);
  }
  if (!valid) {
    p->rejected++;
    p->error = "Invalid handheld report";
    return;
  }
  uint32_t serial = r32(b + 28);
  if (!serial || p->pending.count(serial))
    return;
  if (p->queue.size() >= 8) {
    p->reply(p->run, serial, -1);
    return;
  }
  p->cancelled.erase(serial);
  p->pending.insert(serial);
  std::string body = "{\"app\":\"original_3ds\",\"installation_id\":\"" + id +
                     "\",\"session_id\":\"" + session + "\",\"day\":\"" + day +
                     "\",\"seconds\":" + std::to_string(r32(b + 120)) + "}";
  p->queue.push_back({p->run, serial, p->epoch, body});
  p->cv.notify_all();
}
void Usage::cancel(const uint8_t *b, size_t n) {
  if (n != 36 || r32(b + 20) != 4 || r16(b + 16) != 0 || r16(b + 18) != 1)
    return;
  std::lock_guard<std::mutex> g(p->mutex);
  if (r32(b + 32) != p->run)
    return;
  uint32_t serial = r32(b + 28);
  if (p->pending.count(serial))
    p->cancelled.insert(serial);
  p->queue.erase(std::remove_if(p->queue.begin(), p->queue.end(),
                                [&](auto &j) { return j.serial == serial; }),
                 p->queue.end());
  p->pending.erase(serial);
}
std::string Usage::json() {
  std::lock_guard<std::mutex> g(p->mutex);
  std::ostringstream s;
  s << "\"usage\":{\"received\":" << p->received
    << ",\"rejected\":" << p->rejected << ",\"confirmed\":" << p->confirmed
    << ",\"http\":" << p->http << ",\"error\":\"" << p->error << "\"}";
  return s.str();
}
} // namespace relay
