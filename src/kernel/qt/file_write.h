#pragma once

#include <QByteArray>
#include <QFileDevice>
#include <QString>

namespace waku::kernel {

struct FileWriteResult {
  QFileDevice::FileError error = QFileDevice::NoError;
  QString message;  // QFileDevice::errorString() when the write failed
  explicit operator bool() const { return error == QFileDevice::NoError; }
};

// Writes `data` to `path` atomically: a temporary file in the same folder that replaces `path`
// only once every byte is on disk (QSaveFile), so a crash mid-write leaves the old file intact.
// Creates the folder first. Any thread; two writers of one path must not overlap.
FileWriteResult writeFileAtomic(const QString& path, const QByteArray& data);

}  // namespace waku::kernel
