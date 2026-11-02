#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace waku::quickshell {

// Quickshell.Services.SystemTray's Status. Windows tray icons are always Active.
namespace TrayStatus {
Q_NAMESPACE
QML_NAMED_ELEMENT(Status)
enum Value { Passive = 0, Active = 1, NeedsAttention = 2 };
Q_ENUM_NS(Value)
}  // namespace TrayStatus

// Quickshell.Services.SystemTray's SystemTrayItem for one Windows notification-area icon. A C++
// type because QML cannot declare the property `id`. It holds values and forwards the actions as
// signals; SystemTray.qml creates one per tray icon, sets it from the tray model's row with
// setRow() and connects the signals to the tray service.
//
//   id               the program's executable name without extension, lower case; the icon's
//                    number when the row has no executable
//   title, tooltipTitle  the icon's tooltip, or `id` when it has none; tooltipDescription is empty
//   icon             the row's image url
//   status           Status.Active; category "ApplicationStatus"
//   hasMenu, onlyMenu, menu  false, false, null: the program draws its own menu on a right click
//
// Actions: activate() (left click), secondaryActivate() (middle click), scroll() (nothing) and
// display() (right click) emit the matching *Requested signal. Threading: GUI thread only.
class SystemTrayItem : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool hasMenu READ hasMenu CONSTANT FINAL)
  Q_PROPERTY(QObject* menu READ menu CONSTANT FINAL)
  Q_PROPERTY(bool onlyMenu READ onlyMenu CONSTANT FINAL)
  Q_PROPERTY(QString id READ id NOTIFY changed FINAL)
  Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
  Q_PROPERTY(QString icon READ icon NOTIFY changed FINAL)
  Q_PROPERTY(QString category READ category CONSTANT FINAL)
  Q_PROPERTY(int status READ status CONSTANT FINAL)
  Q_PROPERTY(QString tooltipTitle READ tooltipTitle NOTIFY changed FINAL)
  Q_PROPERTY(QString tooltipDescription READ tooltipDescription CONSTANT FINAL)
  // The icon's number in the tray service, -1 before the first row.
  Q_PROPERTY(int trayId READ trayId NOTIFY changed FINAL)

 public:
  explicit SystemTrayItem(QObject* parent = nullptr) : QObject(parent) {}

  bool hasMenu() const { return false; }
  QObject* menu() const { return nullptr; }
  bool onlyMenu() const { return false; }
  QString id() const { return id_; }
  QString title() const { return title_; }
  QString icon() const { return icon_; }
  QString category() const { return QStringLiteral("ApplicationStatus"); }
  int status() const { return TrayStatus::Active; }
  QString tooltipTitle() const { return tooltip_; }
  QString tooltipDescription() const { return {}; }
  int trayId() const { return trayId_; }

  // The tray model's row: id (number), tooltip, exe, iconUrl. Emits `changed` when a value differs.
  Q_INVOKABLE void setRow(const QVariantMap& row);

  Q_INVOKABLE void activate() { emit activateRequested(); }
  Q_INVOKABLE void secondaryActivate() { emit secondaryActivateRequested(); }
  Q_INVOKABLE void scroll(qreal delta, bool horizontal) { emit scrollRequested(delta, horizontal); }
  Q_INVOKABLE void display(QObject* parentWindow, qreal relativeX, qreal relativeY);

  // The program name an exe path stands for: file name without extension, lower case.
  static QString idFromExe(const QString& exe);

 signals:
  void changed();
  void ready();
  void activateRequested();
  void secondaryActivateRequested();
  void scrollRequested(qreal delta, bool horizontal);
  void displayRequested();

 private:
  QString id_;
  QString title_;
  QString icon_;
  QString tooltip_;
  int trayId_ = -1;
  bool ready_ = false;
};

}  // namespace waku::quickshell
