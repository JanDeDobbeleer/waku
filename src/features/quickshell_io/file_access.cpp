#include "features/quickshell_io/file_access.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <atomic>

namespace wabar::quickshell_io {

namespace {

FileError fromDevice(QFileDevice::FileError error) {
  switch (error) {
    case QFileDevice::NoError:
      return FileError::Success;
    case QFileDevice::PermissionsError:
      return FileError::PermissionDenied;
    default:
      return FileError::Unknown;
  }
}

class RealFileAccess : public FileAccess {
 public:
  ReadResult read(const QString& path) override {
    const QFileInfo info(path);
    if (!info.exists()) return {FileError::FileNotFound, {}};
    if (!info.isFile()) return {FileError::NotAFile, {}};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {fromDevice(file.error()), {}};
    QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) return {fromDevice(file.error()), {}};
    return {FileError::Success, std::move(data)};
  }

  FileError write(const QString& path, const QByteArray& data, bool atomic) override {
    const QFileInfo info(path);
    if (info.isDir()) return FileError::NotAFile;
    if (!QDir().mkpath(info.absolutePath())) return FileError::Unknown;
    if (atomic) {
      QSaveFile file(path);
      if (!file.open(QIODevice::WriteOnly)) return fromDevice(file.error());
      if (file.write(data) != data.size()) return FileError::Unknown;
      return file.commit() ? FileError::Success : fromDevice(file.error());
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fromDevice(file.error());
    if (file.write(data) != data.size()) return FileError::Unknown;
    return FileError::Success;
  }
};

std::atomic<FileAccess*> g_override{nullptr};

}  // namespace

FileAccess& fileAccess() {
  static RealFileAccess real;
  FileAccess* fake = g_override.load();
  return fake ? *fake : real;
}

void setFileAccessForTests(FileAccess* fake) { g_override.store(fake); }

}  // namespace wabar::quickshell_io
