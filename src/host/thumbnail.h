#pragma once

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QtGlobal>
#include <memory>

namespace wabar::host {

// Live window previews with DWM thumbnails (the backend of the QML item WindowThumbnail).
//
// A DWM thumbnail is drawn by the compositor above the destination window's own content, so
// nothing in the destination window can overlay it and no parent clip applies to it.

// What the compositor needs to draw one thumbnail.
struct ThumbnailProps {
  QRect destination;  // in the destination window's client area, physical pixels
  bool visible = false;
  quint8 opacity = 255;
  bool operator==(const ThumbnailProps&) const = default;
};

// The thumbnail registrations of the operating system. The Windows one is
// makeWin32ThumbnailBackend(); tests use FakeThumbnailBackend (host/testing/thumbnail_fakes.h).
// GUI thread only. Every call fails soft: a closed or invalid window gives 0, an empty size or
// false, never an exception.
class ThumbnailBackend {
 public:
  virtual ~ThumbnailBackend() = default;
  // Registers `source` to be drawn in `destination` (both window handles). Returns a handle, or 0
  // when the registration failed (invalid handle, DWM off, no permission).
  virtual quintptr registerThumbnail(quintptr destination, quintptr source) = 0;
  // Releases a handle from registerThumbnail(). Handle 0 is ignored.
  virtual void unregisterThumbnail(quintptr handle) = 0;
  // The source window's size in physical pixels, or an empty size when it cannot be read.
  virtual QSize sourceSize(quintptr handle) = 0;
  // Applies `props`. Returns false when the thumbnail is gone (the source window closed).
  virtual bool update(quintptr handle, const ThumbnailProps& props) = 0;
};

// The Windows implementation (win32/thumbnail_win.cpp): DwmRegisterThumbnail,
// DwmUnregisterThumbnail, DwmQueryThumbnailSourceSize and DwmUpdateThumbnailProperties, all
// documented.
std::unique_ptr<ThumbnailBackend> makeWin32ThumbnailBackend();

// The rectangle of a thumbnail in the destination window's client area, in physical pixels.
// `sceneRect` is the item's rectangle in the window's device-independent units; it is multiplied
// by `devicePixelRatio`. With `preserveAspect` and a non-empty `source` the image is scaled to fit
// inside that rectangle and centred; otherwise it fills it.
QRect thumbnailDestination(const QRectF& sceneRect, qreal devicePixelRatio, const QSize& source,
                           bool preserveAspect);

}  // namespace wabar::host
