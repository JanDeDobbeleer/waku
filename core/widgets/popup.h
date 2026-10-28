#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

namespace yasb::widgets {

// Pure port of PopupWidget.setPosition. `parentTopLeft` is the anchor widget's global top-left.
// All values are logical pixels; the result is clamped into `screen` (QRect::right()/bottom()
// semantics).
QPoint computePopupPosition(const QPoint& parentTopLeft, const QSize& parentSize,
                            const QSize& popupSize, const QRect& screen, const QString& alignment,
                            const QString& direction, int offsetLeft, int offsetTop);

// Frameless popup window anchored to a bar widget; position is recomputed when it resizes. It is a
// Qt::Popup like Python's non-pinnable PopupWidget, so Qt grabs the mouse and a click anywhere
// outside dismisses it (the click on the anchor only closes it). Pinnable subclasses switch to
// Qt::Tool and close on WindowDeactivate unless pinned.
class PopupWidget : public QWidget {
  Q_OBJECT
 public:
  explicit PopupWidget(QWidget* anchor);

  void setPosition(const QString& alignment = QStringLiteral("left"),
                   const QString& direction = QStringLiteral("down"), int offsetLeft = 0,
                   int offsetTop = 0);
  void setAutoClose(bool enabled) { autoClose_ = enabled; }

 protected:
  void resizeEvent(QResizeEvent* event) override;
  bool event(QEvent* event) override;

 private:
  QWidget* anchor_;
  QString alignment_ = QStringLiteral("left");
  QString direction_ = QStringLiteral("down");
  int offsetLeft_ = 0;
  int offsetTop_ = 0;
  bool positioned_ = false;
  bool autoClose_ = true;
};

}  // namespace yasb::widgets
