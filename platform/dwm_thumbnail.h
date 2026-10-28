#pragma once

#include <QSize>
#include <memory>
#include <optional>

#include "platform/window_system.h"

namespace yasb::platform {

// One registered DWM thumbnail (DwmRegisterThumbnail): a live picture of `source` drawn inside the
// client area of `destination`. Unregistered on destruction.
class ThumbnailHandle {
 public:
  virtual ~ThumbnailHandle() = default;
  // The source window's size in physical pixels, when DWM can report it.
  virtual std::optional<QSize> sourceSize() const = 0;
  // Draws the whole source into (0, 0, size) of the destination, opaque and visible.
  virtual bool show(const QSize& physicalSize) = 0;
};

class ThumbnailBackend {
 public:
  virtual ~ThumbnailBackend() = default;
  // nullptr when DWM refuses (the window died between layout and registration).
  virtual std::unique_ptr<ThumbnailHandle> registerThumbnail(WindowHandle destination,
                                                             WindowHandle source) = 0;
};

// "Peek": fade every other window to show one. DwmpActivateLivePreview is exported by ordinal 113
// only (see docs/architecture.md); when the export is missing the backend reports unavailable and
// never does anything.
class LivePreviewBackend {
 public:
  virtual ~LivePreviewBackend() = default;
  virtual bool isAvailable() const = 0;
  // Fades the desktop to show `peek` (activate = true), or restores it. True when DWM accepted.
  virtual bool activate(bool activate, WindowHandle peek, WindowHandle caller) = 0;
  // Keeps one of our windows painted while peek fades the others (DWMWA_EXCLUDED_FROM_PEEK).
  virtual bool excludeFromPeek(WindowHandle hwnd) = 0;
};

std::unique_ptr<ThumbnailBackend> makeWin32ThumbnailBackend();
std::unique_ptr<LivePreviewBackend> makeWin32LivePreview();

}  // namespace yasb::platform
