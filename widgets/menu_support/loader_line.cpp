#include "widgets/menu_support/loader_line.h"

#include <QEasingCurve>
#include <QEvent>
#include <QPainter>
#include <QPalette>
#include <QTimer>
#include <algorithm>

namespace yasb::widgets {

LoaderLine::LoaderLine(QWidget* parent)
    : QWidget(parent), animation_(new QPropertyAnimation(this, "offset", this)) {
  animation_->setDuration(2400);
  animation_->setStartValue(0.0);
  animation_->setEndValue(1.0);
  animation_->setLoopCount(-1);
  animation_->setEasingCurve(QEasingCurve::InOutSine);
  setFixedHeight(1);
  setVisible(false);
  setProperty("class", QStringLiteral("loader-line"));
  setAttribute(Qt::WA_TransparentForMouseEvents);
  raise();
}

void LoaderLine::configure(const QString& className, double segmentRatio, int height) {
  if (!className.isEmpty()) setProperty("class", className);
  segmentRatio_ = segmentRatio;
  if (height >= 0) {
    setFixedHeight(height);
    positionInTarget();
  }
}

void LoaderLine::attachToWidget(QWidget* target) {
  if (!target) return;
  target_ = target;
  if (parent() != target) setParent(target);
  target->installEventFilter(this);
  QTimer::singleShot(0, this, [this] { positionInTarget(); });
}

void LoaderLine::positionInTarget() {
  if (!target_) return;
  int h = maximumHeight();
  if (h <= 0 || h >= 10000) h = height() > 0 ? height() : 2;
  setGeometry(0, target_->height() - h, target_->width(), h);
}

bool LoaderLine::eventFilter(QObject* watched, QEvent* event) {
  if (watched == target_ && event->type() == QEvent::Resize) positionInTarget();
  return QWidget::eventFilter(watched, event);
}

void LoaderLine::start() {
  if (animation_->state() == QAbstractAnimation::Running) return;
  offset_ = 0.0;
  setVisible(true);
  animation_->start();
}

void LoaderLine::stop() {
  if (animation_->state() == QAbstractAnimation::Running) animation_->stop();
  setVisible(false);
  update();
}

void LoaderLine::setOffset(double value) {
  offset_ = value;
  update();
}

void LoaderLine::paintEvent(QPaintEvent*) {
  if (!isVisible()) return;
  const int w = width();
  const int h = height();
  if (w <= 0 || h <= 0) return;
  const double phase = std::clamp(offset_, 0.0, 1.0);
  const double t = 2 * phase - 1;
  const double sizeScale = 0.2 + 1.2 * (1 - t * t);
  const int segment = std::max(6, static_cast<int>(w * segmentRatio_ * sizeScale));
  const int x = static_cast<int>((w + segment) * offset_) - segment;
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(x, 0, segment, h, palette().color(QPalette::WindowText));
}

}  // namespace yasb::widgets
