#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QQmlParserStatus>
#include <QString>
#include <QStringList>

#include "host/hotkeys.h"

namespace wabar::host {

// A named action a key can trigger (Quickshell `GlobalShortcut`, without Hyprland):
//
//     GlobalShortcut {
//         name: "launcher"
//         description: "Toggle the launcher"
//         keys: "alt+space"          // optional
//         onPressed: launcher.active = !launcher.active
//     }
//
//   name, appid, description  identify the shortcut; `name` is what `wabarc shortcut <name>` fires
//   keys       "alt+e", "ctrl+shift+f5", "win+space" (see parseHotkey); empty: no key, the shortcut
//              fires only through `wabarc shortcut <name>` (bind the key in whkd or AutoHotkey)
//   pressed    true while the shortcut fires. Windows reports no key release for a registered
//              hotkey, so a key press is `pressed` then `released` at once
//   registered false when `keys` is empty, invalid, taken by another program, or the desktop is
//              not live (no hotkey is registered then). A taken combination is logged once
//
// A shortcut registers when its component completes and unregisters when it is destroyed (a shell
// reload destroys the old one). Threading: GUI thread only.
class GlobalShortcut : public QObject, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_ELEMENT
  Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged FINAL)
  Q_PROPERTY(QString appid READ appid WRITE setAppid NOTIFY appidChanged FINAL)
  Q_PROPERTY(
      QString description READ description WRITE setDescription NOTIFY descriptionChanged FINAL)
  Q_PROPERTY(QString keys READ keys WRITE setKeys NOTIFY keysChanged FINAL)
  Q_PROPERTY(bool pressed READ isPressed NOTIFY pressedChanged FINAL)
  Q_PROPERTY(bool registered READ isRegistered NOTIFY registeredChanged FINAL)

 public:
  explicit GlobalShortcut(QObject* parent = nullptr);
  ~GlobalShortcut() override;

  QString name() const { return name_; }
  void setName(const QString& name);
  QString appid() const { return appid_; }
  void setAppid(const QString& appid);
  QString description() const { return description_; }
  void setDescription(const QString& description);
  QString keys() const { return keys_; }
  void setKeys(const QString& keys);
  bool isPressed() const { return pressed_; }
  bool isRegistered() const { return handle_ != 0; }

  // Emits pressed then released, as a key press does. What the control pipe and WM_HOTKEY call.
  Q_INVOKABLE void fire();

  void classBegin() override {}
  void componentComplete() override;

  // Every live shortcut named `name`; false when there is none.
  static bool fireByName(const QString& name);
  // The names of the live shortcuts, sorted, without duplicates and without empty names.
  static QStringList names();

 signals:
  void nameChanged();
  void appidChanged();
  void descriptionChanged();
  void keysChanged();
  void pressedChanged();
  void registeredChanged();
  void pressed();
  void released();

 private:
  void applyKeys();
  void release();

  QString name_;
  QString appid_;
  QString description_;
  QString keys_;
  bool pressed_ = false;
  bool complete_ = false;
  Hotkeys::Handle handle_ = 0;
};

}  // namespace wabar::host
