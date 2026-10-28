#include "widgets/systray/systray_popup.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>

#include "core/widgets/backdrop.h"
#include "platform/systray_win.h"

namespace yasb::widgets {

QPoint computeSystrayPopupPosition(const QPoint& buttonOrigin, const QSize& buttonSize,
                                   const QPoint& barOrigin, int barHeight, const QSize& popupSize,
                                   const std::optional<QRect>& screen, const QString& alignment,
                                   const QString& direction, int offsetLeft, int offsetTop) {
  int y = direction == QLatin1String("up") ? barOrigin.y() - popupSize.height() - offsetTop
                                           : barOrigin.y() + barHeight + offsetTop;
  int x = buttonOrigin.x() + offsetLeft;
  if (alignment == QLatin1String("right")) {
    x = buttonOrigin.x() + buttonSize.width() - popupSize.width() + offsetLeft;
  } else if (alignment == QLatin1String("center")) {
    // Python floors the division; integer // on a negative width difference rounds down.
    const int diff = buttonSize.width() - popupSize.width();
    const int half = diff >= 0 ? diff / 2 : -((-diff + 1) / 2);
    x = buttonOrigin.x() + half + offsetLeft;
  }
  if (screen) {
    x = std::max(screen->left(), std::min(x, screen->right() - popupSize.width()));
    y = std::max(screen->top(), std::min(y, screen->bottom() - popupSize.height()));
  }
  return {x, y};
}

SystrayPopup::SystrayPopup(QWidget* parent, QPushButton* toggleButton,
                           const SystrayPopupOptions& options, int iconsPerRow,
                           const QString& labelCollapsed)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                          Qt::NoDropShadowWindowHint),
      toggleButton_(toggleButton),
      options_(options),
      labelCollapsed_(labelCollapsed),
      content_(new QFrame(this)),
      grid_(new TrayDropWidget(nullptr, iconsPerRow)),
      fade_(new QPropertyAnimation(this, "windowOpacity", this)),
      watchTimer_(new QTimer(this)) {
  setAttribute(Qt::WA_TranslucentBackground);
  grid_->setProperty("class", QStringLiteral("systray-popup"));

  auto* outer = new QVBoxLayout;
  outer->setSpacing(0);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->addWidget(grid_);
  setLayout(outer);

  fade_->setDuration(80);
  connect(fade_, &QPropertyAnimation::finished, this, &SystrayPopup::onAnimationFinished);

  // Native mouse poll while a context menu has taken the activation.
  watchTimer_->setInterval(60);
  connect(watchTimer_, &QTimer::timeout, this, &SystrayPopup::onWatchTick);
  connect(this, &SystrayPopup::closed, this, &SystrayPopup::onPopupClosed);
}

void SystrayPopup::toggle(const QString& labelExpanded) {
  if (closing_) return;
  if (isOpen())
    hideAnimated();
  else
    open(labelExpanded);
}

void SystrayPopup::open(const QString& labelExpanded) {
  grid_->relayoutGrid();
  show();
  grid_->updateGeometry();
  adjustSize();
  setPosition({options_.alignment, options_.direction, options_.offsetLeft, options_.offsetTop});
  toggleButton_->setChecked(true);
  toggleButton_->setText(labelExpanded);
}

void SystrayPopup::relayoutGrid() {
  grid_->relayoutGrid();
  if (isVisible()) {
    grid_->updateGeometry();
    adjustSize();
  }
}

void SystrayPopup::sortUnpinned(const std::vector<TrayIconButton*>& sorted) {
  grid_->relayoutGrid(&sorted);
  if (isVisible()) {
    grid_->updateGeometry();
    adjustSize();
  }
}

void SystrayPopup::setPosition(const PositionArgs& args) {
  positionArgs_ = args;
  QWidget* bar = parentWidget();
  if (!bar) return;
  std::optional<QRect> screenRect;
  if (const QScreen* screen =
          QApplication::screenAt(toggleButton_->mapToGlobal(toggleButton_->rect().center()))) {
    screenRect = screen->geometry();
  }
  move(computeSystrayPopupPosition(toggleButton_->mapToGlobal(QPoint(0, 0)), toggleButton_->size(),
                                   bar->mapToGlobal(QPoint(0, 0)), bar->height(), size(),
                                   screenRect, args.alignment, args.direction, args.offsetLeft,
                                   args.offsetTop));
}

QRect SystrayPopup::popupRect() const { return QRect(mapToGlobal(QPoint(0, 0)), size()); }

void SystrayPopup::showEvent(QShowEvent* event) {
  if (options_.blur) {
    enableBlur(winId(), false, options_.roundCorners, options_.roundCornersType,
               options_.borderColor);
  }
  closing_ = false;
  if (fade_->state() == QAbstractAnimation::Running) fade_->stop();
  setWindowOpacity(0.0);
  QWidget::showEvent(event);
  activateWindow();

  fade_->setStartValue(0.0);
  fade_->setEndValue(1.0);
  fade_->start();
}

void SystrayPopup::hideAnimated() {
  if (closing_) return;
  // The button follows at once, not when the fade ends.
  toggleButton_->setChecked(false);
  if (!labelCollapsed_.isEmpty()) toggleButton_->setText(labelCollapsed_);
  if (fade_->state() == QAbstractAnimation::Running) fade_->stop();

  qreal opacity = windowOpacity();
  if (opacity <= 0.0) {
    opacity = 1.0;
    setWindowOpacity(1.0);
  }
  closing_ = true;
  fade_->setStartValue(opacity);
  fade_->setEndValue(0.0);
  fade_->start();
}

void SystrayPopup::onAnimationFinished() {
  if (closing_) {
    QWidget::hide();
    closing_ = false;
  }
}

void SystrayPopup::hideEvent(QHideEvent* event) {
  watchTimer_->stop();
  QWidget::hideEvent(event);
  emit closed();
}

void SystrayPopup::closeEvent(QCloseEvent* event) {
  event->ignore();
  hideAnimated();
}

void SystrayPopup::onPopupClosed() {
  toggleButton_->setChecked(false);
  if (!labelCollapsed_.isEmpty()) toggleButton_->setText(labelCollapsed_);
}

void SystrayPopup::resizeEvent(QResizeEvent* event) {
  content_->setGeometry(0, 0, width(), height());
  if (positionArgs_) setPosition(*positionArgs_);
  QWidget::resizeEvent(event);
}

void SystrayPopup::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() != QEvent::ActivationChange || closing_) return;
  if (isActiveWindow()) {
    watchTimer_->stop();
  } else if (TrayIconButton::dragInProgress()) {
    // Closing while a drag runs would lose the drop target.
  } else if (toggleButton_ &&
             toggleButton_->rect().contains(toggleButton_->mapFromGlobal(QCursor::pos()))) {
    // The click was on the toggle button: its clicked handler decides.
  } else if (popupRect().contains(QCursor::pos())) {
    // A context menu of an icon took the activation; keep the popup while it is used.
    watchTicks_ = 0;
    watchTimer_->start();
  } else {
    hideAnimated();
  }
}

void SystrayPopup::onWatchTick() {
  ++watchTicks_;
  if (closing_ || isActiveWindow() || TrayIconButton::dragInProgress()) {
    watchTimer_->stop();
    return;
  }
  if (watchTicks_ >= 3 && platform::anyMouseButtonDown()) {
    if (!popupRect().contains(QCursor::pos())) {
      watchTimer_->stop();
      hideAnimated();
    }
  }
}

}  // namespace yasb::widgets
