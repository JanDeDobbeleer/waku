#include "features/wallpapers/qml/wallpapers_service.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QQmlEngine>
#include <QRandomGenerator>
#include <QScreen>
#include <QThreadPool>
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

// The PNG thumbnails of the old image://wallpaper provider; the gallery uses image://thumb now.
// Nothing pruned that folder, so it is removed once, off the GUI thread.
void removeRetiredThumbnailFolder() {
  if (!kernel::liveDesktop(QStringLiteral("wallpapers"))) return;
  const QString folder =
      QDir(kernel::appDataDir()).filePath(QStringLiteral("wallpaper_thumbnails"));
  if (!QFileInfo::exists(folder)) return;
  QThreadPool::globalInstance()->start([folder] { QDir(folder).removeRecursively(); });
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
  removeRetiredThumbnailFolder();
  if (!deps_.backend && deps_.realBackend && kernel::liveDesktop(QStringLiteral("wallpapers"))) {
    deps_.backend = [] { return makeDesktopWallpaperBackend(); };
  }
  if (!deps_.dryRun) deps_.dryRun = qEnvironmentVariableIntValue("WABAR_WALLPAPER_DRYRUN") != 0;
  if (!deps_.lockScreen && deps_.realBackend && kernel::liveDesktop(QStringLiteral("wallpapers"))) {
    guarded("lock screen", [&] { deps_.lockScreen = makeLockScreenSetter(kernel::appDataDir()); });
  }
  if (deps_.backend) {
    guarded("backend", [&] { backend_ = deps_.backend(); });
  }
  available_ = backend_ != nullptr;
  if (!available_) return;

  WallpaperManager::Dependencies managerDeps;
  managerDeps.backend = deps_.backend;
  managerDeps.picker = deps_.picker;
  // A timed change takes the path of a manual random(): lock screen, dry run and animation.
  managerDeps.timedChange = [this] { random(); };
  manager_ = std::make_unique<WallpaperManager>(std::move(managerDeps));
  // `--no-hotkeys` switches this hotkey off with the others (host::Hotkeys asks the same name).
  if (!deps_.hotkeys && kernel::liveDesktop(QStringLiteral("hotkeys")))
    deps_.hotkeys = makeWin32HotkeyHost();
  readCurrent();
}

Wallpapers::~Wallpapers() {
  if (transition_) transition_->cancel();
  if (deps_.hotkeys) deps_.hotkeys->unregisterKey();
  if (manager_) manager_->stop();
}

Wallpapers* Wallpapers::create(QQmlEngine*, QJSEngine*) {
  // The engine owns the returned singleton.
  Wallpapers* service = nullptr;
  if (testFactory()) service = testFactory()().release();
  if (!service) service = new Wallpapers(nullptr);
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
    if (!deps_.hotkeys) return;
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

void Wallpapers::setSetLockScreen(bool enabled) {
  if (enabled == setLockScreen_) return;
  setLockScreen_ = enabled;
  emit setLockScreenChanged();
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
        list.append(QVariantMap{
            {QStringLiteral("path"), file},
            {QStringLiteral("name"), QFileInfo(file).fileName()},
            {QStringLiteral("current"),
             !current_.isEmpty() && normalizedPathKey(file) == currentKey},
        });
      }
    }
    // An unchanged list keeps the gallery's delegates and their loaded thumbnails.
    if (list == files_) return;
    files_ = std::move(list);
    emit filesChanged();
  });
}

void Wallpapers::markCurrent() {
  const QString currentKey = normalizedPathKey(current_);
  bool changed = false;
  for (QVariant& row : files_) {
    QVariantMap map = row.toMap();
    const bool current =
        !current_.isEmpty() &&
        normalizedPathKey(map.value(QStringLiteral("path")).toString()) == currentKey;
    if (map.value(QStringLiteral("current")).toBool() == current) continue;
    map.insert(QStringLiteral("current"), current);
    row = map;
    changed = true;
  }
  if (changed) emit filesChanged();
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
  markCurrent();
  emit currentChanged();
}

void Wallpapers::setWallpaper(const QString& path) { setWallpaperImpl(path, false); }

void Wallpapers::setWallpaperImpl(const QString& path, bool dryRun) {
  if (!available_ || !QFileInfo(path).isFile()) return;
  guarded("set wallpaper", [&] {
    if (transition_) {
      // A change during the animation supersedes it. This can run inside the old transition's
      // own signal emission (a handler of currentChanged re-enters here), so the old one is
      // deleted later, never under its own stack frame.
      transition_->cancel();
      transition_->disconnect(this);
      transition_.release()->deleteLater();
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
  // finished() follows skipped(), so a skipped animation sets the wallpaper at once. Queued, so
  // the commit (and every QML handler of currentChanged it triggers) runs after the transition
  // has left its own emission; a transition superseded meanwhile commits nothing.
  const QPointer<WallpaperTransition> played(transition_.get());
  connect(
      transition_.get(), &WallpaperTransition::finished, this,
      [this, played, path, dryRun] {
        if (!played || played != transition_.get()) return;
        guarded("set wallpaper", [&] { commitWallpaper(path, dryRun); });
      },
      Qt::QueuedConnection);
  transition_->start();
}

void Wallpapers::commitWallpaper(const QString& path, bool dryRun) {
  if (deps_.dryRun || dryRun) {
    qInfo().noquote() << "Wallpapers: dry run, not setting" << path;
    updateLockScreen(path, true);
    return;
  }
  manager_->changeBackground(path);
  // The manager has just listed the folder; only the current mark moves.
  readCurrent();
  // `current` comes from Windows, so a failed set leaves the lock screen alone.
  if (!current_.isEmpty() && normalizedPathKey(current_) == normalizedPathKey(path)) {
    updateLockScreen(path, false);
  }
}

void Wallpapers::updateLockScreen(const QString& path, bool dryRun) {
  if (!setLockScreen_) return;
  guarded("lock screen", [&] {
    if (const auto reason = lockScreenSkipReason(path)) {
      qInfo().noquote() << "Wallpapers: lock screen left alone," << *reason << ":"
                        << nativePath(path);
      return;
    }
    if (dryRun) {
      qInfo().noquote() << "Wallpapers: lock screen (dry run):" << nativePath(path);
      return;
    }
    if (deps_.lockScreen) deps_.lockScreen->set(nativePath(path));
  });
}

void Wallpapers::random() {
  if (!available_) return;
  guarded("random wallpaper", [&] {
    // The manager sets what it picks, so a dry run picks here too.
    if (!engineEnabled_ && !deps_.dryRun) {
      const QString before = normalizedPathKey(current_);
      manager_->changeBackground();
      refresh();
      if (!current_.isEmpty() && normalizedPathKey(current_) != before) {
        updateLockScreen(current_, false);
      }
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

}  // namespace wabar::wallpapers
