#include "app.hpp"
#include <algorithm>
#include <atomic>
#include <commctrl.h>
#include <memory>
#include <mmsystem.h>
#include <objidl.h>
#include <olectl.h>
#include <shellapi.h>
#include <sstream>
#include <stdexcept>
#include <thread>
#include "startup_log.hpp"
using namespace relay;
namespace {
constexpr UINT WM_WORK_DONE = WM_APP + 1, WM_TRAY = WM_APP + 2,
               WM_TRAY_READY = WM_APP + 3;
enum {
  CONSOLE = 100,
  ADD,
  EDIT,
  REMOVE,
  CONNECT,
  DISCONNECT,
  REST,
  WAKE,
  PATTERN,
  ROUTE,
  RESOLUTION,
  FPS,
  BITRATE,
  IMAGE,
  QUALITY,
  PREVIEW,
  SAVE,
  DEFAULTS,
  HELP,
  COPY_DIAG,
  OPEN_FOLDER,
  TRAY_OPEN,
  TRAY_EXIT,
  VIEW_SETTINGS,
  VIEW_DIAGNOSTICS,
  MANAGE_CONSOLES
};
HINSTANCE instance;
HFONT font, titleFont;
HWND window, consoleBox, routeBox, resBox, fpsBox, bitrateBox, imageBox,
    qualityBox, previewBox, statusLabel, linksLabel, addressesBox,
    diagnosticBox, previewWindow;
enum class View { Home, Settings, Diagnostics };
View currentView = View::Home;
std::array<std::vector<HWND>, 3> viewWidgets;
std::vector<HWND> *buildingView = nullptr;
HWND navigationButton{};
Settings settings;
std::unique_ptr<Engine> engine;
std::thread worker;
std::atomic<bool> busy{false};
NOTIFYICONDATAW tray{};
UINT taskbarCreated = 0;
bool trayAvailable = false;
bool closing = false, closeOffered = false, disconnectPromptOpen = false;
std::string lastStatus;
IPicture *picture = nullptr;
std::vector<uint8_t> lastPreview;
int scrollX = 0, scrollY = 0;
constexpr int contentWidth = 960, contentHeight = 832;

void resize_scrollbars(HWND w) {
  RECT client{};
  GetClientRect(w, &client);
  const int previousX = scrollX, previousY = scrollY;
  SCROLLINFO info{};
  info.cbSize = sizeof(info);
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
  info.nMax = contentWidth - 1;
  info.nPage = std::max(0L, client.right);
  info.nPos = scrollX;
  SetScrollInfo(w, SB_HORZ, &info, TRUE);
  scrollX = GetScrollPos(w, SB_HORZ);
  // The horizontal scrollbar may have changed the available height.
  GetClientRect(w, &client);
  info.nMax = contentHeight - 1;
  info.nPage = std::max(0L, client.bottom);
  info.nPos = scrollY;
  SetScrollInfo(w, SB_VERT, &info, TRUE);
  scrollY = GetScrollPos(w, SB_VERT);
  ScrollWindowEx(w, previousX - scrollX, previousY - scrollY, nullptr, nullptr,
                 nullptr, nullptr,
                 SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
}
void scroll_content(HWND w, int bar, int command, int wheel = 0) {
  SCROLLINFO info{};
  info.cbSize = sizeof(info);
  info.fMask = SIF_ALL;
  GetScrollInfo(w, bar, &info);
  int next = info.nPos;
  switch (command) {
  case SB_LINEUP:
    next -= 32;
    break;
  case SB_LINEDOWN:
    next += 32;
    break;
  case SB_PAGEUP:
    next -= (int)info.nPage;
    break;
  case SB_PAGEDOWN:
    next += (int)info.nPage;
    break;
  case SB_TOP:
    next = info.nMin;
    break;
  case SB_BOTTOM:
    next = info.nMax;
    break;
  case SB_THUMBTRACK:
  case SB_THUMBPOSITION:
    next = info.nTrackPos;
    break;
  default:
    next += wheel;
    break;
  }
  next = std::clamp(next, info.nMin,
                    std::max(info.nMin, info.nMax - (int)info.nPage + 1));
  SetScrollPos(w, bar, next, TRUE);
  if (bar == SB_VERT) {
    ScrollWindowEx(w, 0, scrollY - next, nullptr, nullptr, nullptr, nullptr,
                   SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    scrollY = next;
  } else {
    ScrollWindowEx(w, scrollX - next, 0, nullptr, nullptr, nullptr, nullptr,
                   SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    scrollX = next;
  }
}
HRESULT CALLBACK close_choice_callback(HWND dialog, UINT notification,
                                       WPARAM elapsed, LPARAM, LONG_PTR) {
  if (notification == TDN_TIMER) {
    if (engine->peer() || !engine->active() || busy)
      SendMessageW(dialog, TDM_CLICK_BUTTON, IDCANCEL, 0);
    else if (elapsed >= 60000)
      SendMessageW(dialog, TDM_CLICK_BUTTON, IDNO, 0);
  }
  return S_OK;
}
int close_choice() {
  TASKDIALOGCONFIG config{};
  config.cbSize = sizeof(config);
  config.hwndParent = window;
  config.dwFlags = TDF_CALLBACK_TIMER | TDF_ALLOW_DIALOG_CANCELLATION;
  config.dwCommonButtons =
      TDCBF_YES_BUTTON | TDCBF_NO_BUTTON | TDCBF_CANCEL_BUTTON;
  config.pszWindowTitle = L"3DS stream closed";
  config.pszMainInstruction = L"Put the PlayStation into Rest mode?";
  config.pszContent =
      L"Yes: Request Rest mode\nNo: Disconnect and leave it on\n"
      L"Cancel: Keep the relay running\n\n"
      L"After 60 seconds, the relay disconnects and leaves the console on. "
      L"Reconnecting the 3DS closes this prompt.";
  config.nDefaultButton = IDNO;
  config.pfCallback = close_choice_callback;
  int result = IDCANCEL;
  if (FAILED(TaskDialogIndirect(&config, &result, nullptr, nullptr)))
    return IDNO; // Failure to show a choice never requests console sleep.
  return result;
}
HWND control(const wchar_t *cls, const wchar_t *text, int id, int x, int y,
             int w, int h, DWORD style = 0, HWND parent = nullptr) {
  if (!parent)
    parent = window;
  HWND c = CreateWindowExW((wcscmp(cls, L"EDIT") == 0) ? WS_EX_CLIENTEDGE : 0,
                           cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                           parent, (HMENU)(INT_PTR)id, instance, nullptr);
  if (!c) {
    DWORD saved = GetLastError();
    char detail[128];
    std::snprintf(detail, sizeof(detail), "Control creation failed id=%d win32_error=%lu", id, saved);
    startup_log::write(detail);
    SetLastError(saved);
  }
  if (buildingView && parent == window)
    buildingView->push_back(c);
  SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
  return c;
}
HWND label(const wchar_t *text, int x, int y, int w, int h = 22,
           HWND parent = nullptr) {
  return control(L"STATIC", text, 0, x, y, w, h, 0, parent);
}
HWND button(const wchar_t *text, int id, int x, int y, int w = 130,
            HWND parent = nullptr) {
  return control(L"BUTTON", text, id, x, y, w, 30, WS_TABSTOP | BS_PUSHBUTTON,
                 parent);
}
HWND combo(int id, int x, int y, int w, const std::vector<std::wstring> &values,
           HWND parent = nullptr) {
  HWND c = control(L"COMBOBOX", L"", id, x, y, w, 220,
                   WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, parent);
  for (auto &s : values)
    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)s.c_str());
  SendMessageW(c, CB_SETCURSEL, 0, 0);
  return c;
}
void show_view(View view) {
  scroll_content(window, SB_VERT, SB_TOP);
  scroll_content(window, SB_HORZ, SB_TOP);
  currentView = view;
  for (size_t i = 0; i < viewWidgets.size(); ++i)
    for (HWND widget : viewWidgets[i])
      ShowWindow(widget, i == (size_t)view ? SW_SHOW : SW_HIDE);
  SetWindowTextW(navigationButton, view == View::Home ? L"Settings" : L"Back");
  SetFocus(view == View::Home ? consoleBox : navigationButton);
  InvalidateRect(window, nullptr, TRUE);
}
void console_menu() {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  const bool idle = !engine->active() && !busy;
  const bool selectedConsole = SendMessageW(consoleBox, CB_GETCURSEL, 0, 0) != CB_ERR;
  UINT flags = MF_STRING | (idle && selectedConsole ? MF_ENABLED : MF_GRAYED);
  AppendMenuW(menu, flags, EDIT, L"Edit console / pair again");
  AppendMenuW(menu, flags, REMOVE, L"Remove console...");
  RECT anchor{};
  GetWindowRect(GetDlgItem(window, MANAGE_CONSOLES), &anchor);
  int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                              anchor.left, anchor.bottom, 0, window, nullptr);
  DestroyMenu(menu);
  if (command) SendMessageW(window, WM_COMMAND, command, 0);
}
int selection(HWND c) { return (int)SendMessageW(c, CB_GETCURSEL, 0, 0); }
std::wstring text(HWND c) {
  int n = GetWindowTextLengthW(c);
  std::wstring s(n + 1, 0);
  GetWindowTextW(c, s.data(), n + 1);
  s.resize(n);
  auto first = s.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos)
    return {};
  return s.substr(first, s.find_last_not_of(L" \t\r\n") - first + 1);
}
void error(const std::string &s) {
  MessageBoxW(window, wide(s).c_str(), L"3DSRP Companion",
              MB_OK | MB_ICONERROR);
}
Console *selected() {
  int n = selection(consoleBox);
  return n >= 0 && n < (int)settings.consoles.size() ? &settings.consoles[n]
                                                     : nullptr;
}
void refresh_consoles() {
  SendMessageW(consoleBox, CB_RESETCONTENT, 0, 0);
  for (auto &c : settings.consoles) {
    auto name = wide(c.name + (c.ps5 ? " (PS5)" : " (PS4)"));
    SendMessageW(consoleBox, CB_ADDSTRING, 0, (LPARAM)name.c_str());
  }
  if (!settings.consoles.empty())
    SendMessageW(
        consoleBox, CB_SETCURSEL,
        std::clamp(settings.selected, 0, (int)settings.consoles.size() - 1), 0);
}
void display_settings() {
  SendMessageW(routeBox, CB_SETCURSEL, settings.remote ? 1 : 0, 0);
  SendMessageW(resBox, CB_SETCURSEL,
               settings.height == 360   ? 0
               : settings.height == 540 ? 1
               : settings.height == 720 ? 2
                                        : 3,
               0);
  SendMessageW(fpsBox, CB_SETCURSEL, settings.fps == 60 ? 1 : 0, 0);
  SetWindowTextW(bitrateBox, std::to_wstring(settings.bitrate).c_str());
  SendMessageW(imageBox, CB_SETCURSEL, settings.image, 0);
  SetWindowTextW(qualityBox, std::to_wstring(settings.quality).c_str());
  SendMessageW(previewBox, BM_SETCHECK,
               settings.preview ? BST_CHECKED : BST_UNCHECKED, 0);
}
int integer(HWND c, int low, int high, const char *name) {
  auto s = text(c);
  if (s.empty() || s.find_first_not_of(L"0123456789") != std::wstring::npos)
    throw std::runtime_error(std::string("Enter a whole-number ") + name);
  unsigned long n = 0;
  try {
    n = std::stoul(s);
  } catch (...) {
    throw std::runtime_error(std::string("Invalid ") + name);
  }
  if (n < (unsigned)low || n > (unsigned)high)
    throw std::runtime_error(std::string(name) + " must be " +
                             std::to_string(low) + " to " +
                             std::to_string(high));
  return (int)n;
}
void save_settings() {
  int heights[] = {360, 540, 720, 1080};
  settings.height = heights[std::clamp(selection(resBox), 0, 3)];
  settings.fps = selection(fpsBox) == 1 ? 60 : 30;
  settings.bitrate = integer(bitrateBox, 100, 50000, "bitrate");
  settings.quality = integer(qualityBox, 1, 100, "JPEG quality");
  settings.image = std::clamp(selection(imageBox), 0, 2);
  settings.remote = selection(routeBox) == 1;
  settings.selected = std::max(0, selection(consoleBox));
  settings.preview = SendMessageW(previewBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
  settings.save();
}
void async(std::function<void()> action) {
  if (busy)
    return;
  if (worker.joinable())
    worker.join();
  busy = true;
  worker = std::thread([action] {
    std::string failure;
    try {
      action();
    } catch (const std::exception &e) {
    startup_log::write(e.what());
      failure = e.what();
    }
    auto *result = new std::string(std::move(failure));
    if (!PostMessageW(window, WM_WORK_DONE, 0, (LPARAM)result))
      delete result;
  });
}
void copy(const std::wstring &s) {
  if (!OpenClipboard(window))
    return;
  EmptyClipboard();
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
  if (memory) {
    void *p = GlobalLock(memory);
    if (p) {
      memcpy(p, s.c_str(), (s.size() + 1) * sizeof(wchar_t));
      GlobalUnlock(memory);
      if (!SetClipboardData(CF_UNICODETEXT, memory))
        GlobalFree(memory);
    } else
      GlobalFree(memory);
  }
  CloseClipboard();
}
enum EditorControlId {
  EDITOR_NAME = 201,
  EDITOR_TYPE,
  EDITOR_LOCAL,
  EDITOR_REMOTE,
  EDITOR_ACCOUNT,
  EDITOR_PIN,
  EDITOR_REPAIR
};
struct Editor {
  HWND w{}, name{}, local{}, remote{}, account{}, pin{}, type{};
  Console console;
  HWND repair{};
  bool paired = false;
  bool accepted = false;
  std::string code;
};
LRESULT CALLBACK editor_proc(HWND w, UINT message, WPARAM wp, LPARAM lp) {
  auto *e = (Editor *)GetWindowLongPtrW(w, GWLP_USERDATA);
  if (message == WM_NCCREATE) {
    e = (Editor *)((CREATESTRUCTW *)lp)->lpCreateParams;
    SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)e);
    e->w = w;
  }
  if (!e)
    return DefWindowProcW(w, message, wp, lp);
  switch (message) {
  case WM_CREATE: {
    HWND heading = label(L"Your PlayStation", 24, 18, 455, 34, w);
    SendMessageW(heading, WM_SETFONT, (WPARAM)titleFont, TRUE);
    label(L"Console name", 24, 68, 288, 22, w);
    e->name = control(L"EDIT", wide(e->console.name).c_str(), EDITOR_NAME,
                      24, 94, 288, 28, WS_TABSTOP | ES_AUTOHSCROLL, w);
    label(L"Console type", 336, 68, 144, 22, w);
    e->type = combo(EDITOR_TYPE, 336, 94, 144, {L"PS5", L"PS4"}, w);
    SendMessageW(e->type, CB_SETCURSEL, e->console.ps5 ? 0 : 1, 0);
    label(L"Local IP address or hostname", 24, 138, 456, 22, w);
    e->local = control(L"EDIT", wide(e->console.local).c_str(), EDITOR_LOCAL,
                       24, 164, 456, 28, WS_TABSTOP | ES_AUTOHSCROLL, w);
    label(L"Internet address (optional)", 24, 208, 456, 22, w);
    e->remote = control(L"EDIT", wide(e->console.remote).c_str(), EDITOR_REMOTE,
                        24, 234, 456, 28, WS_TABSTOP | ES_AUTOHSCROLL, w);
    label(L"PAIRING", 24, 282, 456, 22, w);
    label(L"PSN Account ID (Base64 or numeric)", 24, 312, 456, 22, w);
    e->account = control(L"EDIT", wide(e->console.account).c_str(), EDITOR_ACCOUNT,
                         24, 338, 456, 28, WS_TABSTOP | ES_AUTOHSCROLL, w);
    label(L"8-digit Link Device PIN", 24, 384, 456, 22, w);
    e->pin = control(L"EDIT", L"", EDITOR_PIN, 24, 410, 208, 28,
                     WS_TABSTOP | ES_NUMBER, w);
    SendMessageW(e->pin, EM_SETLIMITTEXT, 8, 0);
    SendMessageW(e->pin, EM_SETREADONLY, e->paired, 0);
    e->repair = control(L"BUTTON", L"Pair again", EDITOR_REPAIR, 256, 410, 224, 28,
                        WS_TABSTOP | BS_AUTOCHECKBOX, w);
    ShowWindow(e->repair, e->paired ? SW_SHOW : SW_HIDE);
    label(e->paired
              ? L"Saved pairing is ready. Select Pair again only if you need to "
                L"enter a new PIN. Save to keep your changes."
              : L"Get a PIN from Remote Play settings on your console. "
                L"Enter it here, save, then choose Connect on your home network.",
          24, 456, 456, 54, w);
    button(L"Save", IDOK, 200, 532, 130, w);
    button(L"Cancel", IDCANCEL, 350, 532, 130, w);
    return 0;
  }
  case WM_COMMAND:
    // Edit/combo notifications are not Save/Cancel commands. Keyboard
    // dialog commands also use notification code zero (BN_CLICKED).
    if (HIWORD(wp) != BN_CLICKED)
      break;
    if (LOWORD(wp) == EDITOR_REPAIR) {
      bool repair = SendMessageW(e->repair, BM_GETCHECK, 0, 0) == BST_CHECKED;
      SendMessageW(e->pin, EM_SETREADONLY, e->paired && !repair, 0);
      if (!repair) SetWindowTextW(e->pin, L"");
      else SetFocus(e->pin);
      return 0;
    }
    if (LOWORD(wp) == IDCANCEL) {
      DestroyWindow(w);
      return 0;
    }
    if (LOWORD(wp) == IDOK) {
      try {
        Console c = e->console;
        c.name = utf8(text(e->name));
        c.local = utf8(text(e->local));
        c.remote = utf8(text(e->remote));
        c.account = utf8(text(e->account));
        c.ps5 = selection(e->type) == 0;
        bool repair = SendMessageW(e->repair, BM_GETCHECK, 0, 0) == BST_CHECKED;
        auto pin = e->paired && !repair ? std::string{} : utf8(text(e->pin));
        std::array<uint8_t, 8> a{};
        if (c.name.empty() || c.local.empty())
          throw std::runtime_error("Enter a name and local console address");
        if (!account_id(c.account, a))
          throw std::runtime_error(
              "Enter a valid Base64 or unsigned 64-bit numeric PSN Account ID");
        std::array<uint8_t, 8> previousAccount{};
        if (e->paired && !repair &&
            (!account_id(e->console.account, previousAccount) ||
             previousAccount != a || c.ps5 != e->console.ps5))
          throw std::runtime_error("Select Pair again and enter a fresh PIN when changing the console type or PSN account");
        if (e->paired && repair && pin.empty())
          throw std::runtime_error("Enter a fresh eight-digit PIN to pair again");
        if (!pin.empty() &&
            (pin.size() != 8 ||
             pin.find_first_not_of("0123456789") != std::string::npos))
          throw std::runtime_error("PIN must contain exactly eight digits");
        e->console = c;
        e->code = pin;
        e->accepted = true;
        DestroyWindow(w);
      } catch (const std::exception &ex) {
        MessageBoxW(w, wide(ex.what()).c_str(), L"Console setup",
                    MB_OK | MB_ICONERROR);
      }
      return 0;
    }
    break;
  case WM_CLOSE:
    DestroyWindow(w);
    return 0;
  }
  return DefWindowProcW(w, message, wp, lp);
}
std::string pendingPin, pinConsole;
void edit_console(bool add) {
  if (engine->active() || busy)
    return;
  Editor editor;
  if (add) {
    editor.console.id = new_id();
    editor.console.name = "My PlayStation";
  } else {
    auto *c = selected();
    if (!c)
      return;
    editor.console = *c;
    editor.paired = has_pairing(editor.console);
  }
  HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"3DSRPConsoleEditor",
                           add ? L"Set up a console" : L"Edit console",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                           CW_USEDEFAULT, CW_USEDEFAULT, 525, 620, window,
                           nullptr, instance, &editor);
  if (!w)
    throw std::runtime_error("Cannot open console setup");
  EnableWindow(window, FALSE);
  ShowWindow(w, SW_SHOW);
  MSG msg;
  while (IsWindow(w) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(w, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  EnableWindow(window, TRUE);
  SetActiveWindow(window);
  if (editor.accepted) {
    if (add) {
      settings.consoles.push_back(editor.console);
      settings.selected = (int)settings.consoles.size() - 1;
    } else {
      int index = selection(consoleBox);
      settings.consoles[index] = editor.console;
      settings.selected = index;
    }
    pendingPin = editor.code;
    pinConsole = editor.console.id;
    settings.save();
    refresh_consoles();
  }
}
LRESULT CALLBACK preview_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  if (m == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    RECT r;
    GetClientRect(w, &r);
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    if (picture) {
      OLE_XSIZE_HIMETRIC pw;
      OLE_YSIZE_HIMETRIC ph;
      picture->get_Width(&pw);
      picture->get_Height(&ph);
      if (pw > 0 && ph > 0) {
        int width = r.right, height = (int)((int64_t)width * ph / pw);
        if (height > r.bottom) {
          height = r.bottom;
          width = (int)((int64_t)height * pw / ph);
        }
        picture->Render(dc, (r.right - width) / 2, (r.bottom - height) / 2,
                        width, height, 0, ph, pw, -ph, nullptr);
      }
    } else {
      SetTextColor(dc, RGB(170, 180, 195));
      SetBkMode(dc, TRANSPARENT);
      DrawTextW(dc, L"Your stream will appear here", -1, &r,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    EndPaint(w, &ps);
    return 0;
  }
  return DefWindowProcW(w, m, wp, lp);
}
void update_preview() {
  bool enabled = SendMessageW(previewBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
  engine->preview(enabled && !IsIconic(window) && IsWindowVisible(window));
  if (!enabled) {
    if (picture) {
      picture->Release();
      picture = nullptr;
      lastPreview.clear();
      InvalidateRect(previewWindow, nullptr, FALSE);
    }
    return;
  }
  if (IsIconic(window) || !IsWindowVisible(window))
    return;
  auto jpeg = engine->preview_jpeg();
  if (jpeg.empty() || jpeg == lastPreview)
    return;
  HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, jpeg.size());
  if (!data)
    return;
  void *dest = GlobalLock(data);
  if (!dest) {
    GlobalFree(data);
    return;
  }
  memcpy(dest, jpeg.data(), jpeg.size());
  GlobalUnlock(data);
  IStream *stream = nullptr;
  if (CreateStreamOnHGlobal(data, TRUE, &stream) != S_OK) {
    GlobalFree(data);
    return;
  }
  IPicture *next = nullptr;
  HRESULT result = OleLoadPicture(stream, (LONG)jpeg.size(), FALSE,
                                  IID_IPicture, (void **)&next);
  stream->Release();
  if (SUCCEEDED(result)) {
    if (picture)
      picture->Release();
    picture = next;
    lastPreview = std::move(jpeg);
    InvalidateRect(previewWindow, nullptr, FALSE);
  }
}
void update() {
  bool active = engine->active(), working = busy;
  for (int id : {CONSOLE, ADD, MANAGE_CONSOLES, CONNECT, PATTERN, ROUTE,
                 RESOLUTION, FPS, BITRATE, IMAGE, QUALITY, SAVE, DEFAULTS})
    EnableWindow(GetDlgItem(window, id), !active && !working);
  EnableWindow(GetDlgItem(window, DISCONNECT), active && !working);
  auto status = engine->status();
  SetWindowTextW(statusLabel, wide(status).c_str());
  auto *console = selected();
  std::string observed = engine->console_state(console);
  std::wstring links = console && console->ps5 ? L"PS5: " : L"PlayStation: ";
  links += engine->connected() ? L"Connected"
           : active ? L"Connecting / test"
                    : wide(observed);
  links += engine->peer() ? L"     |     3DS: Connected"
                          : L"     |     3DS: Waiting";
  SetWindowTextW(linksLabel, links.c_str());
  auto d = engine->diagnostics();
  SetWindowTextW(diagnosticBox, wide(d).c_str());
  if (!working && engine->failed() && active) {
    async([] { engine->stop(); });
  }
  if (!working && active &&
      status == "3DS stream closed. Choose Disconnect or Rest mode." &&
      !closeOffered && !disconnectPromptOpen) {
    closeOffered = true;
    int choice = close_choice();
    if (choice != IDCANCEL)
      async([choice] { engine->stop(choice == IDYES); });
  }
  if (status != "3DS stream closed. Choose Disconnect or Rest mode.")
    closeOffered = false;
  update_preview();
  lastStatus = status;
}
void help() {
  MessageBoxW(
      window,
      L"1. Keep the PC and 3DS on the same local network.\n2. Choose Add "
      L"console. Enter its local IP address and Base64 PSN Account ID.\n3. "
      L"Enable Remote Play and generate a fresh Link Device PIN on the "
      L"console.\n4. Enter the PIN, save, and choose Connect.\n5. Enter one of "
      L"this PC's IPv4 addresses in the release 42 3DS relay app.\n\nBalanced "
      L"picture size is the default. Source resolution, source frame rate and "
      L"bitrate are independent of the image sent to the 3DS. Crop, touchpad "
      L"and motion controls are handled by the 3DS.\n\nRemote / Internet uses "
      L"your saved public address and existing router port forwarding, "
      L"matching Android's direct-IP mode. Pair locally first. PSN sign-in and "
      L"automatic NAT traversal are not included.\n\nMinimize to the taskbar "
      L"or use the tray menu; streaming continues. Exit stops the relay. The "
      L"app and installer do not request administrator access.\n\nThe PC "
      L"automatically discovers the 3DS on the local network and initiates "
      L"the connection. Just enter this PC's address in the 3DS app; no "
      L"handheld address or port entry is needed in Windows. This uses normal "
      L"local-network traffic without changing firewall rules. "
      L"Administrator-enforced policies can still block "
      L"networking.\n\nUsage statistics are controlled on the 3DS. The PC "
      L"forwards consented reports and retains only delivery counters. "
      L"Pairings and settings are encrypted for your Windows user.\n\nThis is "
      L"an independent homebrew application, not an official Sony or Nintendo "
      L"product.",
      L"3DSRP Companion - Setup and help", MB_OK | MB_ICONINFORMATION);
}
int disconnect_choice() {
  TASKDIALOG_BUTTON buttons[] = {{IDYES, L"Put console in Rest mode"},
                                 {IDNO, L"Leave console on"}};
  TASKDIALOGCONFIG config{};
  config.cbSize = sizeof(config);
  config.hwndParent = window;
  config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
  config.pszWindowTitle = L"Disconnect";
  config.pszMainInstruction = L"Put the PlayStation in Rest mode?";
  config.pszContent = L"Choose what happens to your console when the relay disconnects.";
  config.cButtons = 2;
  config.pButtons = buttons;
  config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
  config.nDefaultButton = IDNO;
  int result = IDCANCEL;
  if (FAILED(TaskDialogIndirect(&config, &result, nullptr, nullptr)))
    result = MessageBoxW(window, L"Put the console in Rest mode?\n\n"
        L"Yes: Rest mode and disconnect\nNo: Leave it on and disconnect\nCancel: Keep playing",
        L"Disconnect", MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
  return result;
}
void tray_menu() {
  POINT p;
  GetCursorPos(&p);
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, TRAY_OPEN, L"Open companion");
  AppendMenuW(menu, MF_STRING, TRAY_EXIT, L"Exit");
  SetForegroundWindow(window);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, window, nullptr);
  DestroyMenu(menu);
}
LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  try {
    if (taskbarCreated && m == taskbarCreated) {
      trayAvailable = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
      if (!trayAvailable && !IsWindowVisible(w))
        ShowWindow(w, SW_MINIMIZE);
      return 0;
    }
    switch (m) {
    case WM_CREATE: {
      startup_log::write("WM_CREATE: creating controls");
      window = w;
      HWND title = label(L"3DSRP Companion", 28, 20, 640, 34);
      SendMessageW(title, WM_SETFONT, (WPARAM)titleFont, TRUE);
      label(L"PlayStation to 3DS", 28, 60, 640);
      navigationButton = button(L"Settings", VIEW_SETTINGS, 732, 24, 200);

      buildingView = &viewWidgets[(size_t)View::Home];
      linksLabel = label(L"PlayStation: Idle     |     3DS: Waiting", 28, 104, 904, 26);
      label(L"YOUR CONSOLE", 28, 148, 520);
      consoleBox = combo(CONSOLE, 28, 176, 520, {});
      button(L"Manage consoles", MANAGE_CONSOLES, 568, 174, 170);
      button(L"+ Add console", ADD, 758, 174, 174);
      label(L"Connection", 28, 222, 270);
      routeBox = combo(ROUTE, 28, 248, 270, {L"Home network", L"Internet"});
      label(L"Picture quality", 322, 222, 330);
      imageBox = combo(IMAGE, 322, 248, 330,
                       {L"Low - 240 x 135", L"Balanced - 320 x 180", L"High - 400 x 225"});
      control(L"BUTTON", L"Connect", CONNECT, 28, 296, 180, 40,
              WS_TABSTOP | BS_DEFPUSHBUTTON);
      button(L"Disconnect", DISCONNECT, 228, 301, 160);
      statusLabel = label(L"Ready", 28, 350, 904, 24);
      previewWindow = control(L"3DSRPPreview", L"", 0, 28, 390, 640, 360);
      HWND ipHeading = label(L"On your 3DS", 696, 390, 236, 36);
      SendMessageW(ipHeading, WM_SETFONT, (WPARAM)titleFont, TRUE);
      label(L"ADD THIS PC IP ADDRESS", 696, 446, 236, 28);
      addressesBox = control(L"EDIT", addresses(true).c_str(), 0, 696, 486, 236, 100,
                             ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      SendMessageW(addressesBox, WM_SETFONT, (WPARAM)titleFont, TRUE);
      label(L"In 3DSRP on your handheld, add the IP shown above as the relay address. "
            L"Keep your PC and 3DS on the same home network.", 696, 608, 236, 98);
      previewBox = control(L"BUTTON", L"Show live preview", PREVIEW, 696, 722, 236, 28,
                           WS_TABSTOP | BS_AUTOCHECKBOX);
      button(L"Connection details", VIEW_DIAGNOSTICS, 28, 774, 200);
      button(L"Setup help", HELP, 772, 774, 160);

      buildingView = &viewWidgets[(size_t)View::Settings];
      label(L"STREAM SETTINGS", 28, 116, 904, 28);
      label(L"Fine-tune the stream from your PlayStation. Changes apply the next time you connect.",
            28, 158, 820, 52);
      label(L"Console resolution", 28, 238, 400);
      resBox = combo(RESOLUTION, 28, 266, 400, {L"360p", L"540p", L"720p", L"1080p"});
      label(L"Frame rate", 480, 238, 400);
      fpsBox = combo(FPS, 480, 266, 400, {L"30 fps", L"60 fps"});
      label(L"Bitrate (kbps)", 28, 336, 400);
      bitrateBox = control(L"EDIT", L"3000", BITRATE, 28, 364, 400, 28, WS_TABSTOP | ES_NUMBER);
      label(L"JPEG quality (1-100)", 480, 336, 400);
      qualityBox = control(L"EDIT", L"70", QUALITY, 480, 364, 400, 28, WS_TABSTOP | ES_NUMBER);
      label(L"Higher settings can improve image quality but require more bandwidth and processing.",
            28, 428, 820, 52);
      button(L"Save settings", SAVE, 28, 514, 180);
      button(L"Restore defaults", DEFAULTS, 232, 514, 180);

      buildingView = &viewWidgets[(size_t)View::Diagnostics];
      label(L"CONNECTION DETAILS", 28, 116, 904, 28);
      label(L"Use these details when troubleshooting your connection.", 28, 158, 904, 28);
      diagnosticBox = control(L"EDIT", L"", 0, 28, 216, 904, 400,
          ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
      button(L"Copy diagnostics", COPY_DIAG, 28, 648, 200);
      button(L"Connection test", PATTERN, 252, 648, 200);
      button(L"Open app data", OPEN_FOLDER, 476, 648, 200);
      label(L"Startup logs stay in the logs folder beside the app, or your temporary folder if needed. "
            L"Startup logs are not uploaded.", 28, 714, 904, 52);
      buildingView = nullptr;
      show_view(View::Home);
      startup_log::write("WM_CREATE: controls created; loading console list");
      refresh_consoles();
      display_settings();
      startup_log::write("WM_CREATE: settings displayed; preparing tray icon");
      tray.cbSize = sizeof(tray);
      tray.hWnd = w;
      tray.uID = 1;
      tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
      tray.uCallbackMessage = WM_TRAY;
      tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
      wcscpy_s(tray.szTip, L"3DSRP Companion");
      // Add the tray icon after the window is painted, on a separate thread.
      SetTimer(w, 1, 250, nullptr);
      SetTimer(w, 2, 5000, nullptr);
      startup_log::write("WM_CREATE completed");
      return 0;
    }
    case WM_TRAY_READY:
      trayAvailable = wp != 0;
      if (!trayAvailable && !IsWindowVisible(w))
        ShowWindow(w, SW_MINIMIZE);
      return 0;
    case WM_TIMER:
      { static bool first = true; if (first) { first = false; startup_log::write("First UI timer"); } }
      if (wp == 1)
        update();
      else
        SetWindowTextW(addressesBox, addresses(true).c_str());
      return 0;
    case WM_COMMAND: {
      int id = LOWORD(wp);
      char commandLog[80];
      std::snprintf(commandLog, sizeof(commandLog), "WM_COMMAND id=%d notification=%u", id, (unsigned)HIWORD(wp));
      startup_log::write(commandLog);
      if (id == VIEW_SETTINGS) {
        show_view(currentView == View::Home ? View::Settings : View::Home);
        return 0;
      }
      if (id == VIEW_DIAGNOSTICS) {
        show_view(View::Diagnostics);
        return 0;
      }
      if (id == MANAGE_CONSOLES) {
        console_menu();
        return 0;
      }
      if (id == TRAY_OPEN) {
        ShowWindow(w, SW_RESTORE);
        SetForegroundWindow(w);
        return 0;
      }
      if (id == TRAY_EXIT) {
        PostMessageW(w, WM_CLOSE, 0, 0);
        return 0;
      }
      if (id == PREVIEW) {
        engine->preview(SendMessageW(previewBox, BM_GETCHECK, 0, 0) ==
                        BST_CHECKED);
        return 0;
      }
      if (id == HELP) {
        help();
        return 0;
      }
      if (id == COPY_DIAG) {
        copy(wide(engine->diagnostics()));
        return 0;
      }
      if (id == OPEN_FOLDER) {
        ShellExecuteW(w, L"open", data_dir().c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
        return 0;
      }
      if (busy)
        return 0;
      if (id == DISCONNECT || id == REST) {
        if (disconnectPromptOpen) return 0;
        bool rest = id == REST;
        if (id == DISCONNECT && engine->connected()) {
          disconnectPromptOpen = true;
          int answer = disconnect_choice();
          disconnectPromptOpen = false;
          if (answer == IDCANCEL) return 0;
          rest = answer == IDYES;
        }
        async([rest] { engine->stop(rest); });
        return 0;
      }
      if (engine->active())
        return 0;
      if (id == ADD || id == EDIT) {
        edit_console(id == ADD);
        return 0;
      }
      if (id == REMOVE) {
        auto *c = selected();
        if (!c)
          return 0;
        if (MessageBoxW(w, L"Remove this console and its saved pairing?",
                        L"Remove console",
                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
          auto path = data_dir() + L"\\console-" + wide(c->id) + L".dat";
          if (!DeleteFileW(path.c_str()) &&
              GetLastError() != ERROR_FILE_NOT_FOUND)
            throw std::runtime_error("Could not delete console pairing");
          settings.consoles.erase(settings.consoles.begin() +
                                  selection(consoleBox));
          settings.selected = 0;
          settings.save();
          refresh_consoles();
        }
        return 0;
      }
      if (id == DEFAULTS) {
        settings.height = 360;
        settings.bitrate = 3000;
        settings.fps = 30;
        settings.image = 1;
        settings.quality = 70;
        settings.preview = true;
        display_settings();
        settings.save();
        return 0;
      }
      if (id == SAVE) {
        save_settings();
        show_view(View::Home);
        return 0;
      }
      if (id == CONSOLE && HIWORD(wp) == CBN_SELCHANGE) {
        settings.selected = selection(consoleBox);
        settings.save();
        return 0;
      }
      if (id == CONNECT || id == PATTERN || id == WAKE) {
        save_settings();
        StartOptions o;
        o.pattern = id == PATTERN;
        o.height = settings.height;
        o.bitrate = settings.bitrate;
        o.fps = settings.fps;
        o.image = settings.image;
        o.quality = settings.quality;
        o.remote = settings.remote;
        if (!o.pattern) {
          auto *c = selected();
          if (!c)
            throw std::runtime_error("Add a console first");
          o.console = *c;
          if (pinConsole == c->id)
            o.pin = pendingPin;
        }
        if (id == WAKE) {
          async([o] { engine->wake(o.console, o.remote); });
          return 0;
        }
        engine->start(o);
        pendingPin.clear();
        pinConsole.clear();
        return 0;
      }
      break;
    }
    case WM_WORK_DONE: {
      std::unique_ptr<std::string> result((std::string *)lp);
      if (worker.joinable())
        worker.join();
      busy = false;
      if (!result->empty())
        error(*result);
      if (closing) {
        DestroyWindow(w);
        return 0;
      }
      update();
      return 0;
    }
    case WM_TRAY:
      if (lp == WM_LBUTTONDBLCLK) {
        ShowWindow(w, SW_RESTORE);
        SetForegroundWindow(w);
      } else if (lp == WM_RBUTTONUP)
        tray_menu();
      return 0;
    case WM_SIZE:
      if (wp == SIZE_MINIMIZED)
        engine->preview(false);
      else
        resize_scrollbars(w);
      return 0;
    case WM_VSCROLL:
    case WM_HSCROLL:
      if (!lp)
        scroll_content(w, m == WM_VSCROLL ? SB_VERT : SB_HORZ, LOWORD(wp));
      return 0;
    case WM_MOUSEWHEEL:
      scroll_content(w, SB_VERT, SB_ENDSCROLL,
                     -GET_WHEEL_DELTA_WPARAM(wp) * 96 / WHEEL_DELTA);
      return 0;
    case WM_POWERBROADCAST:
      if (wp == PBT_APMSUSPEND && engine->active() && !busy)
        async([] { engine->stop(); });
      return TRUE;
    case WM_QUERYENDSESSION:
      return TRUE;
    case WM_CLOSE:
      if (closing)
        return 0;
      if (busy) {
        closing = true;
        ShowWindow(w, SW_HIDE);
        return 0;
      }
      if (engine->active()) {
        int answer =
            MessageBoxW(w,
                        L"The relay is running.\n\nYes: Minimize to the system "
                        L"tray and keep streaming\nNo: Stop the relay and "
                        L"exit\nCancel: Keep this window open",
                        L"Close companion", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDYES) {
          ShowWindow(w, trayAvailable ? SW_HIDE : SW_MINIMIZE);
          engine->preview(false);
          return 0;
        }
        if (answer == IDCANCEL)
          return 0;
      }
      closing = true;
      async([] { engine->stop(); });
      return 0;
    case WM_DESTROY:
      KillTimer(w, 1);
      KillTimer(w, 2);
      PostQuitMessage(0);
      return 0;
    }
  } catch (const std::exception &e) {
    startup_log::write(e.what());
    error(e.what());
  }
  return DefWindowProcW(w, m, wp, lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int show) {
  startup_log::start();
  instance = h;
  taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
  startup_log::write("Starting Winsock");
  WSADATA winsock{};
  if (WSAStartup(MAKEWORD(2, 2), &winsock)) {
    MessageBoxW(nullptr, L"Windows networking could not start",
                L"3DSRP Companion", MB_OK | MB_ICONERROR);
    return 1;
  }
  startup_log::write("Starting OLE");
  HRESULT ole = OleInitialize(nullptr);
  timeBeginPeriod(1);
  int code = 0;
  std::thread startupTray;
  try {
    startup_log::write("Loading settings");
    settings.load();
    startup_log::write("Creating engine");
    engine = std::make_unique<Engine>();
    startup_log::write("Engine created; initializing controls");
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    titleFont =
        CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    startup_log::write("Fonts created; registering window classes");
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = h;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cls.lpszClassName = L"3DSRPCompanion";
    cls.lpfnWndProc = proc;
    RegisterClassExW(&cls);
    cls.lpszClassName = L"3DSRPConsoleEditor";
    cls.lpfnWndProc = editor_proc;
    RegisterClassExW(&cls);
    cls.lpszClassName = L"3DSRPPreview";
    cls.lpfnWndProc = preview_proc;
    RegisterClassExW(&cls);
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int initialWidth =
        workArea.right > workArea.left
            ? std::min(1000L, workArea.right - workArea.left)
            : 1000;
    const int initialHeight =
        workArea.bottom > workArea.top
            ? std::min(900L, workArea.bottom - workArea.top)
            : 900;
    startup_log::write("Creating main window");
    HWND w = CreateWindowExW(WS_EX_CONTROLPARENT, L"3DSRPCompanion",
                             L"3DSRP Companion for Windows",
                             WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
                             CW_USEDEFAULT, CW_USEDEFAULT, initialWidth,
                             initialHeight, nullptr, nullptr, h, nullptr);
    if (!w)
      throw std::runtime_error("Could not create the companion window");
    startup_log::write("Main window created; showing window");
    ShowWindow(w, show);
    startup_log::write("Updating window");
    UpdateWindow(w);
    // Explorer can take seconds to accept a tray icon. Do not block painting
    // or message dispatch while it responds. The worker uses its own copy;
    // only the UI thread updates trayAvailable.
    try {
      startupTray = std::thread([icon = tray, w]() mutable {
        startup_log::write("Tray worker: adding icon");
        const BOOL added = Shell_NotifyIconW(NIM_ADD, &icon);
        startup_log::write(added ? "Tray worker: icon ready" : "Tray worker: icon unavailable");
        PostMessageW(w, WM_TRAY_READY, added != FALSE, 0);
      });
    } catch (const std::exception &e) {
      // Optional tray setup must not prevent the main window from running.
      startup_log::write(e.what());
    }
    startup_log::write("Entering message loop");
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      if (!IsDialogMessageW(w, &msg)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
    if (worker.joinable())
      worker.join();
    startup_log::write("Destroying engine");
    engine.reset();
    if (picture)
      picture->Release();
    DeleteObject(font);
    DeleteObject(titleFont);
  } catch (const std::exception &e) {
    startup_log::write(e.what());
    MessageBoxW(nullptr, wide(e.what()).c_str(), L"3DSRP Companion",
                MB_OK | MB_ICONERROR);
    code = 1;
  }
  // Finish any pending add before removal so closing during startup cannot
  // leave an icon behind. Never join this worker from the window procedure.
  if (startupTray.joinable())
    startupTray.join();
  if (tray.hWnd)
    Shell_NotifyIconW(NIM_DELETE, &tray);
  timeEndPeriod(1);
  if (SUCCEEDED(ole))
    OleUninitialize();
  WSACleanup();
  startup_log::write("Exiting wWinMain");
  return code;
}
