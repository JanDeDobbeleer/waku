#include "host/qml/thumbnails.h"

#include <QDebug>
#include <QRunnable>
#include <QThread>
#include <QUrl>
#include <algorithm>

#include "kernel/qt/app_data.h"
#include "kernel/qt/live_desktop.h"

namespace wabar::host {
namespace {

// Loads one thumbnail on a pool thread and hands the image to the response.
class LoadTask : public QObject, public QRunnable {
  Q_OBJECT
 public:
  LoadTask(std::shared_ptr<ThumbnailStore> store, QString source, int width)
      : store_(std::move(store)), source_(std::move(source)), width_(width) {}

  void run() override {
    QImage image;
    try {
      image = store_->get(source_, width_);
    } catch (...) {
      // A failing load fails this request only.
    }
    emit done(image);
  }

 signals:
  void done(const QImage& image);

 private:
  std::shared_ptr<ThumbnailStore> store_;
  QString source_;
  int width_;
};

class ThumbnailResponse : public QQuickImageResponse {
  Q_OBJECT
 public:
  QQuickTextureFactory* textureFactory() const override {
    return QQuickTextureFactory::textureFactoryForImage(image_);
  }
  QString errorString() const override { return error_; }

 public slots:
  void handleDone(const QImage& image) {
    image_ = image;
    if (image.isNull()) error_ = QStringLiteral("cannot make a thumbnail");
    emit finished();
  }

 private:
  QImage image_;
  QString error_;
};

class WarmTask : public QRunnable {
 public:
  WarmTask(std::shared_ptr<ThumbnailStore> store, QString path, int width)
      : store_(std::move(store)), path_(std::move(path)), width_(width) {}
  void run() override {
    try {
      store_->warm(path_, width_);
    } catch (...) {
      // Warming is best effort.
    }
  }

 private:
  std::shared_ptr<ThumbnailStore> store_;
  QString path_;
  int width_;
};

class PruneTask : public QRunnable {
 public:
  explicit PruneTask(std::shared_ptr<ThumbnailStore> store) : store_(std::move(store)) {}
  void run() override {
    try {
      store_->prune();
    } catch (...) {
      // Pruning is best effort.
    }
  }

 private:
  std::shared_ptr<ThumbnailStore> store_;
};

// One store for the process while anything holds it: the singleton and the provider of every
// engine generation. A reload builds the new pair while the old provider still lives, so a store
// per create() kept two memory caches; now the new pair finds the old store warm. GUI thread only
// (create() runs there).
std::shared_ptr<ThumbnailStore> defaultStore() {
  static std::weak_ptr<ThumbnailStore> shared;
  if (auto existing = shared.lock()) return existing;
  // No disk cache without a live desktop: tests and offscreen runs keep to memory.
  auto created = wabar::kernel::liveDesktop(QStringLiteral("thumbnails"))
                     ? std::make_shared<ThumbnailStore>(wabar::kernel::appDataDir() +
                                                        QStringLiteral("/thumbs"))
                     : std::make_shared<ThumbnailStore>(QString());
  shared = created;
  return created;
}

}  // namespace

ThumbnailProvider::ThumbnailProvider(std::shared_ptr<ThumbnailStore> store)
    : store_(std::move(store)) {
  pool_.setMaxThreadCount(3);
}

ThumbnailProvider::~ThumbnailProvider() { pool_.waitForDone(); }

void ThumbnailProvider::parseId(const QString& id, QString* source, int* width) {
  *width = 0;
  *source = id;
  const qsizetype query = id.lastIndexOf(QStringLiteral("?w="));
  if (query < 0) return;
  *source = id.left(query);
  bool ok = false;
  const int parsed = id.mid(query + 3).section(QLatin1Char('&'), 0, 0).toInt(&ok);
  if (ok) *width = parsed;
}

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id,
                                                             const QSize& requestedSize) {
  QString source;
  int width = 0;
  parseId(id, &source, &width);
  if (width <= 0) width = requestedSize.width();
  auto* response = new ThumbnailResponse;
  auto* task = new LoadTask(store_, source, width);
  // The pool deletes the task after run(); the queued connection delivers the image to the
  // response on its own thread and drops it when the response is gone.
  QObject::connect(task, &LoadTask::done, response, &ThumbnailResponse::handleDone,
                   Qt::QueuedConnection);
  pool_.start(task);
  return response;
}

Thumbnails::Thumbnails(std::shared_ptr<ThumbnailStore> store, QObject* parent)
    : QObject(parent), store_(std::move(store)) {
  pool_.setMaxThreadCount(2);
  pool_.setThreadPriority(QThread::LowPriority);
  pool_.start(new PruneTask(store_));
}

Thumbnails::~Thumbnails() {
  pool_.clear();
  pool_.waitForDone();
}

Thumbnails* Thumbnails::create(QQmlEngine* engine, QJSEngine*) {
  auto store = defaultStore();
  auto* singleton = new Thumbnails(store);
  // The engine owns the provider and deletes it with the singleton's engine.
  if (engine) engine->addImageProvider(QStringLiteral("thumb"), new ThumbnailProvider(store));
  return singleton;
}

QString Thumbnails::url(const QString& path, int width) const {
  QString local = path;
  if (local.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
    local = QUrl(local).toLocalFile();
  }
  local.replace(QLatin1Char('\\'), QLatin1Char('/'));
  return QStringLiteral("image://thumb/") + QString::fromLatin1(QUrl::toPercentEncoding(local)) +
         QStringLiteral("?w=") + QString::number(ThumbnailStore::clampWidth(width));
}

void Thumbnails::prewarm(const QVariantList& paths, int width) {
  const int clamped = ThumbnailStore::clampWidth(width);
  for (const QVariant& entry : paths) {
    const QString path =
        entry.userType() == QMetaType::QUrl ? entry.toUrl().toLocalFile() : entry.toString();
    if (path.isEmpty()) continue;
    pool_.start(new WarmTask(store_, path, clamped));
  }
}

void Thumbnails::cancelPrewarm() { pool_.clear(); }

bool Thumbnails::waitForPrewarm(int msecs) { return pool_.waitForDone(msecs); }

}  // namespace wabar::host

#include "thumbnails.moc"
