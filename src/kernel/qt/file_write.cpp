#include "kernel/qt/file_write.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace waku::kernel {

FileWriteResult writeFileAtomic(const QString& path, const QByteArray& data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return {file.error(), file.errorString()};
  if (file.write(data) != data.size()) {
    const FileWriteResult failed{
        file.error() == QFileDevice::NoError ? QFileDevice::WriteError : file.error(),
        file.errorString()};
    file.cancelWriting();
    return failed;
  }
  if (!file.commit()) return {file.error(), file.errorString()};
  return {};
}

}  // namespace waku::kernel
