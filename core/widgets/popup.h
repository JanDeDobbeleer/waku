#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

namespace yasb::widgets {

// Pure port of PopupWidget.setPosition. `parentTopLeft` is the anchor widget's global top-left.
// All values are logical pixels; the result is clamped into `screen` (QRect::right()/bottom() semantics).
QPoint computePopupPosition(const QPoint& parentTopLeft, const QSize& parentSize, const QSize& popupSize,
                            const QRect& screen, const QString& alignment, const QString& direction, int offsetLeft,
                            int offsetTop);

// Frameless tool window anchored to a bar widget; position is recomputed when it resizes.
class PopupWidget : public QWidget {
  Q_OBJECT
 public:
  explicit PopupWidget(QWidget* anchor);

  void setPosition(const QString& alignment = QStringLiteral("left"), const QString& direction = QStringLiteral("down"),
                   int offsetLeft = 0, int offsetTop = 0);
  void setAutoClose(bool enabled) { m_autoClose = enabled; }

 protected:
  void resizeEvent(QResizeEvent* event) override;
  bool event(QEvent* event) override;

 private:
  QWidget* m_anchor;
  QString m_alignment = QStringLiteral("left");
  QString m_direction = QStringLiteral("down");
  int m_offsetLeft = 0;
  int m_offsetTop = 0;
  bool m_positioned = false;
  bool m_autoClose = true;
};

}  // namespace yasb::widgets
