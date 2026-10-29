#include "features/tray/win32/tray_win.h"

// windows.h first.
#include <windows.h>
#include <psapi.h>

#include <QThread>
#include <vector>

#include "features/tray/tray_host.h"
#include "features/tray/tray_pipe.h"

namespace wabar::tray {
namespace {

HWND toHwnd(std::uint64_t value) { return reinterpret_cast<HWND>(static_cast<ULONG_PTR>(value)); }

struct HandleCloser {
  HANDLE handle;
  ~HandleCloser() {
    if (handle) CloseHandle(handle);
  }
};

class RealTrayWindowApi final : public TrayWindowApi {
 public:
  bool isWindow(std::uint64_t hwnd) override { return IsWindow(toHwnd(hwnd)) != FALSE; }

  void allowSetForeground(std::uint64_t hwnd) override {
    DWORD pid = 0;
    GetWindowThreadProcessId(toHwnd(hwnd), &pid);
    AllowSetForegroundWindow(pid);
  }

  void sendNotifyMessage(std::uint64_t hwnd, std::uint32_t message, std::uint64_t wParam,
                         std::int64_t lParam) override {
    SendNotifyMessageW(toHwnd(hwnd), message, static_cast<WPARAM>(wParam),
                       static_cast<LPARAM>(lParam));
  }

  std::pair<int, int> cursorPos() override {
    POINT point{};
    GetCursorPos(&point);
    return {point.x, point.y};
  }

  void broadcastTaskbarCreated() override {
    const UINT message = RegisterWindowMessageW(L"TaskbarCreated");
    SendNotifyMessageW(HWND_BROADCAST, message, 0, 0);
  }
};

}  // namespace

TrayHost::TrayHost(QObject* parent) : QObject(parent) {
  qRegisterMetaType<wabar::tray::TrayIconData>();
}

std::unique_ptr<TrayWindowApi> makeTrayWindowApi() { return std::make_unique<RealTrayWindowApi>(); }

HostArch hostArch() {
  USHORT processMachine = 0;
  USHORT nativeMachine = 0;
  if (!IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine))
    return HostArch::Unknown;
  switch (nativeMachine) {
    case IMAGE_FILE_MACHINE_AMD64:
      return HostArch::X64;
    case IMAGE_FILE_MACHINE_ARM64:
      return HostArch::Arm64;
    default:
      return HostArch::Unknown;
  }
}

bool runningUnderEmulation() {
  USHORT processMachine = 0;
  USHORT nativeMachine = 0;
  if (!IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine)) return false;
  return processMachine != IMAGE_FILE_MACHINE_UNKNOWN;
}

QString trayHookDllName() {
  switch (hostArch()) {
    case HostArch::X64:
      return QStringLiteral("YASBNativeTrayHook.dll");
    case HostArch::Arm64:
      return QStringLiteral("YASBNativeTrayHook_arm64.dll");
    default:
      return {};
  }
}

QString exePathFromHwnd(std::uint64_t hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(toHwnd(hwnd), &pid);
  if (pid == 0) return {};
  HandleCloser process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
  if (!process.handle) return {};
  wchar_t path[MAX_PATH * 4] = {};
  DWORD size = static_cast<DWORD>(std::size(path));
  if (!QueryFullProcessImageNameW(process.handle, 0, path, &size)) return {};
  return QString::fromWCharArray(path, static_cast<int>(size));
}

std::optional<std::uint32_t> explorerPid() {
  wchar_t windowsDir[MAX_PATH] = {};
  if (!GetSystemWindowsDirectoryW(windowsDir, MAX_PATH)) return std::nullopt;
  const QString expected =
      (QString::fromWCharArray(windowsDir) + QStringLiteral("\\explorer.exe")).toLower();
  for (int attempt = 0; attempt < 10; ++attempt) {
    if (attempt > 0) QThread::msleep(50);
    HWND shell = GetShellWindow();
    if (!shell) continue;
    DWORD pid = 0;
    if (!GetWindowThreadProcessId(shell, &pid) || pid == 0) continue;
    HandleCloser process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process.handle) continue;
    wchar_t path[MAX_PATH * 4] = {};
    DWORD size = static_cast<DWORD>(std::size(path));
    if (!QueryFullProcessImageNameW(process.handle, 0, path, &size)) continue;
    if (QString::fromWCharArray(path, static_cast<int>(size)).toLower() != expected) continue;
    return static_cast<std::uint32_t>(pid);
  }
  return std::nullopt;
}

bool isModuleLoaded(std::uint32_t pid, const QString& moduleName) {
  HandleCloser process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid)};
  if (!process.handle) return false;
  std::vector<HMODULE> modules(1024);
  DWORD needed = 0;
  if (!EnumProcessModulesEx(process.handle, modules.data(),
                            static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed,
                            LIST_MODULES_ALL)) {
    return false;
  }
  const size_t count = std::min<size_t>(needed / sizeof(HMODULE), modules.size());
  const QString target = moduleName.toLower();
  wchar_t name[512];
  for (size_t i = 0; i < count; ++i) {
    const DWORD length = GetModuleBaseNameW(process.handle, modules[i], name, 512);
    if (length && QString::fromWCharArray(name, static_cast<int>(length)).toLower() == target)
      return true;
  }
  return false;
}

bool anyMouseButtonDown() {
  return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ||
         (GetAsyncKeyState(VK_MBUTTON) & 0x8000);
}

bool extractIconRgba(TrayIconData& data) {
  if (data.hIcon == 0) return false;
  HICON copy = CopyIcon(reinterpret_cast<HICON>(static_cast<ULONG_PTR>(data.hIcon)));
  if (!copy) return false;
  ICONINFO info{};
  const bool haveInfo = GetIconInfo(copy, &info) != FALSE;
  DestroyIcon(copy);
  if (!haveInfo) return false;

  bool ok = false;
  BITMAP bitmap{};
  if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap)) {
    const int width = bitmap.bmWidth;
    const int height = bitmap.bmHeight;
    if (width > 0 && height > 0 && width <= 256 && height <= 256) {
      BITMAPINFOHEADER header{};
      header.biSize = sizeof(header);
      header.biWidth = width;
      header.biHeight = -height;
      header.biPlanes = 1;
      header.biBitCount = 32;
      header.biCompression = BI_RGB;
      const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
      std::vector<BYTE> color(pixels * 4);
      std::vector<BYTE> mask(pixels * 4);
      HDC dc = CreateCompatibleDC(nullptr);
      const bool haveColor =
          GetDIBits(dc, info.hbmColor, 0, height, color.data(),
                    reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS) == height;
      // Icons without an alpha channel keep their transparency in the AND mask.
      bool maskBased = true;
      for (size_t i = 0; i < pixels; ++i) {
        if (color[i * 4 + 3] != 0) {
          maskBased = false;
          break;
        }
      }
      bool haveMask = false;
      if (haveColor && maskBased && info.hbmMask) {
        haveMask = GetDIBits(dc, info.hbmMask, 0, height, mask.data(),
                             reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS) == height;
      }
      DeleteDC(dc);
      if (haveColor) {
        QByteArray rgba(static_cast<qsizetype>(pixels * 4), Qt::Uninitialized);
        for (size_t i = 0; i < pixels; ++i) {
          const BYTE b = color[i * 4 + 0];
          const BYTE g = color[i * 4 + 1];
          const BYTE r = color[i * 4 + 2];
          BYTE a = color[i * 4 + 3];
          if (maskBased) a = (haveMask && mask[i * 4] == 255) ? 0 : 255;
          rgba[static_cast<qsizetype>(i * 4 + 0)] = static_cast<char>(r);
          rgba[static_cast<qsizetype>(i * 4 + 1)] = static_cast<char>(g);
          rgba[static_cast<qsizetype>(i * 4 + 2)] = static_cast<char>(b);
          rgba[static_cast<qsizetype>(i * 4 + 3)] = static_cast<char>(a);
        }
        data.iconRgba = rgba;
        data.iconWidth = width;
        data.iconHeight = height;
        ok = true;
      }
    }
  }
  if (info.hbmMask) DeleteObject(info.hbmMask);
  if (info.hbmColor) DeleteObject(info.hbmColor);
  return ok;
}

}  // namespace wabar::tray
