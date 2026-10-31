#include "features/apps/qml/app_icon_provider.h"

#include <QImage>
#include <QRunnable>
#include <QUrl>
#include <algorithm>
#include <atomic>

namespace wabar::apps {
namespace {

constexpr int kDefaultIconSize = 64;
constexpr int kMaxIconSize = 256;

QImage toImage(const wabar::kernel::IconBitmap& bitmap, int size) {
  if (bitmap.isNull() || bitmap.bgra.size() < qsizetype(bitmap.width) * bitmap.height * 4)
    return QImage();
  QImage image(reinterpret_cast<const uchar*>(bitmap.bgra.constData()), bitmap.width, bitmap.height,
               bitmap.width * 4, QImage::Format_ARGB32);
  image = image.copy();  // own the pixels: the byte array dies with the bitmap
  if (image.width() > size || image.height() > size)
    image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  return image;
}

// Reads the icon on a pool thread and hands the image to the response.
class IconTask : public QObject, public QRunnable {
  Q_OBJECT
 public:
  IconTask(std::shared_ptr<IconIndex> index, IconLoader loader,
           std::shared_ptr<std::atomic_bool> cancelled, QString id, int size)
      : index_(std::move(index)),
        loader_(std::move(loader)),
        cancelled_(std::move(cancelled)),
        id_(std::move(id)),
        size_(size) {}

  void run() override {
    QImage image;
    try {
      // A provider going away answers its queued loads with an error instead of reading them.
      if (cancelled_->load()) {
        emit done(image);
        return;
      }
      const QString source = index_->sourceFor(id_);
      if (!source.isEmpty() && loader_) image = toImage(loader_(source, size_), size_);
    } catch (...) {
      // A throwing loader fails this request only.
    }
    emit done(image);
  }

 signals:
  void done(const QImage& image);

 private:
  std::shared_ptr<IconIndex> index_;
  IconLoader loader_;
  std::shared_ptr<std::atomic_bool> cancelled_;
  QString id_;
  int size_;
};

class IconResponse : public QQuickImageResponse {
  Q_OBJECT
 public:
  QQuickTextureFactory* textureFactory() const override {
    return QQuickTextureFactory::textureFactoryForImage(image_);
  }
  QString errorString() const override { return error_; }

 public slots:
  void handleDone(const QImage& image) {
    image_ = image;
    if (image.isNull()) error_ = QStringLiteral("no icon");
    emit finished();
  }

 private:
  QImage image_;
  QString error_;
};

}  // namespace

QString appIconUrl(const QString& id) {
  return QStringLiteral("image://appicon/") + QString::fromLatin1(QUrl::toPercentEncoding(id));
}

IconLoader realIconLoader() {
  return [](const QString& source, int size) { return wabar::kernel::shellItemIcon(source, size); };
}

void IconIndex::replace(QHash<QString, QString> sources) {
  QMutexLocker lock(&mutex_);
  sources_ = std::move(sources);
}

QString IconIndex::sourceFor(const QString& id) const {
  QMutexLocker lock(&mutex_);
  auto it = sources_.constFind(id);
  if (it != sources_.constEnd()) return it.value();
  it = sources_.constFind(QUrl::fromPercentEncoding(id.toUtf8()));
  return it == sources_.constEnd() ? QString() : it.value();
}

AppIconProvider::AppIconProvider(std::shared_ptr<IconIndex> index, IconLoader loader)
    : index_(std::move(index)),
      loader_(std::move(loader)),
      cancelled_(std::make_shared<std::atomic_bool>(false)) {
  pool_.setMaxThreadCount(4);
}

AppIconProvider::~AppIconProvider() {
  // A launcher page that just asked for 100 icons must not read them all on the way out.
  cancelled_->store(true);
  pool_.waitForDone();
}

QQuickImageResponse* AppIconProvider::requestImageResponse(const QString& id,
                                                           const QSize& requestedSize) {
  const int wanted = std::max(requestedSize.width(), requestedSize.height());
  const int size = wanted > 0 ? std::min(wanted, kMaxIconSize) : kDefaultIconSize;
  auto* response = new IconResponse;
  auto* task = new IconTask(index_, loader_, cancelled_, id, size);
  // The task is a QObject that the pool deletes after run(); the queued connection delivers the
  // image to the response on its own thread, and drops it when the response is gone.
  QObject::connect(task, &IconTask::done, response, &IconResponse::handleDone,
                   Qt::QueuedConnection);
  pool_.start(task);
  return response;
}

}  // namespace wabar::apps

#include "app_icon_provider.moc"
