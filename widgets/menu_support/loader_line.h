#pragma once

#include <QPropertyAnimation>
#include <QWidget>

namespace yasb::widgets {

// Port of ui/components/loader.LoaderLine: a horizontal indeterminate indicator (a sliding
// segment painted in the widget's foreground colour, so `color:` in CSS sets it).
class LoaderLine : public QWidget {
  Q_OBJECT
  Q_PROPERTY(double offset READ offset WRITE setOffset)

 public:
  explicit LoaderLine(QWidget* parent = nullptr);

  void configure(const QString& className, double segmentRatio, int height = -1);
  // Overlays the bottom edge of `target` and follows its resizes.
  void attachToWidget(QWidget* target);
  void start();
  void stop();
  double offset() const { return offset_; }
  void setOffset(double value);

 protected:
  void paintEvent(QPaintEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void positionInTarget();

  double offset_ = 0.0;
  double segmentRatio_ = 0.18;
  QPropertyAnimation* animation_;
  QWidget* target_ = nullptr;
};

}  // namespace yasb::widgets
