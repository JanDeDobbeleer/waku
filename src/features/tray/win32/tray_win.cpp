#include "features/tray/win32/tray_win.h"

// windows.h first.
#include <windows.h>
#include <psapi.h>

#include <QThread>
#include <vector>

#include "features/tray/icon_bits.h"
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
      return QStringLiteral("WabarTrayHook.dll");
    case HostArch::Arm64:
      return QStringLiteral("WabarTrayHook_arm64.dll");
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

std::optional<std::uint32_t> explorerPid(const std::function<bool()>& stopping) {
  wchar_t windowsDir[MAX_PATH] = {};
  if (!GetSystemWindowsDirectoryW(windowsDir, MAX_PATH)) return std::nullopt;
  const QString expected =
      (QString::fromWCharArray(windowsDir) + QStringLiteral("\\explorer.exe")).toLower();
  for (int attempt = 0; attempt < 10; ++attempt) {
    if (attempt > 0) {
      if (stopping && stopping()) return std::nullopt;
      QThread::msleep(50);
    }
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

namespace {

constexpr int kMaxIconSide = 256;

// `rows` rows of `bitmap` as a top-down 32 bpp DIB; empty when GetDIBits fails.
QByteArray readBits(HDC dc, HBITMAP bitmap, int width, int rows) {
  BITMAPINFOHEADER header{};
  header.biSize = sizeof(header);
  header.biWidth = width;
  header.biHeight = -rows;
  header.biPlanes = 1;
  header.biBitCount = 32;
  header.biCompression = BI_RGB;
  QByteArray bits(qsizetype{4} * width * rows, '\0');
  if (GetDIBits(dc, bitmap, 0, static_cast<UINT>(rows), bits.data(),
                reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS) != rows) {
    return {};
  }
  return bits;
}

// Converts the bitmaps of `info` into data's RGBA; sets data.iconError when it cannot.
bool convertIconInfo(const ICONINFO& info, TrayIconData& data) {
  BITMAP bitmap{};
  HBITMAP sized = info.hbmColor ? info.hbmColor : info.hbmMask;
  if (!sized || !GetObjectW(sized, sizeof(bitmap), &bitmap)) {
    data.iconError = QStringLiteral("the icon has no readable bitmap");
    return false;
  }
  const int width = bitmap.bmWidth;
  // A monochrome icon stacks the AND and XOR masks in one bitmap of twice the height.
  const int height = info.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
  if (width <= 0 || height <= 0 || width > kMaxIconSide || height > kMaxIconSide) {
    data.iconError = QStringLiteral("unsupported icon size %1x%2").arg(width).arg(height);
    return false;
  }
  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc) {
    data.iconError = QStringLiteral("CreateCompatibleDC failed");
    return false;
  }
  QByteArray rgba;
  if (info.hbmColor) {
    const QByteArray color = readBits(dc, info.hbmColor, width, height);
    const QByteArray mask = info.hbmMask ? readBits(dc, info.hbmMask, width, height) : QByteArray();
    if (!color.isEmpty()) rgba = colorIconToRgba(color, mask, width, height);
  } else {
    rgba = monochromeIconToRgba(readBits(dc, info.hbmMask, width, height * 2), width, height);
  }
  DeleteDC(dc);
  if (rgba.isEmpty()) {
    data.iconError = info.hbmColor ? QStringLiteral("GetDIBits failed on the colour bitmap")
                                   : QStringLiteral("GetDIBits failed on the monochrome mask");
    return false;
  }
  data.iconRgba = rgba;
  data.iconWidth = width;
  data.iconHeight = height;
  data.iconError.clear();
  return true;
}

}  // namespace

bool extractIconRgba(TrayIconData& data) {
  if (data.hIcon == 0) {
    data.iconError = QStringLiteral("NIF_ICON without an icon handle");
    return false;
  }
  HICON copy = CopyIcon(reinterpret_cast<HICON>(static_cast<ULONG_PTR>(data.hIcon)));
  if (!copy) {
    data.iconError = QStringLiteral("the icon handle is no longer valid (CopyIcon failed)");
    return false;
  }
  ICONINFO info{};
  const bool haveInfo = GetIconInfo(copy, &info) != FALSE;
  DestroyIcon(copy);
  if (!haveInfo) {
    data.iconError = QStringLiteral("GetIconInfo failed");
    return false;
  }
  const bool ok = convertIconInfo(info, data);
  if (info.hbmMask) DeleteObject(info.hbmMask);
  if (info.hbmColor) DeleteObject(info.hbmColor);
  return ok;
}

}  // namespace wabar::tray
