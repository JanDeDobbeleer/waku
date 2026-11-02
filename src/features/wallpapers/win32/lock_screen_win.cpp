// The lock screen picture through WinRT LockScreen.SetImageFileAsync (Windows.System.UserProfile,
// documented). Windows copies the image, so a temporary copy may be overwritten afterwards.
//
// Threading: one worker thread (kernel::TaskThread, MTA), started by the first set() on the GUI
// thread, owns every WinRT object and the PNG copy. set() only stores the path under a mutex and
// arms one coalesced apply on the worker; only the latest path is set. The destructor stops the
// worker and joins it, so the join waits for one WinRT call at most. The GUI thread never blocks on
// WinRT.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.UserProfile.h>
#include <winrt/base.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <chrono>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>

#include "features/wallpapers/lock_screen.h"
#include "kernel/win32/task_thread.h"
#include "kernel/win32/winrt_strings.h"

namespace waku::wallpapers {
namespace {

class WinrtLockScreenSetter final : public LockScreenSetter {
 public:
  explicit WinrtLockScreenSetter(QString cacheDir) : cacheDir_(std::move(cacheDir)) {}

  ~WinrtLockScreenSetter() override { worker_.stop(); }

  void set(const QString& path) override {
    {
      std::lock_guard lock(mutex_);
      pending_ = path;
    }
    if (!worker_.running()) worker_.start();
    // A burst of set() calls runs one apply, which takes the latest path.
    worker_.postDeferred(std::chrono::milliseconds(0), [this] { applyPending(); });
  }

 private:
  void applyPending() {
    QString path;
    {
      std::lock_guard lock(mutex_);
      if (!pending_) return;
      path = *std::exchange(pending_, std::nullopt);
    }
    apply(path);
  }

  // The file Windows gets: the image itself, or a PNG copy in the cache folder.
  std::optional<QString> usableFile(const QString& path) const {
    if (!lockScreenNeedsCopy(path)) return path;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
      qWarning().noquote() << "Wallpapers: lock screen not set, cannot read" << path << ":"
                           << reader.errorString();
      return std::nullopt;
    }
    const QString copy = QDir(cacheDir_).filePath(QStringLiteral("lock_screen.png"));
    if (!image.save(copy, "PNG")) {
      qWarning().noquote() << "Wallpapers: lock screen not set, cannot write" << copy;
      return std::nullopt;
    }
    return copy;
  }

  void apply(const QString& path) {
    try {
      const std::optional<QString> file = usableFile(path);
      if (!file) return;
      using winrt::Windows::Storage::StorageFile;
      using winrt::Windows::System::UserProfile::LockScreen;
      const QString native = QDir::toNativeSeparators(QFileInfo(*file).absoluteFilePath());
      const StorageFile storageFile =
          StorageFile::GetFileFromPathAsync(kernel::toHString(native)).get();
      LockScreen::SetImageFileAsync(storageFile).get();
      qInfo().noquote() << "Wallpapers: lock screen set:" << path;
    } catch (const winrt::hresult_error& error) {
      qWarning().noquote() << "Wallpapers: lock screen not set:"
                           << kernel::toQString(error.message()) << "(" << path << ")";
    } catch (const std::exception& error) {
      qWarning().noquote() << "Wallpapers: lock screen not set:" << error.what() << "(" << path
                           << ")";
    } catch (...) {
      qWarning().noquote() << "Wallpapers: lock screen not set (" << path << ")";
    }
  }

  const QString cacheDir_;
  std::mutex mutex_;
  std::optional<QString> pending_;  // guarded by mutex_
  kernel::TaskThread worker_{QStringLiteral("Wallpapers: lock screen")};
};

}  // namespace

std::unique_ptr<LockScreenSetter> makeLockScreenSetter(const QString& cacheDir) {
  return std::make_unique<WinrtLockScreenSetter>(cacheDir);
}

}  // namespace waku::wallpapers
