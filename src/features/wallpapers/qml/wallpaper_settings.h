#pragma once

#include <QtQml/qqmlregistration.h>

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QString>

namespace waku::wallpapers {

class Wallpapers;

// Configures the Wallpapers service from shell.qml without a widget. Declare it next to the
// windows:
//
//     ShellRoot {
//         WallpaperSettings { imagePath: "C:/Pictures/Wallpapers"; hotkey: "alt+e";
//                             engine: true; animation: "split" }
//         Panel { ... }
//     }
//
// Not an item: the host calls activate() once the shell is installed (see Scope in
// host/qml/scope.h), which creates the Wallpapers singleton, hands it these values and creates the
// gallery window (the strip the hotkey and `wakuc wallpaper gallery` open), so the hotkey and
// the command-line client work with no widget. The properties mean what they mean on the
// Wallpapers widget; a change after activation is applied at once. Destroying the object (a
// reload, or quitting) switches the hotkey and the folder off again.
//
// The service is shared with any Wallpapers widget: whoever configured it last wins. Declare a
// WallpaperSettings or a Wallpapers widget, not both, or two galleries open at once.
//
// Threads: GUI thread only.
class WallpaperSettings : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString imagePath MEMBER imagePath_ NOTIFY changed FINAL)
  Q_PROPERTY(QString hotkey MEMBER hotkey_ NOTIFY changed FINAL)
  // "cursor" (default), "primary" or "active": the screen the gallery opens on.
  Q_PROPERTY(QString hotkeyScreen MEMBER hotkeyScreen_ NOTIFY changed FINAL)
  // Seconds between random wallpapers, 0 for never.
  Q_PROPERTY(int autoChange MEMBER autoChange_ NOTIFY changed FINAL)
  Q_PROPERTY(bool engine MEMBER engine_ NOTIFY changed FINAL)
  // circle, slide_top, diamond, split or fade.
  Q_PROPERTY(QString animation MEMBER animation_ NOTIFY changed FINAL)
  // How the gallery looks, as on the Wallpapers widget.
  Q_PROPERTY(int imageWidth MEMBER imageWidth_ NOTIFY changed FINAL)
  Q_PROPERTY(QString orientation MEMBER orientation_ NOTIFY changed FINAL)
  Q_PROPERTY(int cornerRadius MEMBER cornerRadius_ NOTIFY changed FINAL)
  Q_PROPERTY(QColor accentColor MEMBER accentColor_ NOTIFY changed FINAL)
  Q_PROPERTY(bool active READ active NOTIFY activeChanged FINAL)

 public:
  explicit WallpaperSettings(QObject* parent = nullptr);
  ~WallpaperSettings() override;

  bool active() const { return active_; }

  // Configures the service of this object's QML engine. Safe to call again.
  Q_INVOKABLE void activate();

 signals:
  void changed();
  void activeChanged();

 private:
  void apply();
  void createGallery();

  QString imagePath_;
  QString hotkey_ = QStringLiteral("alt+e");
  QString hotkeyScreen_ = QStringLiteral("cursor");
  int autoChange_ = 0;
  bool engine_ = false;
  QString animation_ = QStringLiteral("split");
  int imageWidth_ = 240;
  QString orientation_ = QStringLiteral("portrait");
  int cornerRadius_ = 0;
  QColor accentColor_ = Qt::white;

  bool active_ = false;
  QPointer<Wallpapers> service_;
  QPointer<QObject> gallery_;
};

}  // namespace waku::wallpapers
