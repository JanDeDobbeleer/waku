#include "features/wallpapers/desktop_wallpaper.h"

// clang-format off
#include <windows.h>
#include <shlwapi.h>
#include <shobjidl_core.h>
// clang-format on

#include <QDebug>
#include <string>

#include "kernel/win32/com_scope.h"

namespace wabar::wallpapers {
namespace {

template <typename T>
class ComPtr {
 public:
  ComPtr() = default;
  ~ComPtr() {
    if (ptr_) ptr_->Release();
  }
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  T* operator->() const { return ptr_; }
  T** put() { return &ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  T* ptr_ = nullptr;
};

QString toQString(const wchar_t* text) { return text ? QString::fromWCharArray(text) : QString(); }

class ComBackend final : public DesktopWallpaperBackend {
 public:
  bool setWallpaper(const QString& monitorId, const QString& path, QString* error) override {
    const auto fail = [&](const QString& why) {
      if (error) *error = why;
      return false;
    };
    const kernel::ComScope com(kernel::ComApartment::SingleThreaded);
    ComPtr<IDesktopWallpaper> desktop;
    if (!create(desktop)) return fail(QStringLiteral("IDesktopWallpaper is unavailable"));
    const std::wstring monitor = monitorId.toStdWString();
    const std::wstring file = path.toStdWString();
    const HRESULT hr =
        desktop->SetWallpaper(monitor.empty() ? nullptr : monitor.c_str(), file.c_str());
    if (FAILED(hr)) {
      return fail(QStringLiteral("SetWallpaper failed (0x%1)")
                      .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
    }
    return true;
  }

  std::vector<WallpaperMonitor> monitors() override {
    std::vector<WallpaperMonitor> result;
    const kernel::ComScope com(kernel::ComApartment::SingleThreaded);
    ComPtr<IDesktopWallpaper> desktop;
    if (!create(desktop)) return result;
    UINT count = 0;
    if (FAILED(desktop->GetMonitorDevicePathCount(&count))) return result;
    for (UINT i = 0; i < count; ++i) {
      LPWSTR id = nullptr;
      if (FAILED(desktop->GetMonitorDevicePathAt(i, &id)) || !id) continue;
      WallpaperMonitor monitor;
      monitor.id = toQString(id);
      RECT rect{};
      if (SUCCEEDED(desktop->GetMonitorRECT(id, &rect))) {
        monitor.nativeRect =
            QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
      }
      CoTaskMemFree(id);
      result.push_back(std::move(monitor));
    }
    return result;
  }

  QString wallpaperOf(const QString& monitorId) override {
    const kernel::ComScope com(kernel::ComApartment::SingleThreaded);
    ComPtr<IDesktopWallpaper> desktop;
    if (!create(desktop)) return {};
    const std::wstring monitor = monitorId.toStdWString();
    LPWSTR path = nullptr;
    if (FAILED(desktop->GetWallpaper(monitor.c_str(), &path)) || !path) return {};
    const QString result = toQString(path);
    CoTaskMemFree(path);
    return result;
  }

 private:
  static bool create(ComPtr<IDesktopWallpaper>& out) {
    return SUCCEEDED(
        CoCreateInstance(__uuidof(DesktopWallpaper), nullptr, CLSCTX_ALL, IID_PPV_ARGS(out.put())));
  }
};

}  // namespace

std::unique_ptr<DesktopWallpaperBackend> makeDesktopWallpaperBackend() {
  return std::make_unique<ComBackend>();
}

WindowsBuild windowsBuildAndUbr() {
  WindowsBuild result;
  constexpr const wchar_t* kKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
  wchar_t buildText[32] = {};
  DWORD size = sizeof(buildText);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, kKey, L"CurrentBuildNumber", RRF_RT_REG_SZ, nullptr,
                   buildText, &size) == ERROR_SUCCESS) {
    result.build = _wtoi(buildText);
  }
  DWORD ubr = 0;
  size = sizeof(ubr);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, kKey, L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &size) ==
      ERROR_SUCCESS) {
    result.ubr = static_cast<int>(ubr);
  }
  return result;
}

int compareLogical(const QString& a, const QString& b) {
  return StrCmpLogicalW(reinterpret_cast<PCWSTR>(a.utf16()), reinterpret_cast<PCWSTR>(b.utf16()));
}

}  // namespace wabar::wallpapers
