#include "app.hpp"
#include <algorithm>
#include <bcrypt.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <set>
#include <shlobj.h>
#include <sstream>
#include <stdexcept>
#include <wincrypt.h>

namespace relay {
std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                              (int)s.size(), nullptr, 0);
  if (!n)
    throw std::runtime_error("Invalid UTF-8 text");
  std::wstring r(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n);
  return r;
}
std::string utf8(const std::wstring &s) {
  if (s.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string r(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr,
                      nullptr);
  return r;
}
std::wstring data_dir() {
  PWSTR root = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                  nullptr, &root)))
    throw std::runtime_error("Cannot locate user application data");
  std::wstring p = std::wstring(root) + L"\\3DSRP Companion";
  CoTaskMemFree(root);
  if (!CreateDirectoryW(p.c_str(), nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS)
    throw std::runtime_error("Cannot create user application folder");
  return p;
}
std::string new_id() {
  uint8_t b[16];
  if (BCryptGenRandom(nullptr, b, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    throw std::runtime_error("Secure random generator unavailable");
  static const char hex[] = "0123456789abcdef";
  std::string r;
  for (auto c : b) {
    r += hex[c >> 4];
    r += hex[c & 15];
  }
  return r;
}
static void atomic_write(const std::wstring &path, const void *data, size_t n) {
  std::wstring tmp = path + L".tmp";
  HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot save user data");
  DWORD wrote = 0;
  bool ok = WriteFile(f, data, (DWORD)n, &wrote, nullptr) && wrote == n &&
            FlushFileBuffers(f);
  CloseHandle(f);
  if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmp.c_str());
    throw std::runtime_error("Cannot commit user data");
  }
}
static std::vector<uint8_t> read_file(const std::wstring &p) {
  HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) {
    if (GetLastError() == ERROR_FILE_NOT_FOUND)
      return {};
    throw std::runtime_error("Cannot read user data");
  }
  LARGE_INTEGER n{};
  if (!GetFileSizeEx(f, &n) || n.QuadPart > 1024 * 1024 || n.QuadPart < 0) {
    CloseHandle(f);
    throw std::runtime_error("Invalid user data size");
  }
  std::vector<uint8_t> r((size_t)n.QuadPart);
  DWORD got = 0;
  bool ok =
      ReadFile(f, r.data(), (DWORD)r.size(), &got, nullptr) && got == r.size();
  CloseHandle(f);
  if (!ok)
    throw std::runtime_error("Cannot read user data");
  return r;
}
void write_private(const std::wstring &path,
                   const std::vector<uint8_t> &bytes) {
  DATA_BLOB in{(DWORD)bytes.size(), const_cast<BYTE *>(bytes.data())}, out{};
  if (!CryptProtectData(&in, L"3DSRP Companion", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &out))
    throw std::runtime_error("Cannot protect user data");
  try {
    atomic_write(path, out.pbData, out.cbData);
  } catch (...) {
    LocalFree(out.pbData);
    throw;
  }
  LocalFree(out.pbData);
}
std::vector<uint8_t> read_private(const std::wstring &path) {
  auto b = read_file(path);
  if (b.empty())
    return {};
  DATA_BLOB in{(DWORD)b.size(), b.data()}, out{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
    throw std::runtime_error("Cannot decrypt saved data for this Windows user");
  std::vector<uint8_t> r(out.pbData, out.pbData + out.cbData);
  LocalFree(out.pbData);
  return r;
}
bool account_id(const std::string &text, std::array<uint8_t, 8> &out) {
  if (!text.empty() &&
      text.find_first_not_of("0123456789") == std::string::npos) {
    uint64_t n = 0;
    for (char c : text) {
      unsigned digit = (unsigned)(c - '0');
      if (n > (UINT64_MAX - digit) / 10)
        return false;
      n = n * 10 + digit;
    }
    for (int i = 0; i < 8; i++)
      out[i] = (uint8_t)(n >> (i * 8));
    return true;
  }
  if (text.size() != 12 || text.back() != '=')
    return false;
  DWORD n = 8;
  return CryptStringToBinaryA(text.c_str(), (DWORD)text.size(),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                              out.data(), &n, nullptr, nullptr) &&
         n == 8;
}
static void put(std::vector<uint8_t> &b, uint32_t n) {
  for (int i = 0; i < 4; i++)
    b.push_back((uint8_t)(n >> (24 - i * 8)));
}
static void put(std::vector<uint8_t> &b, const std::string &s) {
  put(b, (uint32_t)s.size());
  b.insert(b.end(), s.begin(), s.end());
}
struct Reader {
  const std::vector<uint8_t> &b;
  size_t at = 0;
  uint32_t num() {
    if (at + 4 > b.size())
      throw std::runtime_error("Corrupt saved settings");
    uint32_t n = 0;
    for (int i = 0; i < 4; i++)
      n = n * 256 + b[at++];
    return n;
  }
  std::string str() {
    auto n = num();
    if (n > 4096 || at + n > b.size())
      throw std::runtime_error("Corrupt saved settings");
    std::string s((char *)b.data() + at, n);
    at += n;
    return s;
  }
};
void Settings::save() const {
  std::vector<uint8_t> b;
  put(b, 0x57525031);
  for (int n : {height, bitrate, fps, image, quality, selected, (int)remote,
                (int)preview})
    put(b, n);
  put(b, (uint32_t)consoles.size());
  for (auto &c : consoles) {
    put(b, c.id);
    put(b, c.name);
    put(b, c.local);
    put(b, c.remote);
    put(b, c.account);
    put(b, (uint32_t)c.ps5);
  }
  write_private(data_dir() + L"\\settings.dat", b);
}
void Settings::load() {
  auto b = read_private(data_dir() + L"\\settings.dat");
  if (b.empty())
    return;
  Reader r{b};
  if (r.num() != 0x57525031)
    throw std::runtime_error("Unrecognized saved settings");
  height = (int)r.num();
  bitrate = (int)r.num();
  fps = (int)r.num();
  image = (int)r.num();
  quality = (int)r.num();
  selected = (int)r.num();
  remote = r.num() != 0;
  preview = r.num() != 0;
  auto n = r.num();
  if (n > 100)
    throw std::runtime_error("Invalid console count");
  for (unsigned i = 0; i < n; i++) {
    Console c;
    c.id = r.str();
    c.name = r.str();
    c.local = r.str();
    c.remote = r.str();
    c.account = r.str();
    c.ps5 = r.num() != 0;
    if (c.id.size() != 32 ||
        c.id.find_first_not_of("0123456789abcdef") != std::string::npos)
      throw std::runtime_error("Invalid console identifier");
    consoles.push_back(c);
  }
  if (height != 360 && height != 540 && height != 720 && height != 1080)
    height = 360;
  if (bitrate < 100 || bitrate > 50000)
    bitrate = 3000;
  if (fps != 30 && fps != 60)
    fps = 30;
  if (image < 0 || image > 2)
    image = 1;
  if (quality < 1 || quality > 100)
    quality = 70;
  if (selected < 0 || selected >= (int)consoles.size())
    selected = 0;
}
std::wstring addresses(bool localOnly) {
  ULONG n = 16384;
  std::vector<uint8_t> b(n);
  auto *a = (IP_ADAPTER_ADDRESSES *)b.data();
  ULONG code =
      GetAdaptersAddresses(AF_INET,
                           GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                               GAA_FLAG_SKIP_DNS_SERVER,
                           nullptr, a, &n);
  if (code == ERROR_BUFFER_OVERFLOW) {
    b.resize(n);
    a = (IP_ADAPTER_ADDRESSES *)b.data();
    code =
        GetAdaptersAddresses(AF_INET,
                             GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                 GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, a, &n);
  }
  if (code != NO_ERROR)
    return L"No network address available";
  std::wstring s, wifi, ethernet;
  for (auto *p = a; p; p = p->Next) {
    if (p->OperStatus != IfOperStatusUp ||
        p->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
      continue;
    for (auto *u = p->FirstUnicastAddress; u; u = u->Next) {
      auto *v = (sockaddr_in *)u->Address.lpSockaddr;
      if (v->sin_family != AF_INET)
        continue;
      wchar_t ip[64]{};
      InetNtopW(AF_INET, &v->sin_addr, ip, 64);
      if (p->IfType == IF_TYPE_IEEE80211 || p->IfType == IF_TYPE_ETHERNET_CSMACD) {
        auto &local = p->IfType == IF_TYPE_IEEE80211 ? wifi : ethernet;
        if (!local.empty()) local += L"\r\n";
        local += ip;
      }
      if (!s.empty())
        s += L"\r\n";
      s += ip;
      s += L"   ";
      s += p->FriendlyName;
    }
  }
  if (localOnly)
    return !wifi.empty() ? wifi : !ethernet.empty() ? ethernet : L"No Wi-Fi / LAN IP";
  return s.empty() ? L"Connect this PC to your local network" : s;
}
// Enumerate local Wi-Fi/Ethernet broadcasts and existing neighbor-cache
// entries. No subnet sweep, ARP-cache mutation, firewall mutation, or elevation
// is used.
DiscoveryTargets discovery_targets() {
  DiscoveryTargets result;
  ULONG bytes = 16384;
  std::vector<uint8_t> buffer(bytes);
  ULONG status = ERROR_BUFFER_OVERFLOW;
  IP_ADAPTER_ADDRESSES *adapters = nullptr;
  for (int attempt = 0; attempt < 3 && status == ERROR_BUFFER_OVERFLOW;
       ++attempt) {
    buffer.resize(bytes);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
    status =
        GetAdaptersAddresses(AF_INET,
                             GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                 GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, adapters, &bytes);
  }
  if (status != NO_ERROR) {
    result.error = status;
    return result;
  }
  struct Network {
    ULONG index;
    uint32_t address, mask;
  };
  std::vector<Network> networks;
  std::set<uint32_t> own, broadcasts, neighbors;
  for (auto *adapter = adapters; adapter; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp ||
        (adapter->IfType != IF_TYPE_ETHERNET_CSMACD &&
         adapter->IfType != IF_TYPE_IEEE80211))
      continue;
    for (auto *unicast = adapter->FirstUnicastAddress; unicast;
         unicast = unicast->Next) {
      auto *address =
          reinterpret_cast<sockaddr_in *>(unicast->Address.lpSockaddr);
      if (!address || address->sin_family != AF_INET)
        continue;
      unsigned prefix = unicast->OnLinkPrefixLength;
      uint32_t host = ntohl(address->sin_addr.s_addr);
      if (!host || (host >> 24) == 127 || (host >> 28) >= 14 || !prefix ||
          prefix > 30)
        continue;
      uint32_t mask = UINT32_MAX << (32 - prefix);
      own.insert(host);
      networks.push_back({adapter->IfIndex, host, mask});
      broadcasts.insert((host & mask) | ~mask);
    }
  }
  for (auto broadcast : broadcasts)
    result.broadcasts.push_back(htonl(broadcast));
  MIB_IPNET_TABLE2 *table = nullptr;
  status = GetIpNetTable2(AF_INET, &table);
  if (status != NO_ERROR) {
    result.error = status;
    return result;
  }
  for (ULONG index = 0; index < table->NumEntries; ++index) {
    const auto &row = table->Table[index];
    if (row.Address.si_family != AF_INET || row.State == NlnsUnreachable ||
        row.State == NlnsIncomplete || row.PhysicalAddressLength == 0)
      continue;
    uint32_t host = ntohl(row.Address.Ipv4.sin_addr.s_addr);
    if (!host || own.count(host) || (host >> 24) == 127 || (host >> 28) >= 14)
      continue;
    for (const auto &network : networks) {
      uint32_t subnet = network.address & network.mask;
      if (row.InterfaceIndex == network.index &&
          (host & network.mask) == subnet && host != subnet &&
          host != (subnet | ~network.mask)) {
        neighbors.insert(host);
        break;
      }
    }
    if (neighbors.size() >= 128)
      break;
  }
  FreeMibTable(table);
  for (auto neighbor : neighbors)
    result.neighbors.push_back(htonl(neighbor));
  return result;
}
} // namespace relay
