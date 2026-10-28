#include "platform/dwm_thumbnail.h"

#include <windows.h>
#include <dwmapi.h>

#include <QDebug>

namespace yasb::platform {
namespace {

HWND toHwnd(WindowHandle hwnd) { return reinterpret_cast<HWND>(hwnd); }

class Win32Thumbnail final : public ThumbnailHandle {
 public:
  explicit Win32Thumbnail(HTHUMBNAIL handle) : handle_(handle) {}
  ~Win32Thumbnail() override {
    if (handle_) DwmUnregisterThumbnail(handle_);
  }

  std::optional<QSize> sourceSize() const override {
    SIZE size = {};
    if (SUCCEEDED(DwmQueryThumbnailSourceSize(handle_, &size)) && size.cx > 0 && size.cy > 0)
      return QSize(size.cx, size.cy);
    return std::nullopt;
  }

  bool show(const QSize& physicalSize) override {
    DWM_THUMBNAIL_PROPERTIES props = {};
    props.dwFlags =
        DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    props.rcDestination = RECT{0, 0, qMax(1, physicalSize.width()), qMax(1, physicalSize.height())};
    props.opacity = 255;
    props.fVisible = TRUE;
    props.fSourceClientAreaOnly = FALSE;
    return SUCCEEDED(DwmUpdateThumbnailProperties(handle_, &props));
  }

 private:
  HTHUMBNAIL handle_;
};

class Win32ThumbnailBackend final : public ThumbnailBackend {
 public:
  std::unique_ptr<ThumbnailHandle> registerThumbnail(WindowHandle destination,
                                                     WindowHandle source) override {
    HTHUMBNAIL handle = nullptr;
    if (FAILED(DwmRegisterThumbnail(toHwnd(destination), toHwnd(source), &handle)) || !handle)
      return nullptr;
    return std::make_unique<Win32Thumbnail>(handle);
  }
};

// DwmpActivateLivePreview(BOOL activate, HWND peek, HWND caller, UINT trigger, RECT* rect) with the
// trigger LPT_SUPERBAR (3). Exported by ordinal 113 and absent from some builds.
class Win32LivePreview final : public LivePreviewBackend {
 public:
  Win32LivePreview() {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
      activate_ = reinterpret_cast<ActivateFn>(GetProcAddress(dwm, MAKEINTRESOURCEA(113)));
    }
    if (!activate_) qWarning() << "dwmapi ordinal 113 is not exported; taskbar peek is disabled";
  }

  bool isAvailable() const override { return activate_ != nullptr; }

  bool activate(bool on, WindowHandle peek, WindowHandle caller) override {
    if (!activate_) return false;
    constexpr UINT kTriggerSuperbar = 3;
    const HRESULT result =
        activate_(on ? TRUE : FALSE, toHwnd(peek), toHwnd(caller), kTriggerSuperbar, nullptr);
    if (FAILED(result)) {
      qWarning().noquote() << "DwmpActivateLivePreview failed with"
                           << QString::number(static_cast<quint32>(result), 16);
      return false;
    }
    return true;
  }

  bool excludeFromPeek(WindowHandle hwnd) override {
    constexpr DWORD kDwmwaExcludedFromPeek = 12;
    const BOOL value = TRUE;
    const HRESULT result =
        DwmSetWindowAttribute(toHwnd(hwnd), kDwmwaExcludedFromPeek, &value, sizeof(value));
    return SUCCEEDED(result);
  }

 private:
  using ActivateFn = HRESULT(WINAPI*)(BOOL, HWND, HWND, UINT, RECT*);
  ActivateFn activate_ = nullptr;
};

}  // namespace

std::unique_ptr<ThumbnailBackend> makeWin32ThumbnailBackend() {
  return std::make_unique<Win32ThumbnailBackend>();
}

std::unique_ptr<LivePreviewBackend> makeWin32LivePreview() {
  return std::make_unique<Win32LivePreview>();
}

}  // namespace yasb::platform
