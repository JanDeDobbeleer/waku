#pragma once

#include <QtQml/qqmlregistration.h>

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSize>
#include <QTimer>

#include "host/thumbnail.h"

namespace waku::host {

// A live preview of another window, drawn by the compositor (a DWM thumbnail).
//
//     WindowThumbnail { hwnd: 12345; width: 320; height: 180 }
//
//   hwnd          the source window (HWND as a number); 0 shows nothing
//   active        false unregisters the thumbnail, so no preview is kept (default true)
//   fit           PreserveAspectFit (default) scales the window to fit inside the item and centres
//                 it; Stretch fills the item
//   sourceWidth, sourceHeight  the source window's size in physical pixels; 0 when unknown
//   available     true while the thumbnail is registered and the source window exists
//   opacity       the item's effective opacity (its own times its ancestors') is applied
//
// Limits, all because the compositor draws the thumbnail above the destination window:
//   - QML content cannot overlay it. Put nothing on top of a WindowThumbnail that must be seen.
//   - A parent's `clip` does not clip it. Only the item's own rectangle bounds it.
//   - It is not part of the scene graph: grabToImage() and ShaderEffectSource do not see it.
// When `available` is false the item draws nothing; the shell shows its own fallback.
//
// Inert (no DWM call) when kernel::liveDesktop() is false. Changes of geometry, visibility,
// opacity, the device pixel ratio or the window update the thumbnail on the next event loop pass;
// a frame in which an ancestor moves does too. A one-second timer refreshes the source size and
// registers again after a failure. Threading: GUI thread only.
class WindowThumbnail : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(qlonglong hwnd READ hwnd WRITE setHwnd NOTIFY hwndChanged FINAL)
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
  Q_PROPERTY(Fit fit READ fit WRITE setFit NOTIFY fitChanged FINAL)
  Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY sourceSizeChanged FINAL)
  Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY sourceSizeChanged FINAL)
  Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)

 public:
  enum Fit { PreserveAspectFit, Stretch };
  Q_ENUM(Fit)

  explicit WindowThumbnail(QQuickItem* parent = nullptr);
  ~WindowThumbnail() override;

  qlonglong hwnd() const { return hwnd_; }
  void setHwnd(qlonglong hwnd);
  bool active() const { return active_; }
  void setActive(bool active);
  Fit fit() const { return fit_; }
  void setFit(Fit fit);
  int sourceWidth() const { return source_.width(); }
  int sourceHeight() const { return source_.height(); }
  bool available() const { return available_; }

  // Brings the registration and the properties up to date now. What the queued update calls;
  // tests call it instead of waiting for the event loop.
  void syncNow();

  // Tests install a backend; nullptr restores the default (the Windows one on a live desktop,
  // none otherwise). The backend must outlive every item.
  static void setBackendForTesting(ThumbnailBackend* backend);

 signals:
  void hwndChanged();
  void activeChanged();
  void fitChanged();
  void sourceSizeChanged();
  void availableChanged();

 protected:
  void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
  void itemChange(ItemChange change, const ItemChangeData& value) override;

 private:
  void scheduleSync();
  void sync(bool querySize);
  void release();
  void setSource(const QSize& size);
  void setAvailable(bool available);
  void watchWindow(QQuickWindow* window);
  qreal effectiveOpacity() const;

  qlonglong hwnd_ = 0;
  bool active_ = true;
  Fit fit_ = PreserveAspectFit;
  QSize source_{0, 0};
  bool available_ = false;

  ThumbnailBackend* backend_ = nullptr;  // the backend that issued handle_
  quintptr handle_ = 0;
  quintptr registeredIn_ = 0;  // the destination window handle of handle_
  ThumbnailProps lastProps_;
  bool hasProps_ = false;
  bool syncQueued_ = false;
  QPointer<QQuickWindow> watched_;
  QTimer refresh_;
};

}  // namespace waku::host
