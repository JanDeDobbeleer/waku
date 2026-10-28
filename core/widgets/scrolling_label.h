#pragma once

#include <QLabel>
#include <QMargins>
#include <QStaticText>
#include <QString>
#include <QTimer>

namespace yasb::widgets {

// Port of utils.ScrollingLabel: a QLabel that scrolls (or bounces) its text when it is wider than
// the label. Plain-data options so any widget can build one from its own config.
struct ScrollingLabelOptions {
  enum class Style { Left, Right, Bounce, BounceEase };

  int updateIntervalMs = 33;  // clamped to 4..1000
  Style style = Style::Left;
  bool alwaysScroll = false;
  QString separator = QStringLiteral(" ");  // only added while scrolling (left / right)
  int labelPadding = 0;                     // spaces around the text (bounce styles)
  int easeSlope = 20;
  double easePos = 0.8;
  double easeMin = 0.5;  // clamped to 0.2..1

  // "left" | "right" | "bounce" | "bounce-ease"; anything else (including empty) is "left".
  static Style parseStyle(const QString& name);
};

// Ease factor of the bounce-ease style: 1.0 at the centre, easeMin at both ends.
double scrollEase(int offset, int maxOffset, int slope, double pos, double minValue);

// Single-threaded (GUI thread). The timer only runs while the label is visible and scrolling is
// needed; text, resize and show/hide events re-evaluate it.
class ScrollingLabel : public QLabel {
  Q_OBJECT

 public:
  // maxWidthChars > 0 caps the width at averageCharWidth * maxWidthChars (Python max_width).
  explicit ScrollingLabel(QWidget* parent = nullptr, int maxWidthChars = 0,
                          const ScrollingLabelOptions& options = {});

  void setText(const QString& text);
  QSize sizeHint() const override;

  // Test hooks.
  bool scrollingNeeded() const { return scrollingNeeded_; }
  int offset() const { return offset_; }
  QString displayText() const { return text_; }
  bool timerActive() const { return timer_.isActive(); }
  // One animation step (the timer slot).
  void advance();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  bool isBounce() const;
  void buildTextAndMetrics();
  void setIdleOffset();
  void syncTimer();
  int contentWidth() const;

  ScrollingLabelOptions options_;
  int maxWidthChars_;
  QString paddingChars_;
  QString separator_;
  QString rawText_;
  QString text_;
  QStaticText staticText_;
  QMargins margin_;
  QTimer timer_;
  int bounceDirection_ = -1;
  int offset_ = 0;
  int textWidth_ = 1;
  int textY_ = 0;
  int ascent_ = 0;
  bool scrollingNeeded_ = false;
};

}  // namespace yasb::widgets
