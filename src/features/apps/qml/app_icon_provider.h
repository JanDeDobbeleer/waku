#pragma once

#include <QHash>
#include <QMutex>
#include <QQuickAsyncImageProvider>
#include <QString>
#include <QThreadPool>
#include <atomic>
#include <functional>
#include <memory>

#include "kernel/win32/shell_icons.h"

namespace wabar::apps {

// The image URL of an app: image://appicon/<id>, the id percent-encoded.
QString appIconUrl(const QString& id);

// Reads the icon of a shell item (a path or shell:AppsFolder\<aumid>) at `size` pixels, off the GUI
// thread. Real: kernel::shellItemIcon, which joins the worker thread to COM for the call.
using IconLoader = std::function<wabar::kernel::IconBitmap(const QString& iconSource, int size)>;
IconLoader realIconLoader();

// App id -> what the shell reads its icon from. The service replaces the contents after every
// scan; the provider's worker threads read it. Thread safe.
class IconIndex {
 public:
  void replace(QHash<QString, QString> sources);
  // The icon source of `id`, also tried percent-decoded. "" when unknown.
  QString sourceFor(const QString& id) const;

 private:
  mutable QMutex mutex_;
  QHash<QString, QString> sources_;
};

// image://appicon/<id>?: one app icon, scaled down to the request's size (the QML sourceSize,
// 64 by default). Loads run on the provider's own pool of at most four threads, so a slow shell
// never blocks Qt's image-reader thread or the GUI. An unknown id or an app without an icon fails
// the request (the Image shows its error state).
//
// Threads: Qt calls requestImageResponse on its image-reader thread; each load runs on one of the
// provider's pool threads and touches only the IconIndex (mutex) and the loader. The destructor
// answers the queued loads with an error and waits only for the reads in progress.
class AppIconProvider : public QQuickAsyncImageProvider {
 public:
  AppIconProvider(std::shared_ptr<IconIndex> index, IconLoader loader);
  ~AppIconProvider() override;

  QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

 private:
  std::shared_ptr<IconIndex> index_;
  IconLoader loader_;
  std::shared_ptr<std::atomic_bool> cancelled_;
  QThreadPool pool_;
};

}  // namespace wabar::apps
