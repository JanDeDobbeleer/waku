#include "core/widgets/tooltip.h"

#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QScreen>
#include <algorithm>

#include "core/bar/bar_window.h"
#include "core/bar/theme_class.h"
#include "core/widgets/backdrop.h"

namespace yasb::widgets {
namespace {
TooltipOptions& options() {
  static TooltipOptions o;
  return o;
}
}  // namespace

void setTooltipOptions(const TooltipOptions& o) { options() = o; }
const TooltipOptions& tooltipOptions() { return options(); }

QPoint computeTooltipPosition(const QRect& widgetGeometry, const QRect& topLevelGeometry,
                              const QSize& tipSize, const QRect& screen, const QString& position,
                              int offset) {
  int x = widgetGeometry.center().x() - tipSize.width() / 2;
  const int below = screen.bottom() - topLevelGeometry.bottom();
  const int above = topLevelGeometry.top() - screen.top();
  const int needed = tipSize.height() + offset;
  const int yBelow = topLevelGeometry.bottom() + offset;
  const int yAbove = topLevelGeometry.top() - tipSize.height() - offset;

  int y;
  if (position == QLatin1String("top")) {
    y = above >= needed ? yAbove : yBelow;
  } else if (position == QLatin1String("bottom")) {
    y = below >= needed ? yBelow : yAbove;
  } else if (below >= needed) {
    y = yBelow;
  } else if (above >= needed) {
    y = yAbove;
  } else {
    y = yBelow;
  }

  x = std::max(screen.left(), std::min(x, screen.right() - tipSize.width()));
  y = std::max(screen.top(), std::min(y, screen.bottom() - tipSize.height()));
  return {x, y};
}

CustomToolTip::CustomToolTip(QWidget* parent) : QFrame(parent) {
  setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);

  label_ = new QLabel;
  label_->setProperty("class",
                      yasb::bar::GlobalState::instance().isDark() ? "tooltip dark" : "tooltip");
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(label_);

  hideTimer_.setSingleShot(true);
  connect(&hideTimer_, &QTimer::timeout, this, &CustomToolTip::startFadeOut);

  auto make = [this](const char* prop, qreal from, qreal to, QEasingCurve::Type curve) {
    auto* a = new QPropertyAnimation(this, prop, this);
    a->setDuration(200);
    a->setStartValue(from);
    a->setEndValue(to);
    a->setEasingCurve(curve);
    return a;
  };
  fadeIn_ = make("opacity", 0, 1, QEasingCurve::OutCubic);
  fadeOut_ = make("opacity", 1, 0, QEasingCurve::InCubic);
  slideIn_ = make("slideOffset", 10, 0, QEasingCurve::OutCubic);
  slideOut_ = make("slideOffset", 0, 10, QEasingCurve::InCubic);
  connect(fadeOut_, &QPropertyAnimation::finished, this, [this] {
    fadeOutDone_ = true;
    checkHideComplete();
  });
  connect(slideOut_, &QPropertyAnimation::finished, this, [this] {
    slideOutDone_ = true;
    checkHideComplete();
  });
}

void CustomToolTip::setOpacity(qreal value) {
  opacity_ = value;
  setWindowOpacity(value);
}

void CustomToolTip::setSlideOffset(int value) {
  slideOffset_ = value;
  if (hasBasePos_) move(basePos_.x(), basePos_.y() + value);
}

void CustomToolTip::updateContent(const QString& text) {
  if (label_->text() == text) return;
  label_->setText(text);
  adjustSize();
  if (isVisible() && hasBasePos_) move(basePos_.x(), basePos_.y() + slideOffset_);
}

void CustomToolTip::showTooltip(const QString& text, const QRect& widgetGeometry,
                                const QRect& topLevelGeometry, int durationMs) {
  if (active_ && active_ != this) active_->hide();
  active_ = this;

  label_->setText(text);
  adjustSize();
  if (widgetGeometry.isNull()) return;

  const QRect top = topLevelGeometry.isNull() ? widgetGeometry : topLevelGeometry;
  const QScreen* screen = QGuiApplication::screenAt(widgetGeometry.center());
  if (!screen) screen = QGuiApplication::primaryScreen();
  basePos_ = computeTooltipPosition(widgetGeometry, top, size(), screen->geometry(), position_,
                                    options().offset);
  hasBasePos_ = true;
  move(basePos_);

  startAnimations(true);
  show();

  const TooltipBlur& blur = options().blur;
  if (blur.enabled)
    enableBlur(winId(), blur.darkMode, blur.roundCorners, blur.roundCornersType, blur.borderColor);
  if (durationMs > 0) hideTimer_.start(durationMs);
}

void CustomToolTip::startFadeOut() { startAnimations(false); }

void CustomToolTip::hideActive() {
  if (active_) active_->hide();
}

void CustomToolTip::startAnimations(bool fadeIn) {
  if (fadeIn) {
    fadeOut_->stop();
    slideOut_->stop();
    setOpacity(0.0);
    setSlideOffset(10);
    fadeIn_->start();
    slideIn_->start();
    return;
  }
  hideTimer_.stop();
  fadeIn_->stop();
  slideIn_->stop();
  fadeOut_->setStartValue(windowOpacity());
  fadeOut_->setEndValue(0.0);
  slideOut_->setStartValue(slideOffset_);
  slideOut_->setEndValue(10);
  fadeOutDone_ = false;
  slideOutDone_ = false;
  fadeOut_->start();
  slideOut_->start();
}

void CustomToolTip::checkHideComplete() {
  if (!fadeOutDone_ || !slideOutDone_) return;
  hide();
  if (active_ == this) active_ = nullptr;
  deleteLater();
}

TooltipEventFilter::TooltipEventFilter(QWidget* widget, const QString& text, int delayMs,
                                       const QString& position)
    : QObject(widget), widget_(widget), text_(text), position_(position), delay_(delayMs) {
  hideTimer_.setSingleShot(true);
  connect(&hideTimer_, &QTimer::timeout, this, &TooltipEventFilter::hideTooltip);
  hoverTimer_.setSingleShot(true);
  connect(&hoverTimer_, &QTimer::timeout, this, [this] {
    if (mouseInside_) showNow();
  });
  pollTimer_.setInterval(50);
  connect(&pollTimer_, &QTimer::timeout, this, &TooltipEventFilter::pollMouse);
}

TooltipEventFilter::~TooltipEventFilter() {
  if (tip_) tip_->deleteLater();
}

void TooltipEventFilter::updateText(const QString& text) {
  text_ = text;
  if (tip_ && tip_->isVisible()) tip_->updateContent(text);
}

void TooltipEventFilter::showNow() {
  if (!tip_) {
    tip_ = new CustomToolTip;
    tip_->setPositionPreference(position_);
  }
  if (tip_->isVisible()) {
    tip_->updateContent(text_);
  } else {
    const QRect rect = widget_->rect();
    const QRect global = rect.translated(widget_->mapToGlobal(QPoint(0, 0)));
    const QRect geometry =
        widget_->isVisible() && rect.width() > 0 && rect.height() > 0 ? global : QRect();
    QRect topLevel = global;
    if (auto* bar = qobject_cast<yasb::bar::BarWindow*>(widget_->window())) {
      auto* frame = bar->barFrame();
      topLevel = frame->clickRect().translated(frame->mapToGlobal(QPoint(0, 0)));
    }
    tip_->showTooltip(text_, geometry, topLevel);
  }
  hideTimer_.stop();
  mouseInside_ = true;
  pollTimer_.start();
}

void TooltipEventFilter::hideTooltip() {
  if (tip_) {
    if (tip_->isVisible()) {
      tip_->startFadeOut();  // deletes itself when the fade finishes
    } else {
      tip_->deleteLater();
    }
    tip_ = nullptr;
  }
  mouseInside_ = false;
  pollTimer_.stop();
}

void TooltipEventFilter::pollMouse() {
  const QRect global = widget_->rect().translated(widget_->mapToGlobal(QPoint(0, 0)));
  if (global.contains(QCursor::pos())) {
    hideTimer_.stop();
    mouseInside_ = true;
  } else {
    if (!hideTimer_.isActive()) hideTimer_.start(10);
    mouseInside_ = false;
  }
}

bool TooltipEventFilter::eventFilter(QObject* watched, QEvent* event) {
  if (watched == widget_) {
    switch (event->type()) {
      case QEvent::Enter:
        mouseInside_ = true;
        hoverTimer_.start(delay_);
        hideTimer_.stop();
        break;
      case QEvent::Leave:
        hoverTimer_.stop();
        if (!hideTimer_.isActive()) hideTimer_.start(10);
        mouseInside_ = false;
        break;
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonDblClick:
      case QEvent::FocusIn:
        hoverTimer_.stop();
        hideTimer_.stop();
        hideTooltip();
        break;
      default:
        break;
    }
  }
  return QObject::eventFilter(watched, event);
}

void setTooltip(QWidget* widget, const QString& text, int delayMs, const QString& position) {
  if (text.isEmpty()) return;
  if (auto* existing =
          widget->findChild<TooltipEventFilter*>(QString(), Qt::FindDirectChildrenOnly)) {
    existing->updateText(text);
    if (!position.isNull()) existing->setPosition(position);
    return;
  }
  auto* filter = new TooltipEventFilter(widget, text, delayMs, position);
  widget->setMouseTracking(true);
  widget->installEventFilter(filter);
}

void showTooltipNow(QWidget* widget) {
  if (auto* f = widget->findChild<TooltipEventFilter*>(QString(), Qt::FindDirectChildrenOnly))
    f->showNow();
}

}  // namespace yasb::widgets
