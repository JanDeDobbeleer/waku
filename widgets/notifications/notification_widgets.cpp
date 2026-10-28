#include "widgets/notifications/notification_widgets.h"

#include <QImageReader>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
#include <algorithm>
#include <cmath>
#include <utility>

#include "platform/window_system.h"

namespace yasb::widgets {

// --- ImageView --------------------------------------------------------------------------------

ImageView::ImageView(const QPixmap& pixmap, const QSizeF& aspect, int maxHeight, QWidget* parent)
    : QFrame(parent), pixmap_(pixmap), aspect_(aspect), maxHeight_(maxHeight) {
  QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  policy.setHeightForWidth(true);
  setSizePolicy(policy);
}

int ImageView::heightForWidth(int width) const {
  const QMargins margins = contentsMargins();
  const int inner = width - margins.left() - margins.right();
  if (inner <= 0 || aspect_.isEmpty()) return margins.top() + margins.bottom();
  const int height = static_cast<int>(std::lround(inner * aspect_.height() / aspect_.width()));
  return std::min(height, maxHeight_) + margins.top() + margins.bottom();
}

QSize ImageView::sizeHint() const {
  return QSize(0,
               heightForWidth(width() ? width() : static_cast<int>(std::lround(aspect_.width()))));
}

void ImageView::paintEvent(QPaintEvent* event) {
  QFrame::paintEvent(event);
  const QRectF target(contentsRect());
  if (target.isEmpty() || pixmap_.isNull()) return;
  QRectF source(pixmap_.rect());
  const double scale = std::max(target.width() / source.width(), target.height() / source.height());
  const QSizeF crop(target.width() / scale, target.height() / scale);
  source = QRectF(source.center().x() - crop.width() / 2, source.center().y() - crop.height() / 2,
                  crop.width(), crop.height());
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  QPainterPath path;
  path.addRoundedRect(target, kImageRadius, kImageRadius);
  painter.setClipPath(path);
  painter.drawPixmap(target, pixmap_, source);
}

// --- HoverFrame -------------------------------------------------------------------------------

HoverFrame::HoverFrame(QWidget* shown, QWidget* hidden) : shown_(shown), hidden_(hidden) {
  for (QWidget* widget : {shown, hidden}) {
    if (!widget) continue;
    QSizePolicy policy = widget->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    widget->setSizePolicy(policy);
  }
  shown->hide();
}

void HoverFrame::enterEvent(QEnterEvent* event) {
  if (hidden_) hidden_->hide();
  shown_->show();
  QFrame::enterEvent(event);
}

void HoverFrame::leaveEvent(QEvent* event) {
  shown_->hide();
  if (hidden_) hidden_->show();
  QFrame::leaveEvent(event);
}

// --- NotificationCard -------------------------------------------------------------------------

NotificationCard::NotificationCard(std::function<void()> onClick, QLabel* timeLabel,
                                   QLabel* dismissLabel)
    : HoverFrame(dismissLabel, timeLabel), onClick_(std::move(onClick)) {
  setCursor(Qt::PointingHandCursor);
}

void NotificationCard::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && onClick_) onClick_();
}

// --- ButtonLabel ------------------------------------------------------------------------------

ButtonLabel::ButtonLabel(const QString& text, const QString& cssClass, std::function<void()> action)
    : QLabel(text), action_(std::move(action)) {
  setProperty("class", cssClass);
  setCursor(Qt::PointingHandCursor);
}

void ButtonLabel::mousePressEvent(QMouseEvent*) {
  if (action_) action_();
}

// --- ImageLoader ------------------------------------------------------------------------------

ImageLoader::ImageLoader(ImageSignals* notifier, QString key, QString path, QSize maxSize,
                         bool square, bool circle)
    : notifier_(notifier),
      key_(std::move(key)),
      path_(std::move(path)),
      maxSize_(maxSize),
      square_(square),
      circle_(circle) {
  setAutoDelete(true);
}

void ImageLoader::run() {
  try {
    QImageReader reader(path_);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid()) {
      const QSize scaled =
          size.scaled(maxSize_, square_ ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
      if (scaled.width() < size.width()) reader.setScaledSize(scaled);
    }
    QImage image = reader.read();
    if (!image.isNull() && square_) image = crop(image);
    emit notifier_->loaded(key_, image);
  } catch (...) {
    emit notifier_->loaded(key_, QImage());
  }
}

QImage ImageLoader::crop(const QImage& source) const {
  const int side = maxSize_.width();
  const QImage image =
      source.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
  const QRectF from((image.width() - side) / 2.0, (image.height() - side) / 2.0, side, side);
  QImage result(side, side, QImage::Format_ARGB32_Premultiplied);
  result.fill(Qt::transparent);
  QPainter painter(&result);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  if (circle_) {
    QPainterPath path;
    path.addEllipse(QRectF(0, 0, side, side));
    painter.setClipPath(path);
  }
  painter.drawImage(QRectF(0, 0, side, side), image, from);
  painter.end();
  return result;
}

// --- AppIconLoader ----------------------------------------------------------------------------

AppIconLoader::AppIconLoader(ImageSignals* notifier, QString key, QString aumid, int physicalSize)
    : notifier_(notifier), key_(std::move(key)), aumid_(std::move(aumid)), size_(physicalSize) {
  setAutoDelete(true);
}

void AppIconLoader::run() {
  QImage image;
  try {
    const platform::IconBitmap bitmap =
        platform::shellItemIcon(QStringLiteral("shell:AppsFolder\\") + aumid_, size_);
    if (!bitmap.isNull()) {
      image = QImage(reinterpret_cast<const uchar*>(bitmap.bgra.constData()), bitmap.width,
                     bitmap.height, bitmap.width * 4, QImage::Format_ARGB32)
                  .copy();
    }
  } catch (...) {
  }
  emit notifier_->loaded(key_, image);
}

}  // namespace yasb::widgets
