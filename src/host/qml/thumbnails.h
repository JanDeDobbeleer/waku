#pragma once

#include <QtQml/qqmlregistration.h>

#include <QQmlEngine>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>
#include <QVariantList>
#include <atomic>
#include <memory>

#include "host/thumbnail_store.h"

namespace wabar::host {

// image://thumb/<path>?w=<pixels>: a downscaled copy of an image file, from a memory cache, a disk
// cache (%LOCALAPPDATA%\wabar\thumbs) or a decode off the GUI thread (host/thumbnail_store.h).
// <path> is a local path or file: URL, percent-encoded or not; w defaults to the Image's
// sourceSize.width, then to 640. The Image shows its error state for a missing or unreadable file.
//
// Threads: Qt calls requestImageResponse on its image-reader thread; each load runs on one of the
// provider's pool threads (three) and touches only the ThumbnailStore, which is thread safe. The
// destructor answers the queued loads with an error and waits only for the decodes in progress,
// since it can run on the GUI thread while the engine clears its singletons.
class ThumbnailProvider : public QQuickAsyncImageProvider {
 public:
  explicit ThumbnailProvider(std::shared_ptr<ThumbnailStore> store);
  ~ThumbnailProvider() override;

  QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

  // Splits `id` into the source and the width of its "?w=" query (0 when absent).
  static void parseId(const QString& id, QString* source, int* width);

 private:
  std::shared_ptr<ThumbnailStore> store_;
  std::shared_ptr<std::atomic_bool> cancelled_;
  QThreadPool pool_;
};

// The `Thumbnails` singleton: makes the `thumb` image provider exist and warms its cache.
//
//     Image { source: Thumbnails.url(path, 480); asynchronous: true }
//     Component.onCompleted: Thumbnails.prewarm(listOfPaths, 480)
//
//   url(path, width)       the image URL for a file. Using it creates the singleton and with it the
//                          provider, so build thumbnail URLs with it rather than by hand.
//   prewarm(paths, width)  decodes and caches the thumbnails in the background, in the order given,
//                          at low priority on at most two threads. Already cached files cost one
//                          stat and one header read. Returns at once.
//   cancelPrewarm()        drops the queued work that has not started.
//
// A singleton because QML needs an object to call and because the provider belongs to the engine
// that creates it. It outlives nothing: the engine destroys it, which waits for the running tasks.
// Without a live desktop (offscreen platform) the disk cache is off and only memory is used, so a
// test never writes to the user's folder. GUI thread only; the work runs on the pool.
class Thumbnails : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  explicit Thumbnails(std::shared_ptr<ThumbnailStore> store, QObject* parent = nullptr);
  ~Thumbnails() override;

  static Thumbnails* create(QQmlEngine* engine, QJSEngine* scriptEngine);

  Q_INVOKABLE QString url(const QString& path, int width = 0) const;
  Q_INVOKABLE void prewarm(const QVariantList& paths, int width = 0);
  Q_INVOKABLE void cancelPrewarm();

  // Blocks until the queued prewarm work has run (tests).
  bool waitForPrewarm(int msecs = 30000);
  ThumbnailStore& store() { return *store_; }

 private:
  std::shared_ptr<ThumbnailStore> store_;
  // Prewarm: at most two low-priority threads. Pruning the disk cache runs here once at creation.
  QThreadPool pool_;
};

}  // namespace wabar::host
