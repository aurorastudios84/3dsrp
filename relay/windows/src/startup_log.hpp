#pragma once
#include <cstdio>
#include <cwchar>
// Best-effort diagnostics only: preserve Win32 errors and never alter recovery.
namespace startup_log {
inline HANDLE file = INVALID_HANDLE_VALUE;
inline wchar_t logPath[MAX_PATH]{};
inline void write(const char *message) noexcept {
  DWORD saved = GetLastError();
  if (file != INVALID_HANDLE_VALUE) {
    char line[2048];
    SYSTEMTIME now{};
    GetSystemTime(&now);
    int n = std::snprintf(line, sizeof(line),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ thread=%lu %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
        now.wSecond, now.wMilliseconds, GetCurrentThreadId(), message);
    if (n > 0) {
      DWORD written;
      WriteFile(file, line, (DWORD)std::min(n, (int)sizeof(line)-1), &written, nullptr);
      FlushFileBuffers(file);
    }
  }
  SetLastError(saved);
}
inline LONG WINAPI crash(EXCEPTION_POINTERS *p) noexcept {
  char line[512];
  if (p && p->ExceptionRecord) {
    auto *e = p->ExceptionRecord;
    MEMORY_BASIC_INFORMATION memory{};
    VirtualQuery(e->ExceptionAddress, &memory, sizeof(memory));
    std::snprintf(line, sizeof(line),
        "UNHANDLED exception=0x%08lx address=%p module_base=%p offset=0x%llx",
        e->ExceptionCode, e->ExceptionAddress, memory.AllocationBase,
        (unsigned long long)((uintptr_t)e->ExceptionAddress-(uintptr_t)memory.AllocationBase));
    write(line);
    if (e->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && e->NumberParameters >= 2) {
      std::snprintf(line, sizeof(line), "Access violation operation=%llu target=0x%llx",
          (unsigned long long)e->ExceptionInformation[0],
          (unsigned long long)e->ExceptionInformation[1]);
      write(line);
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
inline bool open_directory(const wchar_t *directory) noexcept {
  CreateDirectoryW(directory, nullptr);
  wchar_t path[MAX_PATH];
  SYSTEMTIME now{};
  GetSystemTime(&now);
  int length = std::swprintf(path, MAX_PATH,
      L"%ls\\app-%04u%02u%02u-%02u%02u%02u-%03u-%lu.log", directory,
      now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
      now.wSecond, now.wMilliseconds, GetCurrentProcessId());
  if (length <= 0 || length >= MAX_PATH)
    return false;
  file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE)
    std::wcscpy(logPath, path);
  return file != INVALID_HANDLE_VALUE;
}
inline void start() noexcept {
  DWORD saved = GetLastError();
  wchar_t executable[MAX_PATH], directory[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, executable, MAX_PATH);
  if (n && n < MAX_PATH) {
    wchar_t *separator = std::wcsrchr(executable, L'\\');
    if (separator) {
      *separator = L'\0';
      int length = std::swprintf(directory, MAX_PATH, L"%ls\\logs", executable);
      if (length > 0 && length < MAX_PATH)
        open_directory(directory);
    }
  }
  if (file == INVALID_HANDLE_VALUE) {
    wchar_t temp[MAX_PATH];
    n = GetTempPathW(MAX_PATH, temp);
    if (n && n < MAX_PATH) {
      int length = std::swprintf(directory, MAX_PATH, L"%ls3DSRP-Companion-logs", temp);
      if (length > 0 && length < MAX_PATH)
        open_directory(directory);
    }
  }
  SetUnhandledExceptionFilter(crash);
  write("Diagnostic build: background tray + local logs; entered wWinMain");
  SetLastError(saved);
}
}
