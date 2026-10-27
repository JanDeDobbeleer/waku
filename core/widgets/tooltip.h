#pragma once

#include <QFrame>
#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRect>
#include <QString>
#include <QTimer>

namespace yasb::bar {
class GlobalState;
}

namespace yasb::widgets {

struct TooltipBlur {
  bool enabled = false;
  bool darkMode = false;
  bool roundCorners = false;
  QString roundCornersType = QStringLiteral("normal");
  QString borderColor = QStringLiteral("System");
};

// Global tooltip settings (config `tooltip` block, applied once at startup).
struct TooltipOptions {
  int offset = 5;
  TooltipBlur blur;
};
void setTooltipOptions(const TooltipOptions& options);
const TooltipOptions& tooltipOptions();

// Pure port of CustomToolTip.calculate_position. `position` is "top", "bottom" or empty (auto).
QPoint computeTooltipPosition(const QRect& widgetGeometry, const QRect& topLevelGeometry,
                              const QSize& tipSize, const QRect& screen, const QString& position,
                              int offset);

// Frameless tooltip with fade and slide animations; styled by the app stylesheet
// (`.tooltip[.dark]`).
class CustomToolTip : public QFrame {
  Q_OBJECT
  Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
  Q_PROPERTY(int slideOffset READ slideOffset WRITE setSlideOffset)

 public:
  explicit CustomToolTip(bool darkMode, QWidget* parent = nullptr);

  qreal opacity() const { return opacity_; }
  void setOpacity(qreal value);
  int slideOffset() const { return slideOffset_; }
  void setSlideOffset(int value);

  void setPositionPreference(const QString& position) { position_ = position; }
  void updateContent(const QString& text);
  // A null or empty widgetGeometry only sets the text (matches Python).
  void showTooltip(const QString& text, const QRect& widgetGeometry, const QRect& topLevelGeometry,
                   int durationMs = 0);
  void startFadeOut();
  static void hideActive();

 private:
  void startAnimations(bool fadeIn);
  void checkHideComplete();

  QLabel* label_;
  qreal opacity_ = 0.0;
  int slideOffset_ = 0;
  QPoint basePos_;
  bool hasBasePos_ = false;
  QString position_;
  QTimer hideTimer_;
  QPropertyAnimation* fadeIn_;
  QPropertyAnimation* fadeOut_;
  QPropertyAnimation* slideIn_;
  QPropertyAnimation* slideOut_;
  bool fadeOutDone_ = false;
  bool slideOutDone_ = false;
  static inline QPointer<CustomToolTip> active_;
};

// Shows a tooltip after `delay` ms of hover and hides it when the cursor leaves.
class TooltipEventFilter : public QObject {
  Q_OBJECT

 public:
  TooltipEventFilter(QWidget* widget, const QString& text, int delayMs, const QString& position,
                     bar::GlobalState& globalState);
  ~TooltipEventFilter() override;

  void updateText(const QString& text);
  void setPosition(const QString& position) { position_ = position; }
  void showNow();
  const QString& text() const { return text_; }
  CustomToolTip* tooltip() const { return tip_; }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void hideTooltip();
  void pollMouse();

  QWidget* widget_;
  QString text_;
  QString position_;
  int delay_;
  bar::GlobalState& globalState_;
  QPointer<CustomToolTip> tip_;
  QTimer hideTimer_;
  QTimer hoverTimer_;
  QTimer pollTimer_;
  bool mouseInside_ = false;
};

// Declarative attach: an empty text does nothing; calling again updates the existing tooltip's
// text.
void setTooltip(QWidget* widget, const QString& text, bar::GlobalState& globalState,
                int delayMs = 400, const QString& position = QString());
void showTooltipNow(QWidget* widget);

}  // namespace yasb::widgets
