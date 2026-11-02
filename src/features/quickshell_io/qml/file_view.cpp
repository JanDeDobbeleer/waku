#include "features/quickshell_io/qml/file_view.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFuture>
#include <QPromise>
#include <QThreadPool>
#include <memory>
#include <string_view>

namespace waku::quickshell_io {

namespace {

constexpr int kDebounceMs = 50;

FileViewError::Enum toEnum(FileError error) { return static_cast<FileViewError::Enum>(error); }

}  // namespace

FileView::FileView(QObject* parent) : QObject(parent) {
  debounce_.setSingleShot(true);
  debounce_.setInterval(kDebounceMs);
  connect(&debounce_, &QTimer::timeout, this, [this] {
    startRead(true);
    updateWatcher();  // an atomic replace drops the watched path
  });
  connect(&watcher_, &QFileSystemWatcher::fileChanged, &debounce_, qOverload<>(&QTimer::start));
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, &debounce_,
          qOverload<>(&QTimer::start));
}

FileView::~FileView() = default;

void FileView::setPath(const QString& path) {
  if (path == path_) return;
  path_ = path;
  data_.clear();
  if (readWatcher_) {
    readWatcher_->disconnect(this);
    readWatcher_->deleteLater();
    readWatcher_ = nullptr;
  }
  const bool wasLoaded = std::exchange(loaded_, false);
  emit pathChanged();
  if (wasLoaded) emit loadedChanged();
  if (!complete_) return;
  updateWatcher();
  if (preload_ && !path_.isEmpty()) {
    if (blockLoading_ || blockAllReads_) {
      finishRead(fileAccess().read(path_), false);
    } else {
      startRead(false);
    }
  }
}

void FileView::setBlockLoading(bool block) {
  if (block == blockLoading_) return;
  blockLoading_ = block;
  emit blockLoadingChanged();
}

void FileView::setBlockAllReads(bool block) {
  if (block == blockAllReads_) return;
  blockAllReads_ = block;
  emit blockAllReadsChanged();
}

void FileView::setBlockWrites(bool block) {
  if (block == blockWrites_) return;
  blockWrites_ = block;
  emit blockWritesChanged();
}

void FileView::setPreload(bool preload) {
  if (preload == preload_) return;
  preload_ = preload;
  emit preloadChanged();
  if (preload_ && complete_ && !loaded_ && !readWatcher_ && !path_.isEmpty()) ensureLoaded();
}

void FileView::setWatchChanges(bool watch) {
  if (watch == watchChanges_) return;
  watchChanges_ = watch;
  emit watchChangesChanged();
  updateWatcher();
}

void FileView::setPrintErrors(bool print) {
  if (print == printErrors_) return;
  printErrors_ = print;
  emit printErrorsChanged();
}

void FileView::setAtomicWrites(bool atomic) {
  if (atomic == atomicWrites_) return;
  atomicWrites_ = atomic;
  emit atomicWritesChanged();
}

void FileView::setAdapter(FileViewAdapter* adapter) {
  if (adapter == adapter_) return;
  if (adapter_) adapter_->disconnect(this);
  adapter_ = adapter;
  if (adapter_) {
    connect(adapter_, &FileViewAdapter::adapterUpdated, this, [this] {
      if (!applyingAdapter_) emit adapterUpdated();
    });
    if (loaded_) {
      applyingAdapter_ = true;
      adapter_->deserialize(data_);
      applyingAdapter_ = false;
    }
  }
  emit adapterChanged();
}

void FileView::componentComplete() {
  complete_ = true;
  updateWatcher();
  if (preload_ && !path_.isEmpty()) ensureLoaded();
}

void FileView::ensureLoaded() {
  if (loaded_ || path_.isEmpty()) return;
  if (blockLoading_ || blockAllReads_) {
    if (readWatcher_) {
      readWatcher_->disconnect(this);
      readWatcher_->deleteLater();
      readWatcher_ = nullptr;
    }
    finishRead(fileAccess().read(path_), false);
  } else if (!readWatcher_ || readIsCheck_) {
    startRead(false);
  }
}

QString FileView::text() {
  ensureLoaded();
  return QString::fromUtf8(data_);
}

QByteArray FileView::data() {
  ensureLoaded();
  return data_;
}

void FileView::reload() {
  if (path_.isEmpty()) return;
  if (blockAllReads_) {
    finishRead(fileAccess().read(path_), false);
  } else {
    startRead(false);
  }
}

void FileView::startRead(bool checkOnly) {
  if (path_.isEmpty()) return;
  if (checkOnly && readWatcher_ && !readIsCheck_) return;  // a load is on its way
  if (readWatcher_) {
    readWatcher_->disconnect(this);
    readWatcher_->deleteLater();
    readWatcher_ = nullptr;
  }
  auto promise = std::make_shared<QPromise<ReadResult>>();
  QFuture<ReadResult> future = promise->future();
  promise->start();
  FileAccess* access = &fileAccess();
  QThreadPool::globalInstance()->start([promise, access, path = path_] {
    promise->addResult(access->read(path));
    promise->finish();
  });
  auto* watcher = new QFutureWatcher<ReadResult>(this);
  readWatcher_ = watcher;
  readIsCheck_ = checkOnly;
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
    const ReadResult result = watcher->result();
    const bool check = readIsCheck_;
    readWatcher_ = nullptr;
    watcher->deleteLater();
    finishRead(result, check);
  });
  watcher->setFuture(future);
}

void FileView::finishRead(const ReadResult& result, bool checkOnly) {
  if (checkOnly) {
    if (result.error == FileError::Success) {
      if (!loaded_ || result.data != data_) emit fileChanged();
    } else if (loaded_) {
      emit fileChanged();  // removed or unreadable
    }
    return;
  }
  if (result.error == FileError::Success) {
    applyLoaded(result.data);
  } else {
    fail(result.error, "read");
  }
}

void FileView::applyLoaded(const QByteArray& data) {
  data_ = data;
  const bool wasLoaded = std::exchange(loaded_, true);
  if (adapter_) {
    applyingAdapter_ = true;
    adapter_->deserialize(data_);
    applyingAdapter_ = false;
  }
  if (!wasLoaded) emit loadedChanged();
  emit loaded();
}

void FileView::fail(FileError error, const char* what) {
  if (printErrors_) {
    qWarning().noquote() << "FileView: cannot" << what << path_ << "- error"
                         << static_cast<int>(error);
  }
  if (std::string_view(what) == "read") {
    emit loadFailed(toEnum(error));
  } else {
    emit saveFailed(toEnum(error));
  }
}

void FileView::setText(const QString& text) { setData(text.toUtf8()); }

void FileView::setData(const QByteArray& data) {
  if (path_.isEmpty()) {
    qWarning() << "FileView: cannot write without a path";
    return;
  }
  data_ = data;  // what the file will hold, so the watcher does not report our own write
  if (blockWrites_) {
    const FileError error = fileAccess().write(path_, data, atomicWrites_);
    if (error == FileError::Success)
      emit saved();
    else
      fail(error, "write");
    return;
  }
  queueWrite(data);
}

void FileView::writeAdapter() {
  if (!adapter_) {
    qWarning() << "FileView: writeAdapter() without an adapter";
    return;
  }
  setData(adapter_->serialize());
}

void FileView::queueWrite(const QByteArray& data) {
  if (writeWatcher_) {
    pendingWrite_ = data;
    return;
  }
  startWrite(data);
}

void FileView::startWrite(const QByteArray& data) {
  auto promise = std::make_shared<QPromise<FileError>>();
  QFuture<FileError> future = promise->future();
  promise->start();
  FileAccess* access = &fileAccess();
  QThreadPool::globalInstance()->start(
      [promise, access, path = path_, data, atomic = atomicWrites_] {
        promise->addResult(access->write(path, data, atomic));
        promise->finish();
      });
  auto* watcher = new QFutureWatcher<FileError>(this);
  writeWatcher_ = watcher;
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
    const FileError error = watcher->result();
    writeWatcher_ = nullptr;
    watcher->deleteLater();
    finishWrite(error);
  });
  watcher->setFuture(future);
}

void FileView::finishWrite(FileError error) {
  if (error == FileError::Success)
    emit saved();
  else
    fail(error, "write");
  if (pendingWrite_ && !writeWatcher_) {
    const QByteArray next = std::move(*pendingWrite_);
    pendingWrite_.reset();
    startWrite(next);
  }
}

bool FileView::waitForJob() {
  bool waited = false;
  if (readWatcher_) {
    auto* watcher = readWatcher_;
    watcher->future().waitForFinished();
    const ReadResult result = watcher->result();
    const bool check = readIsCheck_;
    readWatcher_ = nullptr;
    watcher->disconnect(this);
    watcher->deleteLater();
    finishRead(result, check);
    waited = true;
  }
  while (writeWatcher_) {
    auto* watcher = writeWatcher_;
    watcher->future().waitForFinished();
    const FileError error = watcher->result();
    writeWatcher_ = nullptr;
    watcher->disconnect(this);
    watcher->deleteLater();
    finishWrite(error);
    waited = true;
  }
  return waited;
}

void FileView::updateWatcher() {
  const QStringList old = watcher_.files() + watcher_.directories();
  if (!old.isEmpty()) watcher_.removePaths(old);
  if (!watchChanges_ || !complete_ || path_.isEmpty()) return;
  const QFileInfo info(path_);
  if (info.exists()) watcher_.addPath(info.absoluteFilePath());
  const QString dir = info.absolutePath();
  if (QFileInfo::exists(dir)) watcher_.addPath(dir);
}

}  // namespace waku::quickshell_io
