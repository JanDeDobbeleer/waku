// The lock screen picture through WinRT LockScreen.SetImageFileAsync (Windows.System.UserProfile,
// documented). Windows copies the image, so a temporary copy may be overwritten afterwards.
//
// Threading: one worker thread (std::thread, MTA), started by the first set(), owns every WinRT
// object and the PNG copy. set() only stores the path under a mutex and wakes the worker, which
// sleeps on a condition variable in between; only the latest path is set. The destructor sets the
// stop flag, wakes the worker and joins it, so the join waits for one WinRT call at most. The GUI
// thread never blocks on WinRT.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.UserProfile.h>
#include <winrt/base.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "features/wallpapers/lock_screen.h"

namespace wabar::wallpapers {
namespace {

class WinrtLockScreenSetter final : public LockScreenSetter {
 public:
  explicit WinrtLockScreenSetter(QString cacheDir) : cacheDir_(std::move(cacheDir)) {}

  ~WinrtLockScreenSetter() override {
    {
      std::lock_guard lock(mutex_);
      stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
  }

  void set(const QString& path) override {
    {
      std::lock_guard lock(mutex_);
      pending_ = path;
      if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
    }
    wake_.notify_all();
  }

 private:
  void run() {
    bool apartment = false;
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
      apartment = true;
    } catch (const winrt::hresult_error&) {
      // Already in an apartment; the process-wide MTA (main.cpp) keeps WinRT usable.
    }
    for (;;) {
      QString path;
      {
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] { return stop_ || pending_.has_value(); });
        if (stop_) break;
        path = *std::exchange(pending_, std::nullopt);
      }
      apply(path);
    }
    if (apartment) winrt::uninit_apartment();
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
          StorageFile::GetFileFromPathAsync(winrt::hstring(native.toStdWString())).get();
      LockScreen::SetImageFileAsync(storageFile).get();
      qInfo().noquote() << "Wallpapers: lock screen set:" << path;
    } catch (const winrt::hresult_error& error) {
      qWarning().noquote() << "Wallpapers: lock screen not set:"
                           << QString::fromWCharArray(error.message().c_str()) << "(" << path
                           << ")";
    } catch (const std::exception& error) {
      qWarning().noquote() << "Wallpapers: lock screen not set:" << error.what() << "(" << path
                           << ")";
    } catch (...) {
      qWarning().noquote() << "Wallpapers: lock screen not set (" << path << ")";
    }
  }

  const QString cacheDir_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::optional<QString> pending_;
  bool stop_ = false;
  std::thread thread_;
};

}  // namespace

std::unique_ptr<LockScreenSetter> makeLockScreenSetter(const QString& cacheDir) {
  return std::make_unique<WinrtLockScreenSetter>(cacheDir);
}

}  // namespace wabar::wallpapers
