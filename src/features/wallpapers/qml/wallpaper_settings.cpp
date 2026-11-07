#include "features/wallpapers/qml/wallpaper_settings.h"

#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <exception>

#include "features/wallpapers/qml/wallpapers_service.h"

namespace waku::wallpapers {
namespace {

constexpr auto kGalleryUrl = "qrc:/qt/qml/waku/widgets/WallpaperGallery.qml";

}  // namespace

WallpaperSettings::WallpaperSettings(QObject* parent) : QObject(parent) {
  // A property set after activation reaches the service at once.
  connect(this, &WallpaperSettings::changed, this, [this] {
    if (active_) apply();
  });
}

WallpaperSettings::~WallpaperSettings() {
  try {
    delete gallery_.data();
    if (active_ && service_) {
      service_->configure(QString(), QString(), QStringLiteral("cursor"), 0);
    }
  } catch (...) {
    qWarning() << "WallpaperSettings: could not switch the wallpaper service off";
  }
}

void WallpaperSettings::activate() {
  try {
    if (active_) return;
    QQmlEngine* engine = qmlEngine(this);
    if (!engine) {
      qWarning() << "WallpaperSettings: no QML engine, nothing configured";
      return;
    }
    service_ = engine->singletonInstance<Wallpapers*>(QStringLiteral("waku.services"),
                                                      QStringLiteral("Wallpapers"));
    if (!service_) {
      qWarning() << "WallpaperSettings: the Wallpapers service is not available";
      return;
    }
    active_ = true;
    apply();
    if (service_->available()) createGallery();
    apply();
    emit activeChanged();
  } catch (const std::exception& e) {
    qWarning().noquote() << "WallpaperSettings: activate failed:" << e.what();
  } catch (...) {
    qWarning() << "WallpaperSettings: activate failed";
  }
}

void WallpaperSettings::apply() {
  if (!service_) return;
  service_->configure(imagePath_, hotkey_, hotkeyScreen_, autoChange_);
  if (gallery_) {
    gallery_->setProperty("imageWidth", imageWidth_);
    gallery_->setProperty("orientation", orientation_);
    gallery_->setProperty("cornerRadius", cornerRadius_);
    gallery_->setProperty("accentColor", accentColor_);
  }
}

void WallpaperSettings::createGallery() {
  // The gallery is QML (waku.widgets, which depends on this module), so it is loaded by URL.
  QQmlEngine* engine = qmlEngine(this);
  QQmlComponent component(engine, QUrl(QString::fromLatin1(kGalleryUrl)));
  QObject* gallery =
      component.isError()
          ? nullptr
          : component.createWithInitialProperties({{QStringLiteral("imageWidth"), imageWidth_},
                                                   {QStringLiteral("orientation"), orientation_},
                                                   {QStringLiteral("cornerRadius"), cornerRadius_},
                                                   {QStringLiteral("accentColor"), accentColor_}});
  if (!gallery) {
    qWarning().noquote() << "WallpaperSettings: cannot create the gallery:"
                         << component.errorString();
    return;
  }
  gallery->setParent(this);
  gallery_ = gallery;
}

}  // namespace waku::wallpapers
