#include "features/wallpapers/win32/workerw.h"

#include <windows.h>

#include <QByteArray>
#include <string>

namespace waku::wallpapers {
namespace {

constexpr const wchar_t* kDesktopKey = L"Control Panel\\Desktop";

// Message to Progman that makes it create the WorkerW window behind the icons (undocumented).
constexpr UINT kSpawnWorker = 0x052C;

std::optional<std::wstring> readString(const wchar_t* key, const wchar_t* name) {
  wchar_t text[64] = {};
  DWORD size = sizeof(text);
  if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, text, &size) !=
      ERROR_SUCCESS) {
    return std::nullopt;
  }
  return std::wstring(text);
}

std::optional<DWORD> readDword(const wchar_t* key, const wchar_t* name) {
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &value, &size) !=
      ERROR_SUCCESS) {
    return std::nullopt;
  }
  return value;
}

// Strings written as REG_SZ ("1", "10") or, rarely, as a DWORD; both read as text here.
std::optional<std::wstring> readValueText(const wchar_t* key, const wchar_t* name) {
  wchar_t text[64] = {};
  DWORD size = sizeof(text);
  if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, text, &size) ==
      ERROR_SUCCESS) {
    return std::wstring(text);
  }
  if (const auto number = readDword(key, name)) return std::to_wstring(*number);
  return std::nullopt;
}

HWND findWorkerW(HWND progman) {
  HWND worker = nullptr;
  EnumChildWindows(
      progman,
      [](HWND hwnd, LPARAM param) -> BOOL {
        wchar_t name[32] = {};
        GetClassNameW(hwnd, name, 32);
        if (std::wstring(name) == L"WorkerW") {
          *reinterpret_cast<HWND*>(param) = hwnd;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&worker));
  if (worker) return worker;

  // Windows 10 and older Windows 11 builds: WorkerW is a top-level sibling after the window that
  // holds SHELLDLL_DefView.
  EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        if (FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr)) {
          if (HWND candidate = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr)) {
            *reinterpret_cast<HWND*>(param) = candidate;
            return FALSE;
          }
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&worker));
  return worker;
}

HWND locateWorkerW(QString* failure) {
  HWND progman = FindWindowW(L"Progman", nullptr);
  if (!progman) {
    if (failure) *failure = QStringLiteral("Could not locate Progman");
    return nullptr;
  }
  DWORD_PTR ignored = 0;
  SendMessageTimeoutW(progman, kSpawnWorker, 0, 0, SMTO_NORMAL, 1000, &ignored);
  HWND worker = findWorkerW(progman);
  if (!worker) {
    if (failure) {
      *failure = QStringLiteral("Could not locate WorkerW (Windows OS animation must be enabled)");
    }
    return nullptr;
  }
  ShowWindow(worker, SW_SHOW);
  return worker;
}

}  // namespace

WallpaperFit readWallpaperFit() {
  const auto tile = readValueText(kDesktopKey, L"TileWallpaper");
  const auto style = readValueText(kDesktopKey, L"WallpaperStyle");
  // Python reads both values in one try block: either one missing means "fill".
  if (!tile || !style) return WallpaperFit::Fill;
  if (*tile == L"1") return WallpaperFit::Tile;
  const std::wstring& value = *style;
  if (value == L"0") return WallpaperFit::Center;
  if (value == L"2") return WallpaperFit::Stretch;
  if (value == L"6") return WallpaperFit::Fit;
  if (value == L"10") return WallpaperFit::Fill;
  if (value == L"22") return WallpaperFit::Span;
  return WallpaperFit::Fill;
}

std::uint32_t readDesktopBackgroundRgb() {
  const auto text = readString(L"Control Panel\\Colors", L"Background");
  if (!text) return 0;
  int r = 0;
  int g = 0;
  int b = 0;
  if (swscanf_s(text->c_str(), L"%d %d %d", &r, &g, &b) != 3) return 0;
  const auto clamp = [](int v) {
    return static_cast<std::uint32_t>(v < 0 ? 0 : v > 255 ? 255 : v);
  };
  return (clamp(r) << 16) | (clamp(g) << 8) | clamp(b);
}

std::optional<std::pair<int, int>> readTranscodeLimits() {
  const auto virtualDesktop = readDword(kDesktopKey, L"MaxVirtualDesktopDimension");
  const auto monitor = readDword(kDesktopKey, L"MaxMonitorDimension");
  if (!virtualDesktop || !monitor) return std::nullopt;
  const int a = static_cast<int>(*virtualDesktop);
  const int b = static_cast<int>(*monitor);
  if (a > 0 && b > 0) return std::make_pair(a, b);
  return std::nullopt;
}

double readPanoramaThreshold(bool landscape) {
  const double fallback = landscape ? 2.22 : 1.0;
  const auto value =
      readDword(kDesktopKey, landscape ? L"PanoramaThreshold" : L"PanoramaPortraitThreshold");
  if (!value || *value == 0) return fallback;
  return static_cast<double>(*value) / 1000.0;
}

QString transcodedWallpaperPath() {
  const QByteArray appData = qgetenv("APPDATA");
  return QString::fromLocal8Bit(appData) +
         QStringLiteral("\\Microsoft\\Windows\\Themes\\TranscodedWallpaper");
}

std::optional<WorkerWAttachment> attachToWorkerW(std::uintptr_t handle, QString* failure) {
  HWND worker = locateWorkerW(failure);
  if (!worker) return std::nullopt;
  HWND hwnd = reinterpret_cast<HWND>(handle);

  // Reparenting a layered window fails; drop WS_EX_LAYERED around SetParent.
  const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
  SetParent(hwnd, worker);
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  SetWindowLongPtrW(hwnd, GWL_STYLE,
                    (style | static_cast<LONG_PTR>(WS_CHILD)) & ~static_cast<LONG_PTR>(WS_POPUP));

  RECT workerRect{};
  GetWindowRect(worker, &workerRect);

  WorkerWAttachment attachment;
  attachment.workerRect = QRect(workerRect.left, workerRect.top, workerRect.right - workerRect.left,
                                workerRect.bottom - workerRect.top);
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR, HDC, LPRECT rect, LPARAM param) -> BOOL {
        auto* list = reinterpret_cast<std::vector<QRect>*>(param);
        list->push_back(
            QRect(rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top));
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&attachment.monitors));

  SetWindowPos(hwnd, HWND_TOP, 0, 0, attachment.workerRect.width(), attachment.workerRect.height(),
               SWP_NOACTIVATE | SWP_FRAMECHANGED);
  return attachment;
}

void detachFromWorkerW(std::uintptr_t handle) {
  HWND hwnd = reinterpret_cast<HWND>(handle);
  // Gone with WorkerW when Explorer restarted; nothing to undo then.
  if (!hwnd || !IsWindow(hwnd)) return;
  const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  SetWindowLongPtrW(hwnd, GWL_STYLE,
                    (style & ~static_cast<LONG_PTR>(WS_CHILD)) | static_cast<LONG_PTR>(WS_POPUP));
  SetParent(hwnd, nullptr);
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
}

}  // namespace waku::wallpapers
