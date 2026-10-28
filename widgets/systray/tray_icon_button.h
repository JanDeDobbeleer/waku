#pragma once

#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QPushButton>

#include "platform/systray_host.h"
#include "platform/systray_types.h"

namespace yasb::bar {
class GlobalState;
}

namespace yasb::widgets {

// Settings every icon button of a systray widget shares (Python keeps them as IconWidget class
// attributes).
struct TrayIconSettings {
  int iconSize = 16;
  bool enableTooltips = true;
  Qt::KeyboardModifier pinModifier = Qt::AltModifier;
};

// Port of IconWidget (services/systray/systray_widget.py): one tray icon as a flat button with
// CSS class "button". A click is forwarded to the icon's owner window as the mouse messages the
// shell would have sent; <modifier>+click pins or unpins; dragging moves it between containers.
class TrayIconButton : public QPushButton {
  Q_OBJECT
 public:
  TrayIconButton(const TrayIconSettings& settings, platform::TrayWindowApi* api,
                 bar::GlobalState* globalState);

  // Accumulated tray notification data; `hasData` is false until the first one is applied.
  platform::TrayIconData info;
  bool hasData = false;
  bool isPinned = false;

  // IconWidget.drag_in_progress (shared by all buttons: only one drag runs at a time).
  static bool dragInProgress() { return dragInProgress_; }

  // IconWidget.update_icon: tooltip text and the button's QIcon.
  void updateIcon();
  // The 32x32 image built from info.iconRgba (null when there is none).
  const QImage& iconImage() const { return image_; }
  void setIconImage(const QImage& image) { image_ = image; }
  void updateScaledPixmap();

  // Mouse actions sent to the owner window (WM_LBUTTONDOWN, ...). Public for tests.
  void sendAction(std::uint32_t action);

 signals:
  void pinnedChanged(TrayIconButton* icon);
  void iconMoved(TrayIconButton* icon);

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 private:
  void sendNotify(std::uint32_t message);

  TrayIconSettings settings_;
  platform::TrayWindowApi* api_;
  bar::GlobalState* globalState_;
  QImage image_;
  QPixmap scaledPixmap_;
  QPoint lastCursorPos_;
  bool lmbPressed_ = false;
  bool ignoreNextRelease_ = false;
  static inline bool dragInProgress_ = false;
};

}  // namespace yasb::widgets
