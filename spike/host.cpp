#include "spike/host.h"

#include <QDir>
#include <QQmlEngine>
#include <QUrl>

namespace spike {

Host::Host(QQmlEngine* engine, const QString& widgetsDir, QObject* parent)
    : QObject(parent), engine_(engine), dir_(widgetsDir) {
  debounce_.setSingleShot(true);
  debounce_.setInterval(150);
  connect(&debounce_, &QTimer::timeout, this, &Host::rescan);
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, &debounce_,
          qOverload<>(&QTimer::start));
  connect(&watcher_, &QFileSystemWatcher::fileChanged, &debounce_, qOverload<>(&QTimer::start));

  memoryTimer_.setInterval(2000);
  connect(&memoryTimer_, &QTimer::timeout, this, &Host::sampleMemory);
  memoryTimer_.start();
  sampleMemory();
  rescan();
}

void Host::rescan() {
  // A saved file is often replaced, so every path is added again each time.
  const QDir dir(dir_);
  const QStringList names = dir.entryList({QStringLiteral("*.qml")}, QDir::Files, QDir::Name);
  QStringList urls;
  QStringList paths{dir_};
  for (const QString& name : names) {
    const QString path = dir.filePath(name);
    paths << path;
    urls << QUrl::fromLocalFile(path).toString();
  }
  const QStringList old = watcher_.files() + watcher_.directories();
  if (!old.isEmpty()) watcher_.removePaths(old);
  watcher_.addPaths(paths);

  // The Repeater keeps its delegates when the list is equal, so empty it for one event-loop
  // turn: the loaders are destroyed, then rebuilt from a cold component cache.
  widgetFiles_.clear();
  emit widgetFilesChanged();
  QTimer::singleShot(0, this, [this, urls] {
    engine_->clearComponentCache();
    widgetFiles_ = urls;
    emit widgetFilesChanged();
  });
}

void Host::sampleMemory() {
  try {
    memoryPercent_ = memory_.sample().percent;
    emit memoryChanged();
  } catch (...) {
  }
}

}  // namespace spike
