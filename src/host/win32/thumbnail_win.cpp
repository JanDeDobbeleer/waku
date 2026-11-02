#include <windows.h>
#include <dwmapi.h>

#include <QSize>

#include "host/thumbnail.h"

namespace waku::host {

namespace {

// Documented APIs only: DwmRegisterThumbnail, DwmUnregisterThumbnail,
// DwmQueryThumbnailSourceSize, DwmUpdateThumbnailProperties. Handles are HTHUMBNAIL values.
class Win32ThumbnailBackend final : public ThumbnailBackend {
 public:
  quintptr registerThumbnail(quintptr destination, quintptr source) override {
    if (destination == 0 || source == 0) return 0;
    const HWND src = reinterpret_cast<HWND>(source);
    if (!IsWindow(src)) return 0;
    HTHUMBNAIL thumbnail = nullptr;
    if (FAILED(DwmRegisterThumbnail(reinterpret_cast<HWND>(destination), src, &thumbnail)) ||
        thumbnail == nullptr) {
      return 0;
    }
    return reinterpret_cast<quintptr>(thumbnail);
  }

  void unregisterThumbnail(quintptr handle) override {
    if (handle != 0) DwmUnregisterThumbnail(reinterpret_cast<HTHUMBNAIL>(handle));
  }

  QSize sourceSize(quintptr handle) override {
    if (handle == 0) return {};
    SIZE size{};
    if (FAILED(DwmQueryThumbnailSourceSize(reinterpret_cast<HTHUMBNAIL>(handle), &size))) {
      return {};
    }
    return QSize(size.cx, size.cy);
  }

  bool update(quintptr handle, const ThumbnailProps& props) override {
    if (handle == 0) return false;
    DWM_THUMBNAIL_PROPERTIES properties{};
    properties.dwFlags =
        DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    properties.rcDestination = RECT{props.destination.left(), props.destination.top(),
                                    props.destination.left() + props.destination.width(),
                                    props.destination.top() + props.destination.height()};
    properties.fVisible = props.visible ? TRUE : FALSE;
    properties.opacity = props.opacity;
    properties.fSourceClientAreaOnly = FALSE;
    return SUCCEEDED(
        DwmUpdateThumbnailProperties(reinterpret_cast<HTHUMBNAIL>(handle), &properties));
  }
};

}  // namespace

std::unique_ptr<ThumbnailBackend> makeWin32ThumbnailBackend() {
  return std::make_unique<Win32ThumbnailBackend>();
}

}  // namespace waku::host
