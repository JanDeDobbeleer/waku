#include "widgets/menu_support/menu_popup.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>

#include "core/widgets/backdrop.h"
#include "core/widgets/base_widget.h"
#include "core/widgets/popup.h"

namespace yasb::widgets {

MenuPopup::MenuPopup(QWidget* anchor, bool blur, bool roundCorners, const QString& roundCornersType,
                     const QString& borderColor, bool darkMode, bool persistent, int animationMs)
    : QWidget(anchor, Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                          Qt::NoDropShadowWindowHint),
      anchor_(anchor),
      content_(new QFrame(this)),
      blur_(blur),
      roundCorners_(roundCorners),
      roundCornersType_(roundCornersType),
      borderColor_(borderColor),
      darkMode_(darkMode),
      persistent_(persistent),
      fade_(new QPropertyAnimation(this, "windowOpacity", this)) {
  setAttribute(Qt::WA_TranslucentBackground);
  fade_->setDuration(animationMs);
  connect(fade_, &QPropertyAnimation::finished, this, &MenuPopup::onFadeFinished);
}

MenuPopup::~MenuPopup() {
  if (QApplication::instance()) qApp->removeEventFilter(this);
}

void MenuPopup::setCssClass(const QString& cssClass) {
  setProperty("class", cssClass);
  content_->setProperty("class", cssClass);
}

void MenuPopup::setPosition(const QString& alignment, const QString& direction, int offsetLeft,
                            int offsetTop) {
  alignment_ = alignment;
  direction_ = direction;
  offsetLeft_ = offsetLeft;
  offsetTop_ = offsetTop;
  positioned_ = true;
  if (!anchor_) return;
  const QScreen* screen = QApplication::screenAt(anchor_->mapToGlobal(anchor_->rect().center()));
  if (!screen) return;
  move(computePopupPosition(anchor_->mapToGlobal(QPoint(0, 0)), anchor_->size(), size(),
                            screen->geometry(), alignment, direction, offsetLeft, offsetTop));
}

MenuPopup* MenuPopup::openFor(QWidget* anchor) {
  if (!anchor) return nullptr;
  const auto popups = anchor->findChildren<MenuPopup*>(QString(), Qt::FindDirectChildrenOnly);
  for (MenuPopup* popup : popups) {
    if (popup->isVisible() && !popup->closing_) return popup;
  }
  return nullptr;
}

bool MenuPopup::showToggled() {
  if (MenuPopup* other = openFor(anchor_); other && other != this) {
    other->hideAnimated();
    return false;
  }
  show();
  return true;
}

void MenuPopup::hideAnimated() {
  if (closing_) return;
  qreal opacity = windowOpacity();
  if (fade_->state() == QAbstractAnimation::Running) fade_->stop();
  if (opacity <= 0.0) {
    opacity = 1.0;
    setWindowOpacity(1.0);
  }
  closing_ = true;
  fade_->setEasingCurve(QEasingCurve::InCubic);
  fade_->setStartValue(opacity);
  fade_->setEndValue(0.0);
  fade_->start();
}

void MenuPopup::hideNow() {
  if (fade_->state() == QAbstractAnimation::Running) fade_->stop();
  closing_ = true;
  QWidget::hide();
  if (!persistent_) deleteLater();
}

void MenuPopup::onFadeFinished() {
  if (!closing_) return;
  QWidget::hide();
  if (!persistent_) deleteLater();
}

void MenuPopup::showEvent(QShowEvent* event) {
  qApp->installEventFilter(this);
  // Clear a stuck :hover state on the anchor's own content.
  if (auto* base = qobject_cast<BaseWidget*>(anchor_)) base->clearHoverState();
  if (blur_) {
    enableBlur(winId(), darkMode_, roundCorners_, roundCornersType_, borderColor_);
  }
  closing_ = false;
  if (fade_->state() == QAbstractAnimation::Running) fade_->stop();
  setWindowOpacity(0.0);
  QWidget::showEvent(event);
  activateWindow();
  fade_->setEasingCurve(QEasingCurve::OutCubic);
  fade_->setStartValue(0.0);
  fade_->setEndValue(1.0);
  fade_->start();
}

void MenuPopup::hideEvent(QHideEvent* event) {
  if (closing_) qApp->removeEventFilter(this);
  QWidget::hideEvent(event);
}

void MenuPopup::closeEvent(QCloseEvent* event) {
  event->ignore();
  hideAnimated();
}

void MenuPopup::resizeEvent(QResizeEvent* event) {
  content_->setGeometry(0, 0, width(), height());
  if (positioned_) setPosition(alignment_, direction_, offsetLeft_, offsetTop_);
  QWidget::resizeEvent(event);
}

bool MenuPopup::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() != QEvent::MouseButtonPress || !isVisible() || closing_)
    return QWidget::eventFilter(watched, event);
  const QPoint globalPos = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
  if (QRect(mapToGlobal(QPoint(0, 0)), size()).contains(globalPos))
    return QWidget::eventFilter(watched, event);
  // Clicks inside a visible QMenu or QDialog (context menu, file dialogs) belong to them.
  for (QWidget* top : QApplication::topLevelWidgets()) {
    if ((qobject_cast<QMenu*>(top) || qobject_cast<QDialog*>(top)) && top->isVisible() &&
        top->frameGeometry().contains(globalPos)) {
      return QWidget::eventFilter(watched, event);
    }
  }
  for (QMenu* menu : findChildren<QMenu*>()) {
    if (menu->isVisible()) menu->close();
  }
  hideAnimated();
  return true;
}

}  // namespace yasb::widgets
