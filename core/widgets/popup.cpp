#include "popup.h"

#include <QApplication>
#include <QEvent>
#include <QScreen>
#include <algorithm>

namespace yasb::widgets {

QPoint computePopupPosition(const QPoint& parentTopLeft, const QSize& parentSize,
                            const QSize& popupSize, const QRect& screen, const QString& alignment,
                            const QString& direction, int offsetLeft, int offsetTop) {
  QPoint anchor(parentTopLeft.x() + offsetLeft,
                parentTopLeft.y() + parentSize.height() + offsetTop);
  if (direction == QLatin1String("up")) {
    anchor =
        QPoint(parentTopLeft.x() + offsetLeft, parentTopLeft.y() - popupSize.height() - offsetTop);
  }

  QPoint pos = anchor;
  if (alignment == QLatin1String("right")) {
    pos.setX(anchor.x() + parentSize.width() - popupSize.width());
  } else if (alignment == QLatin1String("center")) {
    // Python floor division: matters for negative differences.
    const int diff = parentSize.width() - popupSize.width();
    const int half = diff >= 0 ? diff / 2 : -((-diff + 1) / 2);
    pos.setX(anchor.x() + half);
  }

  const int x = std::max(screen.left(), std::min(pos.x(), screen.right() - popupSize.width()));
  const int y = std::max(screen.top(), std::min(pos.y(), screen.bottom() - popupSize.height()));
  return {x, y};
}

PopupWidget::PopupWidget(QWidget* anchor)
    : QWidget(anchor, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      m_anchor(anchor) {
  setAttribute(Qt::WA_TranslucentBackground);
  setProperty("class", "popup");
}

void PopupWidget::setPosition(const QString& alignment, const QString& direction, int offsetLeft,
                              int offsetTop) {
  m_alignment = alignment;
  m_direction = direction;
  m_offsetLeft = offsetLeft;
  m_offsetTop = offsetTop;
  m_positioned = true;
  if (!m_anchor) return;

  const QPoint topLeft = m_anchor->mapToGlobal(QPoint(0, 0));
  const QScreen* screen = QApplication::screenAt(m_anchor->mapToGlobal(m_anchor->rect().center()));
  if (!screen) screen = QApplication::primaryScreen();
  move(computePopupPosition(topLeft, m_anchor->size(), size(), screen->geometry(), alignment,
                            direction, offsetLeft, offsetTop));
}

void PopupWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (m_positioned) setPosition(m_alignment, m_direction, m_offsetLeft, m_offsetTop);
}

bool PopupWidget::event(QEvent* e) {
  if (e->type() == QEvent::WindowDeactivate && m_autoClose && isVisible()) {
    hide();
  }
  return QWidget::event(e);
}

}  // namespace yasb::widgets
