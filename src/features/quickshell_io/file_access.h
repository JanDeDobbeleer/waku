#pragma once

#include <QByteArray>
#include <QString>

namespace waku::quickshell_io {

// Mirrors FileViewError (qml/file_view.h); the values must stay equal.
enum class FileError { Success = 0, Unknown, FileNotFound, PermissionDenied, NotAFile };

struct ReadResult {
  FileError error = FileError::Success;
  QByteArray data;
};

// How FileView touches the disk. The real one uses QFile; tests replace it with a fake.
// Implementations are called from thread-pool threads and must be thread-safe.
class FileAccess {
 public:
  virtual ~FileAccess() = default;
  virtual ReadResult read(const QString& path) = 0;
  // Creates missing parent folders. `atomic` writes a temporary file and renames it over `path`.
  virtual FileError write(const QString& path, const QByteArray& data, bool atomic) = 0;
};

// The process-wide access FileView uses: the real one unless a test installed a fake.
FileAccess& fileAccess();
// Installs `fake` (not owned; must outlive its use); nullptr restores the real access.
void setFileAccessForTests(FileAccess* fake);

}  // namespace waku::quickshell_io
