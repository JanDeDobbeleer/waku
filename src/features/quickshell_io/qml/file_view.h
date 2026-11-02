#pragma once

#include <QtQml/qqmlregistration.h>

#include <QByteArray>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QString>
#include <QTimer>
#include <optional>

#include "features/quickshell_io/file_access.h"
#include "features/quickshell_io/qml/json_adapter.h"

namespace waku::quickshell_io {

// Quickshell.Io FileViewError, as a QML enum namespace.
namespace FileViewError {
Q_NAMESPACE
QML_ELEMENT

enum Enum {
  Success = static_cast<int>(FileError::Success),
  Unknown = static_cast<int>(FileError::Unknown),
  FileNotFound = static_cast<int>(FileError::FileNotFound),
  PermissionDenied = static_cast<int>(FileError::PermissionDenied),
  NotAFile = static_cast<int>(FileError::NotAFile),
};
Q_ENUM_NS(Enum)
}  // namespace FileViewError

// Quickshell.Io FileView: reads a file in the background, optionally watches it, writes it, and
// optionally converts it to QML properties through an adapter (JsonAdapter).
//
// Threads: the GUI thread owns everything. Reads and writes run on the global QThreadPool through
// waku::quickshell_io::fileAccess() and return through a QFutureWatcher owned by this object, so
// a result that arrives after the object is gone is dropped. Writes run one at a time; a write
// requested meanwhile replaces the one waiting. blockLoading, blockAllReads and blockWrites make
// the matching access run on the calling thread instead. Stopped by destroying the object.
class FileView : public QObject, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_ELEMENT
  Q_CLASSINFO("DefaultProperty", "adapter")
  Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
  Q_PROPERTY(bool loaded READ isLoaded NOTIFY loadedChanged FINAL)
  Q_PROPERTY(
      bool blockLoading READ blockLoading WRITE setBlockLoading NOTIFY blockLoadingChanged FINAL)
  Q_PROPERTY(bool blockAllReads READ blockAllReads WRITE setBlockAllReads NOTIFY
                 blockAllReadsChanged FINAL)
  Q_PROPERTY(bool blockWrites READ blockWrites WRITE setBlockWrites NOTIFY blockWritesChanged FINAL)
  Q_PROPERTY(bool preload READ preload WRITE setPreload NOTIFY preloadChanged FINAL)
  Q_PROPERTY(
      bool watchChanges READ watchChanges WRITE setWatchChanges NOTIFY watchChangesChanged FINAL)
  Q_PROPERTY(bool printErrors READ printErrors WRITE setPrintErrors NOTIFY printErrorsChanged FINAL)
  Q_PROPERTY(
      bool atomicWrites READ atomicWrites WRITE setAtomicWrites NOTIFY atomicWritesChanged FINAL)
  Q_PROPERTY(waku::quickshell_io::FileViewAdapter* adapter READ adapter WRITE setAdapter NOTIFY
                 adapterChanged FINAL)

 public:
  explicit FileView(QObject* parent = nullptr);
  ~FileView() override;

  QString path() const { return path_; }
  void setPath(const QString& path);
  bool isLoaded() const { return loaded_; }
  bool blockLoading() const { return blockLoading_; }
  void setBlockLoading(bool block);
  bool blockAllReads() const { return blockAllReads_; }
  void setBlockAllReads(bool block);
  bool blockWrites() const { return blockWrites_; }
  void setBlockWrites(bool block);
  bool preload() const { return preload_; }
  void setPreload(bool preload);
  bool watchChanges() const { return watchChanges_; }
  void setWatchChanges(bool watch);
  bool printErrors() const { return printErrors_; }
  void setPrintErrors(bool print);
  bool atomicWrites() const { return atomicWrites_; }
  void setAtomicWrites(bool atomic);
  FileViewAdapter* adapter() const { return adapter_; }
  void setAdapter(FileViewAdapter* adapter);

  // The loaded content. Empty until the file is loaded; with blockLoading (or blockAllReads) the
  // first call loads it on the calling thread.
  Q_INVOKABLE QString text();
  Q_INVOKABLE QByteArray data();
  // Loads the file again (in the background unless blockAllReads).
  Q_INVOKABLE void reload();
  Q_INVOKABLE void setText(const QString& text);
  Q_INVOKABLE void setData(const QByteArray& data);
  // Serializes the adapter and writes it.
  Q_INVOKABLE void writeAdapter();
  // Waits for the read or writes in flight and applies their results. False when none was running.
  Q_INVOKABLE bool waitForJob();

  void classBegin() override {}
  void componentComplete() override;

 signals:
  void pathChanged();
  void loadedChanged();
  void blockLoadingChanged();
  void blockAllReadsChanged();
  void blockWritesChanged();
  void preloadChanged();
  void watchChangesChanged();
  void printErrorsChanged();
  void atomicWritesChanged();
  void adapterChanged();
  // The file was read (and, with an adapter, applied to it).
  void loaded();
  void loadFailed(waku::quickshell_io::FileViewError::Enum error);
  // A write finished.
  void saved();
  void saveFailed(waku::quickshell_io::FileViewError::Enum error);
  // QML changed an adapter value.
  void adapterUpdated();
  // With watchChanges: the file on disk differs from the loaded content. Call reload().
  void fileChanged();

 private:
  void ensureLoaded();
  void startRead(bool checkOnly);
  void finishRead(const ReadResult& result, bool checkOnly);
  void applyLoaded(const QByteArray& data);
  void fail(FileError error, const char* what);
  void queueWrite(const QByteArray& data);
  void startWrite(const QByteArray& data);
  void finishWrite(FileError error);
  void updateWatcher();

  QString path_;
  QByteArray data_;
  bool loaded_ = false;
  bool complete_ = false;
  bool blockLoading_ = false;
  bool blockAllReads_ = false;
  bool blockWrites_ = false;
  bool preload_ = true;
  bool watchChanges_ = false;
  bool printErrors_ = true;
  bool atomicWrites_ = true;
  bool applyingAdapter_ = false;
  QPointer<FileViewAdapter> adapter_;

  QFutureWatcher<ReadResult>* readWatcher_ = nullptr;
  bool readIsCheck_ = false;
  QFutureWatcher<FileError>* writeWatcher_ = nullptr;
  std::optional<QByteArray> pendingWrite_;

  QFileSystemWatcher watcher_;
  QTimer debounce_;
};

}  // namespace waku::quickshell_io
