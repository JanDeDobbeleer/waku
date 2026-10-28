#pragma once

#include <QEnterEvent>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QObject>
#include <QPixmap>
#include <QRunnable>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <functional>

// Ports of core/widgets/services/notifications/notification_widgets.py.
namespace yasb::widgets {

constexpr int kImageRadius = 6;

// A picture as wide as the view in the shape of `aspect`, at most maxHeight tall, cropped to fit.
class ImageView : public QFrame {
  Q_OBJECT
 public:
  ImageView(const QPixmap& pixmap, const QSizeF& aspect, int maxHeight, QWidget* parent = nullptr);

  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override;
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QPixmap pixmap_;
  QSizeF aspect_;
  int maxHeight_;
};

// Shows `shown` in place of `hidden` while the pointer is over the frame; both keep their space.
class HoverFrame : public QFrame {
  Q_OBJECT
 public:
  explicit HoverFrame(QWidget* shown, QWidget* hidden = nullptr);

 protected:
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  QWidget* shown_;
  QWidget* hidden_;
};

// A toast row: a left click runs the callback; the dismiss label replaces the time on hover.
class NotificationCard : public HoverFrame {
  Q_OBJECT
 public:
  NotificationCard(std::function<void()> onClick, QLabel* timeLabel, QLabel* dismissLabel);

 protected:
  void mousePressEvent(QMouseEvent* event) override;

 private:
  std::function<void()> onClick_;
};

// MenuBuilder._create_button: a QLabel that runs an action on any mouse press and swallows it, so
// the row behind it does not get the click too.
class ButtonLabel : public QLabel {
  Q_OBJECT
 public:
  ButtonLabel(const QString& text, const QString& cssClass, std::function<void()> action);

 protected:
  void mousePressEvent(QMouseEvent* event) override;

 private:
  std::function<void()> action_;
};

class ImageSignals : public QObject {
  Q_OBJECT
 signals:
  // key, image (null if unreadable)
  void loaded(const QString& key, const QImage& image);
};

// Reads a picture off the GUI thread, scaled to fit maxSize, or cropped to a square of it.
class ImageLoader : public QRunnable {
 public:
  ImageLoader(ImageSignals* notifier, QString key, QString path, QSize maxSize, bool square,
              bool circle);
  void run() override;

 private:
  QImage crop(const QImage& image) const;

  ImageSignals* notifier_;
  QString key_;
  QString path_;
  QSize maxSize_;
  bool square_;
  bool circle_;
};

// Loads the shell icon of an app by AUMID off the GUI thread (IconService AppIcon).
class AppIconLoader : public QRunnable {
 public:
  AppIconLoader(ImageSignals* notifier, QString key, QString aumid, int physicalSize);
  void run() override;

 private:
  ImageSignals* notifier_;
  QString key_;
  QString aumid_;
  int size_;
};

}  // namespace yasb::widgets
