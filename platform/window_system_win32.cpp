// clang-format off
#include <windows.h>
#include <objbase.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl_core.h>
#include <winternl.h>
// clang-format on

#include <QDebug>
#include <algorithm>
#include <memory>
#include <set>
#include <vector>

#include "platform/app_identity.h"
#include "platform/pin_support.h"
#include "platform/window_system.h"

namespace yasb::platform {
namespace {

HWND toHwnd(WindowHandle hwnd) { return reinterpret_cast<HWND>(hwnd); }
WindowHandle fromHwnd(HWND hwnd) { return reinterpret_cast<WindowHandle>(hwnd); }

constexpr int kWmGetIconTimeoutMs = 200;

const std::set<QString>& ignoredProcesses() {
  static const std::set<QString> processes = {QStringLiteral("SearchHost.exe")};
  return processes;
}

const std::set<QString>& ignoredClasses() {
  static const std::set<QString> classes = {
      QStringLiteral("Progman"),
      QStringLiteral("Shell_TrayWnd"),
      QStringLiteral("Shell_SecondaryTrayWnd"),
      QStringLiteral("DV2ControlHost"),
      QStringLiteral("Windows.UI.Composition.DesktopWindowContentBridge"),
      QStringLiteral("ForegroundStaging"),
      QStringLiteral("ApplicationManager_DesktopShellWindow"),
      QStringLiteral("WorkerW"),
      QStringLiteral("Button"),
      QStringLiteral("Windows.UI.Input.InputSite.WindowClass"),
      QStringLiteral("Windows.Internal.Shell.TabProxyWindow"),
      QStringLiteral("Microsoft-Windows-Sts-ComponentHost-Elevated"),
      QStringLiteral("SysListView32"),
      QStringLiteral("XamlExplorerHostIslandWindow_WASDK"),
      QStringLiteral("Microsoft.UI.Content.PopupWindowSiteBridge"),
      QStringLiteral("Microsoft.UI.Content.DesktopChildSiteBridge"),
      QStringLiteral("SysHeader32"),
      QStringLiteral("#32768"),
      QStringLiteral("msctls_statusbar32"),
      QStringLiteral("DirectUIHWND"),
      QStringLiteral("SHELLDLL_DefView"),
  };
  return classes;
}

QString windowTitle(HWND hwnd) {
  const int length = GetWindowTextLengthW(hwnd);
  if (length <= 0) return QString();
  std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1);
  const int copied = GetWindowTextW(hwnd, buffer.data(), length + 1);
  return QString::fromWCharArray(buffer.data(), copied);
}

QString windowClass(HWND hwnd) {
  wchar_t buffer[256] = {};
  const int copied = GetClassNameW(hwnd, buffer, 256);
  return QString::fromWCharArray(buffer, copied);
}

bool windowCloaked(HWND hwnd) {
  DWORD cloaked = 0;
  return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
         cloaked > 0;
}

// Explorer immersive shell surfaces (Start, Search overlays, UWP frames) are not taskbar windows.
bool isImmersiveShellWindow(const WindowInfo& info) {
  const QString& cls = info.className;
  if (cls.isEmpty()) return false;
  const auto exStyle = static_cast<DWORD>(GetWindowLongW(toHwnd(info.hwnd), GWL_EXSTYLE));
  static const std::set<QString> framed = {
      QStringLiteral("ApplicationFrameWindow"), QStringLiteral("Windows.UI.Core.CoreWindow"),
      QStringLiteral("StartMenuSizingFrame"), QStringLiteral("Shell_LightDismissOverlay")};
  static const std::set<QString> explorerOnly = {
      QStringLiteral("ImmersiveBackgroundWindow"), QStringLiteral("SearchPane"),
      QStringLiteral("NativeHWNDHost"), QStringLiteral("Shell_CharmWindow"),
      QStringLiteral("ImmersiveLauncher")};
  if (framed.count(cls)) return (exStyle & WS_EX_WINDOWEDGE) == 0;
  if (explorerOnly.count(cls)) {
    return info.processName.value_or(QString()).toLower().contains(QLatin1String("explorer.exe"));
  }
  return false;
}

bool canAddToTaskbar(HWND hwnd) {
  const auto exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
  const bool isAppWindow = (exStyle & WS_EX_APPWINDOW) != 0;
  const bool isToolWindow = (exStyle & WS_EX_TOOLWINDOW) != 0;
  const bool isNoActivate = (exStyle & WS_EX_NOACTIVATE) != 0;
  const bool hasOwner = GetWindow(hwnd, GW_OWNER) != nullptr;
  const bool isDeleted = GetPropW(hwnd, L"ITaskList_Deleted") != nullptr;
  return IsWindow(hwnd) && IsWindowVisible(hwnd) && (!hasOwner || isAppWindow) &&
         (!isNoActivate || isAppWindow) && !isToolWindow && !isDeleted;
}

// Pre-launched background UWP apps are cloaked and belong to no virtual desktop; real ones on
// another desktop are cloaked but assigned to one.
bool isGhostUwpApp(const WindowInfo& info) {
  if (info.processName.value_or(QString()) != QLatin1String("ApplicationFrameHost.exe"))
    return false;
  if (!info.isCloaked) return false;
  ComScope com;
  IVirtualDesktopManager* manager = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(VirtualDesktopManager), nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&manager))) ||
      !manager) {
    return true;
  }
  GUID desktop = {};
  const HRESULT result = manager->GetWindowDesktopId(toHwnd(info.hwnd), &desktop);
  manager->Release();
  if (FAILED(result)) return true;
  return IsEqualGUID(desktop, GUID_NULL) != FALSE;
}

// --- icons

// A GetDIBits read of an icon bitmap as top-down 32-bit BGRA.
bool readBitmap(HBITMAP bitmap, int width, int height, std::vector<unsigned char>& pixels) {
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  pixels.assign(static_cast<size_t>(width) * height * 4, 0);
  HDC dc = GetDC(nullptr);
  const int lines =
      GetDIBits(dc, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS);
  ReleaseDC(nullptr, dc);
  return lines == height;
}

IconBitmap bitmapFromHicon(HICON icon) {
  IconBitmap result;
  if (!icon) return result;
  ICONINFO iconInfo = {};
  if (!GetIconInfo(icon, &iconInfo)) return result;
  BITMAP colorInfo = {};
  if (iconInfo.hbmColor && GetObjectW(iconInfo.hbmColor, sizeof(colorInfo), &colorInfo)) {
    const int width = colorInfo.bmWidth;
    const int height = colorInfo.bmHeight;
    std::vector<unsigned char> pixels;
    if (width > 0 && height > 0 && readBitmap(iconInfo.hbmColor, width, height, pixels)) {
      bool hasAlpha = false;
      for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
          hasAlpha = true;
          break;
        }
      }
      if (!hasAlpha && iconInfo.hbmMask) {
        // Old style icon: the AND mask says which pixels are transparent.
        std::vector<unsigned char> mask;
        if (readBitmap(iconInfo.hbmMask, width, height, mask)) {
          for (size_t i = 0; i < pixels.size(); i += 4) pixels[i + 3] = mask[i] ? 0 : 255;
          hasAlpha = true;
        }
      }
      if (hasAlpha) {
        result.width = width;
        result.height = height;
        result.bgra = QByteArray(reinterpret_cast<const char*>(pixels.data()),
                                 static_cast<qsizetype>(pixels.size()));
      }
    }
  }
  if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
  if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
  return result;
}

// The 32-bit premultiplied DIB IShellItemImageFactory hands back, as straight alpha.
IconBitmap bitmapFromShell(HBITMAP bitmap) {
  IconBitmap result;
  BITMAP info = {};
  if (!bitmap || !GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 ||
      info.bmHeight <= 0) {
    return result;
  }
  std::vector<unsigned char> pixels;
  if (!readBitmap(bitmap, info.bmWidth, info.bmHeight, pixels)) return result;
  for (size_t i = 0; i < pixels.size(); i += 4) {
    const unsigned alpha = pixels[i + 3];
    if (alpha != 0 && alpha != 255) {
      for (size_t c = 0; c < 3; ++c)
        pixels[i + c] = static_cast<unsigned char>(std::min(255u, pixels[i + c] * 255u / alpha));
    }
  }
  result.width = info.bmWidth;
  result.height = info.bmHeight;
  result.bgra = QByteArray(reinterpret_cast<const char*>(pixels.data()),
                           static_cast<qsizetype>(pixels.size()));
  return result;
}

IconBitmap shellIcon(const QString& parsingName, int size) {
  IShellItemImageFactory* factory = nullptr;
  if (FAILED(SHCreateItemFromParsingName(reinterpret_cast<PCWSTR>(parsingName.utf16()), nullptr,
                                         IID_PPV_ARGS(&factory))) ||
      !factory) {
    return {};
  }
  HBITMAP bitmap = nullptr;
  IconBitmap result;
  if (SUCCEEDED(
          factory->GetImage(SIZE{size, size}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap))) {
    result = bitmapFromShell(bitmap);
  }
  if (bitmap) DeleteObject(bitmap);
  factory->Release();
  return result;
}

// The window's own icon (WM_GETICON big, else small) and whether it answered in time.
IconBitmap ownIcon(HWND hwnd, bool& answered) {
  answered = true;
  for (WPARAM which : {static_cast<WPARAM>(ICON_BIG), static_cast<WPARAM>(ICON_SMALL)}) {
    DWORD_PTR icon = 0;
    // SMTO_BLOCK so messages arriving while we wait do not restart the timeout.
    if (!SendMessageTimeoutW(hwnd, WM_GETICON, which, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                             kWmGetIconTimeoutMs, &icon)) {
      answered = false;
      return {};
    }
    IconBitmap bitmap = bitmapFromHicon(reinterpret_cast<HICON>(icon));
    if (!bitmap.isNull()) return bitmap;
  }
  return {};
}

class Win32WindowSystem final : public WindowSystem {
 public:
  bool isWindow(WindowHandle hwnd) const override { return IsWindow(toHwnd(hwnd)) != FALSE; }
  bool isIconic(WindowHandle hwnd) const override { return IsIconic(toHwnd(hwnd)) != FALSE; }
  WindowHandle foregroundWindow() const override { return fromHwnd(GetForegroundWindow()); }

  std::vector<WindowHandle> enumerateWindows() const override {
    std::vector<WindowHandle> windows;
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
          reinterpret_cast<std::vector<WindowHandle>*>(param)->push_back(fromHwnd(hwnd));
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    return windows;
  }

  WindowInfo query(WindowHandle handle) const override {
    HWND hwnd = toHwnd(handle);
    WindowInfo info;
    info.hwnd = handle;
    info.title = windowTitle(hwnd);
    info.className = windowClass(hwnd);
    info.isCloaked = windowCloaked(hwnd);
    info.monitor = reinterpret_cast<WindowHandle>(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL));
    info.canMinimize = canMinimize(handle);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return info;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
      // These errors mean the process is gone; any other means it exists but cannot be opened.
      const DWORD error = GetLastError();
      const bool gone =
          error == ERROR_INVALID_PARAMETER || error == ERROR_INVALID_HANDLE || error == 0;
      info.pid = gone ? 0 : pid;
      return info;
    }
    wchar_t path[MAX_PATH * 4] = {};
    DWORD length = static_cast<DWORD>(std::size(path));
    if (QueryFullProcessImageNameW(process, 0, path, &length) && length > 0) {
      info.processPath = QString::fromWCharArray(path, static_cast<int>(length));
      const qsizetype slash = info.processPath.lastIndexOf(QLatin1Char('\\'));
      info.processName = info.processPath.mid(slash + 1);
    }
    CloseHandle(process);
    info.pid = pid;
    return info;
  }

  bool isTaskbarWindow(const WindowInfo& info) const override {
    HWND hwnd = toHwnd(info.hwnd);
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
    if (info.title.trimmed().isEmpty()) return false;
    if (isImmersiveShellWindow(info)) return false;
    if (!canAddToTaskbar(hwnd)) return false;
    if (info.processName && ignoredProcesses().count(*info.processName)) return false;
    if (ignoredClasses().count(info.className)) return false;
    if (isGhostUwpApp(info)) return false;
    return true;
  }

  bool canMinimize(WindowHandle handle) const override {
    HWND hwnd = toHwnd(handle);
    const auto style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
    return (style & WS_MINIMIZEBOX) != 0 && IsWindowEnabled(hwnd) != FALSE;
  }

  WindowHandle monitorOfWindow(WindowHandle hwnd) const override {
    return reinterpret_cast<WindowHandle>(MonitorFromWindow(toHwnd(hwnd), MONITOR_DEFAULTTONULL));
  }

  QRect windowRect(WindowHandle hwnd) const override {
    RECT rect = {};
    if (!GetWindowRect(toHwnd(hwnd), &rect)) return {};
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
  }

  std::pair<WindowHandle, WindowHandle> resolveBaseAndFocus(WindowHandle handle) const override {
    HWND hwnd = toHwnd(handle);
    HWND base = GetAncestor(hwnd, GA_ROOTOWNER);
    if (!base) base = GetAncestor(hwnd, GA_ROOT);
    if (!base) base = hwnd;
    HWND focus = base;
    HWND last = GetLastActivePopup(base);
    if (last && IsWindowVisible(last)) focus = last;
    return {fromHwnd(base), fromHwnd(focus)};
  }

  bool isOwnerRootActive(WindowHandle base) const override {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    HWND top = GetAncestor(foreground, GA_ROOTOWNER);
    if (!top) top = GetAncestor(foreground, GA_ROOT);
    if (!top) top = foreground;
    return fromHwnd(top) == base;
  }

  std::optional<QString> appUniqueId(const WindowInfo& info) const override {
    return platform::appUniqueId(info.hwnd, info.processPath, info.pid);
  }

  IconBitmap windowIcon(WindowHandle handle, int size) const override {
    ComScope com;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return {};
    bool answered = true;
    IconBitmap own = ownIcon(hwnd, answered);
    IconBitmap image;
    if (!own.isNull() && own.width >= size) image = std::move(own);

    if (image.isNull()) {
      const WindowInfo info = query(handle);
      const auto aumid = aumidForWindow(handle, info.pid);
      // Only Store app IDs (Family!App) give the real icon, their exe has a generic one.
      if (aumid && aumid->contains(QLatin1Char('!')))
        image = shellIcon(QStringLiteral("shell:AppsFolder\\") + *aumid, size);
      if (image.isNull() && !info.processPath.isEmpty()) image = shellIcon(info.processPath, size);
    }
    if (image.isNull()) {
      // The window class' small and big icons, the larger one wins.
      for (int index : {GCLP_HICONSM, GCLP_HICON}) {
        IconBitmap candidate =
            bitmapFromHicon(reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, index)));
        if (!candidate.isNull() && (image.isNull() || candidate.width > image.width))
          image = std::move(candidate);
      }
    }
    if (image.isNull()) {
      const int metric = GetSystemMetrics(SM_CXICON);
      HICON fallback = static_cast<HICON>(
          LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON, metric, metric, LR_SHARED));
      image = bitmapFromHicon(fallback);
    }
    image.complete = answered;
    return image;
  }

  // elevated apps do not take posted messages (UIPI); ShowWindowAsync still works on them
  void restoreWindow(WindowHandle hwnd) override { postSysCommand(hwnd, SC_RESTORE, SW_RESTORE); }
  void minimizeWindow(WindowHandle hwnd) override {
    postSysCommand(hwnd, SC_MINIMIZE, SW_MINIMIZE);
  }
  void showWindow(WindowHandle hwnd) override { ShowWindowAsync(toHwnd(hwnd), SW_SHOW); }

  void setForeground(WindowHandle hwnd) override {
    SetWindowPos(toHwnd(hwnd), HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    SetForegroundWindow(toHwnd(hwnd));
  }

  void closeApplication(WindowHandle handle, bool force) override {
    HWND hwnd = toHwnd(handle);
    if (!hwnd) return;
    HWND target = GetAncestor(hwnd, GA_ROOTOWNER);
    if (!target) target = GetAncestor(hwnd, GA_ROOT);
    if (!target) target = hwnd;

    if (force) {
      // EndTask is what the shell's "End task" uses. It is not in every SDK import library, so
      // look it up, and fall back to terminating the process.
      using EndTaskFn = BOOL(WINAPI*)(HWND, BOOL, BOOL);
      static const auto endTask =
          reinterpret_cast<EndTaskFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "EndTask"));
      if (endTask && endTask(target, FALSE, TRUE)) return;
      DWORD pid = 0;
      GetWindowThreadProcessId(hwnd, &pid);
      if (pid == 0) return;
      HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
      if (!process) return;
      TerminateProcess(process, 1);
      CloseHandle(process);
      return;
    }

    // SC_CLOSE through SendMessageTimeout, so a hung window cannot hang the bar.
    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(target, WM_SYSCOMMAND, SC_CLOSE, 0, SMTO_ABORTIFHUNG, 2000, &result))
      return;
    // The call also gives up while the app shows a modal prompt ("Save changes?"); only a hung
    // window needs the fallback, a responsive one would get a second close on top of the prompt.
    if (!IsHungAppWindow(target)) return;
    if (PostMessageW(target, WM_CLOSE, 0, 0)) return;
    using EndTaskFn = BOOL(WINAPI*)(HWND, BOOL, BOOL);
    static const auto endTask =
        reinterpret_cast<EndTaskFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "EndTask"));
    if (endTask) endTask(target, FALSE, FALSE);
  }

 private:
  static void postSysCommand(WindowHandle hwnd, WPARAM command, int fallbackShowCommand) {
    if (!PostMessageW(toHwnd(hwnd), WM_SYSCOMMAND, command, 0))
      ShowWindowAsync(toHwnd(hwnd), fallbackShowCommand);
  }
};

}  // namespace

IconBitmap shellItemIcon(const QString& parsingName, int size) {
  ComScope com;
  return shellIcon(parsingName, size);
}

IconBitmap iconBitmapFromHicon(void* hicon) { return bitmapFromHicon(static_cast<HICON>(hicon)); }

IconBitmap defaultAppIcon() {
  const int metric = GetSystemMetrics(SM_CXICON);
  return bitmapFromHicon(static_cast<HICON>(
      LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON, metric, metric, LR_SHARED)));
}

std::shared_ptr<WindowSystem> makeWin32WindowSystem() {
  return std::make_shared<Win32WindowSystem>();
}

}  // namespace yasb::platform
