#include "features/wallpapers/qml/wallpapers_service.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QQmlEngine>
#include <QRandomGenerator>
#include <QScreen>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>
#include <exception>
#include <utility>

#include "features/wallpapers/wallpaper_files.h"
#include "features/wallpapers/win32/wallpaper_hotkey_win.h"
#include "features/wallpapers/win32/window_focus.h"
#include "kernel/qt/app_data.h"
#include "kernel/qt/live_desktop.h"
#include "kernel/win32/window_focus.h"

namespace wabar::wallpapers {
namespace {

std::function<std::unique_ptr<Wallpapers>()>& testFactory() {
  static std::function<std::unique_ptr<Wallpapers>()> factory;
  return factory;
}

template <typename F>
void guarded(const char* what, F&& body) {
  try {
    body();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Wallpapers:" << what << "failed:" << error.what();
  } catch (...) {
    qWarning().noquote() << "Wallpapers:" << what << "failed";
  }
}

std::shared_ptr<ThumbnailCache> defaultCache() {
  return std::make_shared<ThumbnailCache>(
      QDir(kernel::appDataDir()).filePath(QStringLiteral("wallpaper_thumbnails")));
}

bool hasFolderPart(const QString& target) {
  return target.contains(QLatin1Char('/')) || target.contains(QLatin1Char('\\')) ||
         target.contains(QLatin1Char(':'));
}

QString nativePath(const QString& path) {
  return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

ResolvedWallpaper validateImageFile(const QString& path, const QStringList& types) {
  const QFileInfo info(path);
  if (!info.exists()) return {{}, QStringLiteral("no such file: %1").arg(nativePath(path))};
  if (!info.isFile()) return {{}, QStringLiteral("not a file: %1").arg(nativePath(path))};
  if (!types.contains(info.suffix().toLower())) {
    return {{},
            QStringLiteral("not a supported image type (%1): %2")
                .arg(types.join(QStringLiteral(", ")), nativePath(path))};
  }
  QImageReader reader(info.absoluteFilePath());
  if (!reader.canRead()) {
    return {{}, QStringLiteral("cannot read the image: %1").arg(nativePath(path))};
  }
  return {nativePath(path), {}};
}

QString okAnswer(const QString& path) { return QStringLiteral("ok: ") + path; }
QString errorAnswer(const QString& why) { return QStringLiteral("error: ") + why; }

}  // namespace

ResolvedWallpaper resolveWallpaperTarget(const QString& rawTarget, const QString& folder) {
  const QString target = rawTarget.trimmed();
  if (target.isEmpty()) return {{}, QStringLiteral("no file given")};
  const QStringList types = supportedImageTypes();
  if (hasFolderPart(target)) return validateImageFile(target, types);

  if (folder.isEmpty()) {
    return {{},
            QStringLiteral("no wallpaper folder is configured (WallpaperSettings.imagePath); "
                           "give a full path")};
  }
  const QStringList all = collectImageFiles({folder}, types);
  auto find = [&all](const QString& name) {
    QStringList found;
    for (const QString& file : all) {
      if (QFileInfo(file).fileName().compare(name, Qt::CaseInsensitive) == 0) found << file;
    }
    return found;
  };
  QStringList matches = find(target);
  if (matches.isEmpty()) {
    // "sunset" may mean sunset.jpg or sunset.png.
    for (const QString& type : types) matches << find(target + QLatin1Char('.') + type);
  }
  if (matches.size() > 1) {
    QStringList shown;
    for (const QString& match : matches) shown << nativePath(match);
    return {{},
            QStringLiteral("'%1' is ambiguous in %2: %3")
                .arg(target, QDir::toNativeSeparators(folder), shown.join(QStringLiteral("; ")))};
  }
  if (matches.size() == 1) return validateImageFile(matches.first(), types);
  // Not a listed image: a real file of the folder gets the precise reason (wrong type, ...).
  const QString inFolder = QDir(folder).filePath(target);
  if (QFileInfo(inFolder).isFile()) return validateImageFile(inFolder, types);
  return {{},
          QStringLiteral("no wallpaper named '%1' in %2")
              .arg(target, QDir::toNativeSeparators(folder))};
}

Wallpapers::Wallpapers(QObject* parent) : Wallpapers(Deps{}, parent) {}

Wallpapers::Wallpapers(Deps deps, QObject* parent) : QObject(parent), deps_(std::move(deps)) {
  cache_ = deps_.cache ? deps_.cache : defaultCache();
  if (!deps_.backend && deps_.realBackend && kernel::liveDesktop(QStringLiteral("wallpapers"))) {
    deps_.backend = [] { return makeDesktopWallpaperBackend(); };
  }
  if (!deps_.dryRun) deps_.dryRun = qEnvironmentVariableIntValue("WABAR_WALLPAPER_DRYRUN") != 0;
  if (deps_.backend) {
    guarded("backend", [&] { backend_ = deps_.backend(); });
  }
  available_ = backend_ != nullptr;
  if (!available_) return;

  WallpaperManager::Dependencies managerDeps;
  managerDeps.backend = deps_.backend;
  managerDeps.picker = deps_.picker;
  manager_ = std::make_unique<WallpaperManager>(std::move(managerDeps));
  if (!deps_.hotkeys) deps_.hotkeys = makeWin32HotkeyHost();
  readCurrent();
}

Wallpapers::~Wallpapers() {
  if (transition_) transition_->cancel();
  if (deps_.hotkeys) deps_.hotkeys->unregisterKey();
  if (manager_) manager_->stop();
}

Wallpapers* Wallpapers::create(QQmlEngine* engine, QJSEngine*) {
  // The engine owns the returned singleton and deletes its providers first.
  Wallpapers* service = nullptr;
  if (testFactory()) service = testFactory()().release();
  if (!service) service = new Wallpapers(nullptr);
  if (engine) {
    engine->addImageProvider(QStringLiteral("wallpaper"),
                             new WallpaperThumbnailProvider(service->cache()));
  }
  return service;
}

void Wallpapers::setFactoryForTests(std::function<std::unique_ptr<Wallpapers>()> factory) {
  testFactory() = std::move(factory);
}

void Wallpapers::setImagePath(const QString& path) {
  if (path == imagePath_) return;
  imagePath_ = path;
  applyToManager();
  emit imagePathChanged();
  refresh();
}

void Wallpapers::configure(const QString& imagePath, const QString& hotkey,
                           const QString& hotkeyScreen, int autoChangeSeconds) {
  guarded("configure", [&] {
    hotkeyScreen_ = hotkeyScreen.isEmpty() ? QStringLiteral("cursor") : hotkeyScreen;
    autoChangeSeconds_ = autoChangeSeconds;
    setImagePath(imagePath);
    applyToManager();
    if (!available_) return;
    if (hotkey == hotkey_) return;
    hotkey_ = hotkey;
    deps_.hotkeys->unregisterKey();
    if (hotkey.trimmed().isEmpty()) return;
    const auto combo = parseHotkey(hotkey);
    if (!combo) {
      qWarning().noquote() << "Wallpapers: cannot parse hotkey" << hotkey;
    } else if (!deps_.hotkeys->registerKey(*combo, [this] { onHotkey(); })) {
      qWarning().noquote() << "Wallpapers: could not register hotkey" << hotkey
                           << "(already taken?)";
    }
  });
}

void Wallpapers::configureEngine(bool enabled, const QString& animation) {
  engineEnabled_ = enabled;
  animation_ = animation.isEmpty() ? QStringLiteral("split") : animation;
}

void Wallpapers::applyToManager() {
  if (!manager_) return;
  manager_->configure(imagePath_.isEmpty() ? QStringList() : QStringList{imagePath_},
                      autoChangeSeconds_, autoChangeSeconds_ > 0, {});
}

void Wallpapers::refresh() {
  guarded("refresh", [&] {
    readCurrent();
    QVariantList list;
    const QString currentKey = normalizedPathKey(current_);
    if (!imagePath_.isEmpty()) {
      for (const QString& file : collectImageFiles({imagePath_})) {
        const QFileInfo info(file);
        const QString url =
            QStringLiteral("image://wallpaper/") +
            QString::fromLatin1(QUrl::toPercentEncoding(QDir::fromNativeSeparators(file), "/:")) +
            QLatin1Char('?') + QString::number(info.lastModified().toSecsSinceEpoch());
        list.append(QVariantMap{
            {QStringLiteral("path"), file},
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("thumbnail"), url},
            {QStringLiteral("current"),
             !current_.isEmpty() && normalizedPathKey(file) == currentKey},
        });
      }
    }
    files_ = list;
    emit filesChanged();
  });
}

void Wallpapers::readCurrent() {
  if (!backend_) return;
  QString now;
  guarded("current wallpaper", [&] {
    const auto monitors = backend_->monitors();
    now = backend_->wallpaperOf(monitors.empty() ? QString() : monitors.front().id);
  });
  if (now == current_) return;
  current_ = now;
  emit currentChanged();
}

void Wallpapers::setWallpaper(const QString& path) { setWallpaperImpl(path, false); }

void Wallpapers::setWallpaperImpl(const QString& path, bool dryRun) {
  if (!available_ || !QFileInfo(path).isFile()) return;
  guarded("set wallpaper", [&] {
    if (transition_) {
      transition_->cancel();  // a change during the animation supersedes it
      transition_.reset();
    }
    if (engineEnabled_) {
      playTransition(path, dryRun);
    } else {
      commitWallpaper(path, dryRun);
    }
  });
}

void Wallpapers::playTransition(const QString& path, bool dryRun) {
  transition_ = deps_.transition ? deps_.transition(path, animation_)
                                 : std::make_unique<WallpaperTransition>(path, animation_);
  if (!transition_) {
    commitWallpaper(path, dryRun);
    return;
  }
  connect(transition_.get(), &WallpaperTransition::skipped, this, [this](const QString& reason) {
    // The same reason repeats on every change, so say it once.
    if (std::exchange(skipLogged_, true)) return;
    qWarning().noquote() << "Wallpaper animation skipped:" << reason;
  });
  // finished() follows skipped(), so a skipped animation sets the wallpaper at once.
  connect(transition_.get(), &WallpaperTransition::finished, this, [this, path, dryRun] {
    guarded("set wallpaper", [&] { commitWallpaper(path, dryRun); });
  });
  transition_->start();
}

void Wallpapers::commitWallpaper(const QString& path, bool dryRun) {
  if (deps_.dryRun || dryRun) {
    qInfo().noquote() << "Wallpapers: dry run, not setting" << path;
    return;
  }
  manager_->changeBackground(path);
  refresh();
}

void Wallpapers::random() {
  if (!available_) return;
  guarded("random wallpaper", [&] {
    if (!engineEnabled_) {
      manager_->changeBackground();
      refresh();
      return;
    }
    // Pick here (never the one shown now) so the change can animate like a gallery click.
    const QStringList all = collectImageFiles({imagePath_});
    if (all.isEmpty()) return;
    QString next;
    do {
      const int count = static_cast<int>(all.size());
      next = all.at(deps_.picker ? deps_.picker(count) % count
                                 : static_cast<int>(QRandomGenerator::global()->bounded(count)));
    } while (all.size() > 1 && normalizedPathKey(next) == normalizedPathKey(current_) &&
             (deps_.picker == nullptr));
    setWallpaper(next);
  });
}

// --- wabarc wallpaper ...

QString Wallpapers::controlStart(const QString& path, bool dryRun) {
  setWallpaperImpl(path, dryRun);
  return okAnswer(path);
}

QString Wallpapers::controlSet(const QString& target, bool dryRun) {
  QString answer;
  guarded("wallpaper set", [&] {
    if (!available_) {
      answer = errorAnswer(QStringLiteral("the wallpaper service is not available"));
      return;
    }
    const ResolvedWallpaper resolved = resolveWallpaperTarget(target, imagePath_);
    answer = resolved.ok() ? controlStart(resolved.path, dryRun) : errorAnswer(resolved.error);
  });
  return answer.isEmpty() ? errorAnswer(QStringLiteral("the wallpaper could not be set")) : answer;
}

QString Wallpapers::controlRandom(bool dryRun) {
  QString answer;
  guarded("wallpaper random", [&] {
    if (!available_) {
      answer = errorAnswer(QStringLiteral("the wallpaper service is not available"));
      return;
    }
    readCurrent();
    const QStringList all = collectImageFiles({imagePath_});
    if (all.isEmpty()) {
      answer = errorAnswer(QStringLiteral("no wallpapers found in %1")
                               .arg(imagePath_.isEmpty() ? QStringLiteral("(no folder configured)")
                                                         : QDir::toNativeSeparators(imagePath_)));
      return;
    }
    // Never the one shown now, unless it is the only one.
    const QString currentKey = normalizedPathKey(current_);
    QStringList candidates;
    for (const QString& file : all) {
      if (normalizedPathKey(file) != currentKey) candidates << file;
    }
    if (candidates.isEmpty()) candidates = all;
    const int count = static_cast<int>(candidates.size());
    const int index = deps_.picker ? deps_.picker(count) % count
                                   : static_cast<int>(QRandomGenerator::global()->bounded(count));
    answer = controlStart(nativePath(candidates.at(index)), dryRun);
  });
  return answer.isEmpty() ? errorAnswer(QStringLiteral("the wallpaper could not be set")) : answer;
}

// step +1 is the file after the current one, -1 the one before; both wrap around. When the
// current wallpaper is not in the folder, next is the first file and previous the last.
QString Wallpapers::controlStep(int step, bool dryRun) {
  QString answer;
  guarded("wallpaper step", [&] {
    if (!available_) {
      answer = errorAnswer(QStringLiteral("the wallpaper service is not available"));
      return;
    }
    readCurrent();
    const QStringList all = collectImageFiles({imagePath_});
    if (all.isEmpty()) {
      answer = errorAnswer(QStringLiteral("no wallpapers found in %1")
                               .arg(imagePath_.isEmpty() ? QStringLiteral("(no folder configured)")
                                                         : QDir::toNativeSeparators(imagePath_)));
      return;
    }
    const int count = static_cast<int>(all.size());
    const QString currentKey = normalizedPathKey(current_);
    int at = -1;
    for (int i = 0; i < count && at < 0; ++i) {
      if (normalizedPathKey(all.at(i)) == currentKey) at = i;
    }
    const int target = at < 0 ? (step > 0 ? 0 : count - 1) : (at + step + count) % count;
    answer = controlStart(nativePath(all.at(target)), dryRun);
  });
  return answer.isEmpty() ? errorAnswer(QStringLiteral("the wallpaper could not be set")) : answer;
}

QString Wallpapers::controlNext(bool dryRun) { return controlStep(1, dryRun); }
QString Wallpapers::controlPrevious(bool dryRun) { return controlStep(-1, dryRun); }

QString Wallpapers::controlGallery() {
  if (!available_) return errorAnswer(QStringLiteral("the wallpaper service is not available"));
  toggleGallery();
  return okAnswer(galleryOpen_ ? QStringLiteral("gallery opened")
                               : QStringLiteral("gallery closed"));
}

QString Wallpapers::screenNameFor(const QString& mode) const {
  QScreen* chosen = nullptr;
  if (mode == QLatin1String("cursor")) {
    chosen = QGuiApplication::screenAt(QCursor::pos());
  } else if (mode == QLatin1String("active")) {
    if (const auto monitor = foregroundMonitorRect()) {
      for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->geometry().topLeft() == monitor->topLeft()) chosen = screen;
      }
    }
  }
  if (!chosen) chosen = QGuiApplication::primaryScreen();
  return chosen ? chosen->name() : QString();
}

void Wallpapers::toggleGallery() {
  if (galleryOpen_) {
    closeGallery();
    return;
  }
  if (!available_) return;
  guarded("gallery", [&] {
    refresh();
    galleryScreen_ = screenNameFor(hotkeyScreen_);
    galleryOpen_ = true;
    emit galleryOpenChanged();
  });
}

void Wallpapers::closeGallery() {
  if (!galleryOpen_) return;
  galleryOpen_ = false;
  emit galleryOpenChanged();
}

void Wallpapers::focusWindow(QWindow* window) {
  if (!window || !wabar::kernel::liveDesktop(QStringLiteral("wallpapers"))) return;
  window->requestActivate();
  wabar::kernel::forceForegroundFocus(static_cast<std::uintptr_t>(window->winId()));
}

// --- image provider

WallpaperThumbnailProvider::WallpaperThumbnailProvider(
    std::shared_ptr<wabar::wallpapers::ThumbnailCache> cache)
    : QQuickImageProvider(QQuickImageProvider::Image,
                          QQuickImageProvider::ForceAsynchronousImageLoading),
      cache_(std::move(cache)) {}

QImage WallpaperThumbnailProvider::requestImage(const QString& id, QSize* size,
                                                const QSize& requestedSize) {
  if (size) *size = QSize(0, 0);
  try {
    QString path = id.left(id.indexOf(QLatin1Char('?')));
    if (!QFileInfo::exists(path)) path = QUrl::fromPercentEncoding(path.toUtf8());
    if (!cache_ || !QFileInfo(path).isFile()) return {};
    const QSize box =
        requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(480, 270);
    const QString name = cache_->key(path, box.width(), box.height());
    if (!name.isEmpty()) {
      if (auto cached = cache_->load(name)) {
        if (size) *size = cached->size();
        return *cached;
      }
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isValid()) reader.setScaledSize(original.scaled(box, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull()) return {};
    if (!original.isValid())
      image = image.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!name.isEmpty()) cache_->save(name, image);
    if (size) *size = image.size();
    return image;
  } catch (...) {
    return {};
  }
}

}  // namespace wabar::wallpapers
