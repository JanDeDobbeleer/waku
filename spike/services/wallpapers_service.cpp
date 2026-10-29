#include "spike/services/wallpapers_service.h"

#include <windows.h>

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QQmlEngine>
#include <QScreen>
#include <QUrl>
#include <QVariantMap>
#include <exception>

#include "platform/window_focus.h"
#include "widgets/wallpapers/wallpaper_files.h"

namespace wabar::services {
namespace {

namespace widgets = ::yasb::widgets;

bool g_disabled = false;

std::function<std::unique_ptr<Wallpapers>()>& testFactory() {
  static std::function<std::unique_ptr<Wallpapers>()> factory;
  return factory;
}

bool isOffscreen() { return QGuiApplication::platformName() == QStringLiteral("offscreen"); }

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

std::shared_ptr<widgets::ThumbnailCache> defaultCache() {
  return std::make_shared<widgets::ThumbnailCache>(
      QDir(qEnvironmentVariable("LOCALAPPDATA"))
          .filePath(QStringLiteral("YASB/wallpaper_thumbnails")));
}

// RegisterHotKey (documented) with a null window: WM_HOTKEY is posted to the registering thread's
// queue, which is the GUI thread, and Qt's message loop hands every message to native event
// filters. Owner: the Wallpapers singleton; removed in unregisterKey() and the destructor.
class RealHotkeyHost final : public HotkeyHost, public QAbstractNativeEventFilter {
 public:
  ~RealHotkeyHost() override { unregisterKey(); }

  bool registerKey(const HotkeyCombo& combo, std::function<void()> pressed) override {
    unregisterKey();
    constexpr unsigned kNoRepeat = 0x4000;  // MOD_NOREPEAT
    if (!RegisterHotKey(nullptr, kId, combo.modifiers | kNoRepeat, combo.virtualKey)) return false;
    pressed_ = std::move(pressed);
    registered_ = true;
    if (QCoreApplication::instance()) QCoreApplication::instance()->installNativeEventFilter(this);
    return true;
  }

  void unregisterKey() override {
    if (!registered_) return;
    UnregisterHotKey(nullptr, kId);
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
    registered_ = false;
    pressed_ = nullptr;
  }

  bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override {
    if (!registered_ || eventType != "windows_generic_MSG" || !message) return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_HOTKEY || msg->wParam != kId) return false;
    guarded("hotkey", [&] {
      if (pressed_) pressed_();
    });
    return true;
  }

 private:
  static constexpr int kId = 0x7A11;
  bool registered_ = false;
  std::function<void()> pressed_;
};

}  // namespace

std::optional<HotkeyCombo> parseHotkey(const QString& keys) {
  HotkeyCombo combo;
  for (QString part : keys.split(QLatin1Char('+'))) {
    part = part.simplified().remove(QLatin1Char(' ')).toLower();
    if (part.isEmpty()) continue;
    if (part == QLatin1String("alt")) {
      combo.modifiers |= 1;
    } else if (part == QLatin1String("ctrl") || part == QLatin1String("control")) {
      combo.modifiers |= 2;
    } else if (part == QLatin1String("shift")) {
      combo.modifiers |= 4;
    } else if (part == QLatin1String("win") || part == QLatin1String("windows") ||
               part == QLatin1String("super") || part == QLatin1String("meta")) {
      combo.modifiers |= 8;
    } else {
      if (combo.virtualKey != 0) return std::nullopt;
      if (part.size() == 1 && ((part[0] >= QLatin1Char('a') && part[0] <= QLatin1Char('z')) ||
                               (part[0] >= QLatin1Char('0') && part[0] <= QLatin1Char('9')))) {
        combo.virtualKey = static_cast<unsigned>(part[0].toUpper().unicode());
      } else if (part == QLatin1String("space")) {
        combo.virtualKey = VK_SPACE;
      } else if (part.size() >= 2 && part[0] == QLatin1Char('f')) {
        bool ok = false;
        const int number = part.mid(1).toInt(&ok);
        if (!ok || number < 1 || number > 24) return std::nullopt;
        combo.virtualKey = static_cast<unsigned>(VK_F1 + number - 1);
      } else {
        return std::nullopt;
      }
    }
  }
  if (combo.virtualKey == 0) return std::nullopt;
  return combo;
}

Wallpapers::Wallpapers(QObject* parent) : Wallpapers(Deps{}, parent) {}

Wallpapers::Wallpapers(Deps deps, QObject* parent) : QObject(parent), deps_(std::move(deps)) {
  cache_ = deps_.cache ? deps_.cache : defaultCache();
  if (!deps_.backend && deps_.realBackend && !g_disabled && !isOffscreen()) {
    deps_.backend = [] { return platform::makeDesktopWallpaperBackend(); };
  }
  if (deps_.backend) {
    guarded("backend", [&] { backend_ = deps_.backend(); });
  }
  available_ = backend_ != nullptr;
  if (!available_) return;

  widgets::WallpaperManager::Dependencies managerDeps;
  managerDeps.backend = deps_.backend;
  managerDeps.picker = deps_.picker;
  manager_ = std::make_unique<widgets::WallpaperManager>(std::move(managerDeps));
  if (!deps_.hotkeys) deps_.hotkeys = std::make_unique<RealHotkeyHost>();
  readCurrent();
}

Wallpapers::~Wallpapers() {
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

void Wallpapers::setDisabled(bool disabled) { g_disabled = disabled; }

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

void Wallpapers::applyToManager() {
  if (!manager_) return;
  manager_->configure(imagePath_.isEmpty() ? QStringList() : QStringList{imagePath_},
                      autoChangeSeconds_, autoChangeSeconds_ > 0, {}, {});
}

void Wallpapers::refresh() {
  guarded("refresh", [&] {
    readCurrent();
    QVariantList list;
    const QString currentKey = widgets::normalizedPathKey(current_);
    if (!imagePath_.isEmpty()) {
      for (const QString& file : widgets::collectImageFiles({imagePath_})) {
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
             !current_.isEmpty() && widgets::normalizedPathKey(file) == currentKey},
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

void Wallpapers::setWallpaper(const QString& path) {
  if (!available_ || !QFileInfo(path).isFile()) return;
  guarded("set wallpaper", [&] {
    manager_->changeBackground(path);
    refresh();
  });
}

void Wallpapers::random() {
  if (!available_) return;
  guarded("random wallpaper", [&] {
    manager_->changeBackground();
    refresh();
  });
}

QString Wallpapers::screenNameFor(const QString& mode) const {
  QScreen* chosen = nullptr;
  if (mode == QLatin1String("cursor")) {
    chosen = QGuiApplication::screenAt(QCursor::pos());
  } else if (mode == QLatin1String("active")) {
    if (const auto monitor = platform::foregroundMonitorRect()) {
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

// --- image provider

WallpaperThumbnailProvider::WallpaperThumbnailProvider(
    std::shared_ptr<yasb::widgets::ThumbnailCache> cache)
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

}  // namespace wabar::services
