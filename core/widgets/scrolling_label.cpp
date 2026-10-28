#include "core/widgets/scrolling_label.h"

#include <QFontMetrics>
#include <QPainter>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace yasb::widgets {

ScrollingLabelOptions::Style ScrollingLabelOptions::parseStyle(const QString& name) {
  if (name == QLatin1String("right")) return Style::Right;
  if (name == QLatin1String("bounce")) return Style::Bounce;
  if (name == QLatin1String("bounce-ease")) return Style::BounceEase;
  return Style::Left;
}

double scrollEase(int offset, int maxOffset, int slope, double pos, double minValue) {
  const double x = std::abs(maxOffset != 0 ? 2.0 * offset / maxOffset - 1 : 0.0);
  return (1 + std::tanh(-slope * (x - pos))) * (1 - minValue) / 2 + minValue;
}

ScrollingLabel::ScrollingLabel(QWidget* parent, int maxWidthChars,
                               const ScrollingLabelOptions& options)
    : QLabel(parent), options_(options), maxWidthChars_(maxWidthChars) {
  options_.updateIntervalMs = std::clamp(options_.updateIntervalMs, 4, 1000);
  options_.easeMin = std::clamp(options_.easeMin, 0.2, 1.0);
  if (isBounce()) {
    paddingChars_ = QString(std::max(0, options_.labelPadding), QChar(u' '));
  } else {
    separator_ = options_.separator;
  }
  margin_ = contentsMargins();

  buildTextAndMetrics();
  connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
  if (!scrollingNeeded_) setIdleOffset();
  syncTimer();
}

bool ScrollingLabel::isBounce() const {
  return options_.style == ScrollingLabelOptions::Style::Bounce ||
         options_.style == ScrollingLabelOptions::Style::BounceEase;
}

int ScrollingLabel::contentWidth() const { return width() - margin_.left() - margin_.right(); }

void ScrollingLabel::setText(const QString& text) {
  QLabel::setText(text);
  offset_ = 0;
  rawText_ = text;
  buildTextAndMetrics();
  advance();
}

void ScrollingLabel::buildTextAndMetrics() {
  ensurePolished();
  margin_ = contentsMargins();
  const QFontMetrics metrics(font());
  ascent_ = metrics.ascent();

  const QString base = paddingChars_ + rawText_ + paddingChars_;
  const bool textIsWider = metrics.boundingRect(base).width() > contentWidth();

  if (isBounce()) {
    scrollingNeeded_ = textIsWider;
  } else {
    scrollingNeeded_ = options_.alwaysScroll || textIsWider;
  }

  // The separator is only added while scrolling.
  text_ = (scrollingNeeded_ && !isBounce()) ? paddingChars_ + rawText_ + separator_ + paddingChars_
                                            : base;

  staticText_ = QStaticText(text_);
  staticText_.prepare(QTransform(), font());
  textWidth_ = std::max(metrics.horizontalAdvance(text_), 1);
  textY_ = (height() + metrics.ascent() - metrics.descent() + 1) / 2;

  if (maxWidthChars_ > 0) setMaximumWidth(metrics.averageCharWidth() * maxWidthChars_);
}

void ScrollingLabel::setIdleOffset() {
  offset_ = isBounce() ? (textWidth_ - contentWidth()) / 2 : 0;
}

void ScrollingLabel::syncTimer() {
  if (scrollingNeeded_ && isVisible()) {
    if (!timer_.isActive()) timer_.start(options_.updateIntervalMs);
  } else {
    if (timer_.isActive()) timer_.stop();
    if (options_.style == ScrollingLabelOptions::Style::BounceEase)
      timer_.setInterval(options_.updateIntervalMs);
  }
}

void ScrollingLabel::advance() {
  if (!scrollingNeeded_) {
    setIdleOffset();
    syncTimer();
    if (isVisible()) update();
    return;
  }
  if (!isVisible()) {
    syncTimer();
    return;
  }

  switch (options_.style) {
    case ScrollingLabelOptions::Style::Left:
      offset_ = (offset_ + 1) % textWidth_;
      break;
    case ScrollingLabelOptions::Style::Right:
      --offset_;
      if (offset_ <= -textWidth_) offset_ = 0;
      break;
    case ScrollingLabelOptions::Style::Bounce:
    case ScrollingLabelOptions::Style::BounceEase: {
      const int maxOffset = textWidth_ - contentWidth();
      if (options_.style == ScrollingLabelOptions::Style::BounceEase) {
        const double factor =
            scrollEase(offset_, maxOffset, options_.easeSlope, options_.easePos, options_.easeMin);
        timer_.setInterval(static_cast<int>(options_.updateIntervalMs / factor));
      }
      offset_ += bounceDirection_;
      if (offset_ >= maxOffset) {
        offset_ = maxOffset;
        bounceDirection_ = -1;
      } else if (offset_ <= 0) {
        offset_ = 0;
        bounceDirection_ = 1;
      }
      break;
    }
  }
  syncTimer();
  update();
}

void ScrollingLabel::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  const QRect content(margin_.left(), margin_.top(), contentWidth(),
                      height() - margin_.top() - margin_.bottom());
  painter.setClipRect(content);

  int x = margin_.left() - offset_;
  const int y = textY_ - ascent_;

  switch (options_.style) {
    case ScrollingLabelOptions::Style::Left:
      if (scrollingNeeded_) {
        painter.drawStaticText(QPoint(x - textWidth_, y), staticText_);
        while (x < margin_.left() + content.width()) {
          painter.drawStaticText(QPoint(x, y), staticText_);
          x += textWidth_;
        }
      } else {
        painter.drawStaticText(QPoint(margin_.left(), y), staticText_);
      }
      break;
    case ScrollingLabelOptions::Style::Right:
      if (scrollingNeeded_) {
        painter.drawStaticText(QPoint(x + textWidth_, y), staticText_);
        while (x > margin_.left() - textWidth_) {
          painter.drawStaticText(QPoint(x, y), staticText_);
          x -= textWidth_;
        }
      } else {
        painter.drawStaticText(QPoint(margin_.left(), y), staticText_);
      }
      break;
    case ScrollingLabelOptions::Style::Bounce:
    case ScrollingLabelOptions::Style::BounceEase:
      painter.drawStaticText(QPoint(x, y), staticText_);
      break;
  }
}

QSize ScrollingLabel::sizeHint() const {
  const QFontMetrics metrics(font());
  const QMargins margins = contentsMargins();
  const QRect box = metrics.boundingRect(paddingChars_ + rawText_ + paddingChars_);
  const int w = std::max(1, box.width() + margins.left() + margins.right());
  const int h = std::max(1, box.height() + margins.top() + margins.bottom());
  return QSize(std::min(maximumWidth(), w), std::min(maximumHeight(), h));
}

void ScrollingLabel::showEvent(QShowEvent* event) {
  QLabel::showEvent(event);
  syncTimer();
}

void ScrollingLabel::hideEvent(QHideEvent* event) {
  if (timer_.isActive()) timer_.stop();
  if (options_.style == ScrollingLabelOptions::Style::BounceEase)
    timer_.setInterval(options_.updateIntervalMs);
  QLabel::hideEvent(event);
}

void ScrollingLabel::resizeEvent(QResizeEvent* event) {
  QLabel::resizeEvent(event);
  buildTextAndMetrics();
  advance();
}

}  // namespace yasb::widgets
