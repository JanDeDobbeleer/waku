#include "platform/systray_legacy.h"

// windows.h first.
#include <windows.h>

#include <QDebug>

#include "platform/systray_pipe.h"
#include "platform/systray_win.h"

namespace yasb::platform {
namespace {

constexpr wchar_t kClassName[] = L"Shell_TrayWnd";
constexpr UINT_PTR kKeepOnTopTimer = 1;

HWND asHwnd(void* value) { return static_cast<HWND>(value); }

// find_real_tray_hwnd: the Shell_TrayWnd that belongs to explorer.exe and is not ours.
HWND findRealTrayWindow(HWND ignore) {
  HWND hwnd = nullptr;
  while ((hwnd = FindWindowExW(nullptr, hwnd, kClassName, nullptr)) != nullptr) {
    if (hwnd == ignore) continue;
    const QString exe = exePathFromHwnd(reinterpret_cast<std::uintptr_t>(hwnd));
    if (exe.endsWith(QStringLiteral("explorer.exe"), Qt::CaseInsensitive)) return hwnd;
  }
  return nullptr;
}

// WINNOTIFYICONIDENTIFIER (dwData 3): hand back the cursor as the icon rectangle corner.
struct NotifyIconIdentifier {
  std::uint32_t magic;
  std::uint32_t message;
  std::uint32_t callbackSize;
  std::uint32_t padding;
  std::uint32_t windowHandle;
  std::uint32_t uid;
  GUID guid;
};

}  // namespace

struct LegacyAccess {
  static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SystrayLegacy* self = nullptr;
    if (message == WM_NCCREATE) {
      self = static_cast<SystrayLegacy*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
      self = reinterpret_cast<SystrayLegacy*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
    try {
      return self->handleMessage(hwnd, message, wParam, lParam);
    } catch (...) {
      // Exceptions never cross the window procedure boundary.
      return DefWindowProcW(hwnd, message, wParam, lParam);
    }
  }
};

SystrayLegacy::SystrayLegacy(QObject* parent) : TrayHost(parent) {}

SystrayLegacy::~SystrayLegacy() { stop(); }

void SystrayLegacy::start() {
  if (thread_.joinable()) return;
  stopping_ = false;
  thread_ = std::thread([this] { threadMain(); });
}

void SystrayLegacy::stop() {
  stopping_ = true;
  if (void* hwnd = hwnd_.load()) PostMessageW(asHwnd(hwnd), WM_CLOSE, 0, 0);
  if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

void SystrayLegacy::threadMain() {
  SetThreadDescription(GetCurrentThread(), L"SystrayMonitor");
  qDebug() << "Tray monitor thread is starting";
  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = &LegacyAccess::proc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = kClassName;
  if (!RegisterClassW(&windowClass)) {
    qWarning() << "Tray monitor: window class registration failed";
    return;
  }
  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName, kClassName,
                              WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, nullptr,
                              nullptr, windowClass.hInstance, this);
  if (!hwnd) {
    qCritical() << "Tray monitor: window creation failed";
    UnregisterClassW(kClassName, windowClass.hInstance);
    return;
  }
  hwnd_ = hwnd;
  // Stay above Explorer's own tray window so apps find this one first.
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SetTimer(hwnd, kKeepOnTopTimer, 100, nullptr);
  if (stopping_) PostMessageW(hwnd, WM_CLOSE, 0, 0);
  emit updateIcons();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  hwnd_ = nullptr;
  UnregisterClassW(kClassName, windowClass.hInstance);
}

std::int64_t SystrayLegacy::handleMessage(void* hwndPtr, unsigned message, std::uint64_t wParam,
                                          std::int64_t lParam) {
  HWND hwnd = asHwnd(hwndPtr);
  if (message == WM_CLOSE) {
    DestroyWindow(hwnd);
    return 0;
  }
  if (message == WM_DESTROY) {
    KillTimer(hwnd, kKeepOnTopTimer);
    PostQuitMessage(0);
    return 0;
  }
  if (stopping_) return DefWindowProcW(hwnd, message, wParam, lParam);
  if (message == taskbarCreated_ && taskbarCreated_ != 0) {
    SetPropW(hwnd, L"TaskbandHWND", hwnd);
    return 0;
  }
  if (message == WM_TIMER) {
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return 0;
  }
  if (message == WM_COPYDATA) return handleCopyData(hwnd, message, wParam, lParam);
  if (message == WM_ACTIVATEAPP || message == WM_COMMAND || message >= WM_USER)
    return forwardMessage(hwnd, message, wParam, lParam);
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::int64_t SystrayLegacy::handleCopyData(void* hwndPtr, unsigned message, std::uint64_t wParam,
                                           std::int64_t lParam) {
  const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
  if (!copy || copy->cbData == 0) return 0;
  if (copy->dwData == 1) {
    ParsedTrayMessage parsed = parseShellTrayData(static_cast<const char*>(copy->lpData),
                                                  copy->cbData, nullptr, 0, 0, 0, exePathFromHwnd);
    if (parsed.kind == ParsedTrayMessage::Kind::IconModified) {
      if (parsed.data.uFlags & kNifIcon) extractIconRgba(parsed.data);
      emit iconModified(parsed.data);
    } else if (parsed.kind == ParsedTrayMessage::Kind::IconDeleted) {
      emit iconDeleted(parsed.data);
    }
    return forwardMessage(hwndPtr, message, wParam, lParam);
  }
  if (copy->dwData == 3 && copy->lpData && copy->cbData >= sizeof(NotifyIconIdentifier)) {
    const auto* identifier = static_cast<const NotifyIconIdentifier*>(copy->lpData);
    POINT cursor{};
    GetCursorPos(&cursor);
    if (identifier->message == 1) return packI32(cursor.x, cursor.y + 1);
    if (identifier->message == 2) return packI32(cursor.x + 1, cursor.y - 1);
    return 0;
  }
  return forwardMessage(hwndPtr, message, wParam, lParam);
}

std::int64_t SystrayLegacy::forwardMessage(void* hwndPtr, unsigned message, std::uint64_t wParam,
                                           std::int64_t lParam) {
  HWND hwnd = asHwnd(hwndPtr);
  if (!realTray_ || !IsWindow(asHwnd(realTray_))) realTray_ = findRealTrayWindow(hwnd);
  if (!realTray_) return DefWindowProcW(hwnd, message, wParam, lParam);
  if (message == WM_USER + 372) {  // asynchronous tray message
    PostMessageW(asHwnd(realTray_), message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
  }
  return SendMessageW(asHwnd(realTray_), message, wParam, lParam);
}

}  // namespace yasb::platform
