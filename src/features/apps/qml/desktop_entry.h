#pragma once

#include <QtQml/qqmlregistration.h>

#include <QList>
#include <QObject>
#include <QQmlListProperty>
#include <QString>
#include <QStringList>
#include <functional>

#include "features/apps/apps_platform.h"
#include "features/apps/catalog.h"

namespace waku::apps {

// Quickshell's DesktopAction: one extra command of an entry. waku entries have "Run as
// administrator" (shortcuts) and "Open file location" (shortcuts).
class DesktopAction : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("DesktopAction objects come from DesktopEntry.actions")
  Q_PROPERTY(QString id READ id CONSTANT FINAL)
  Q_PROPERTY(QString name READ name CONSTANT FINAL)
  Q_PROPERTY(QString icon READ icon CONSTANT FINAL)
  Q_PROPERTY(QString execString READ execString CONSTANT FINAL)
  Q_PROPERTY(QStringList command READ command CONSTANT FINAL)

 public:
  DesktopAction(QString id, QString name, std::function<void()> run, QObject* parent);

  QString id() const { return id_; }
  QString name() const { return name_; }
  QString icon() const { return QString(); }
  QString execString() const { return QString(); }
  QStringList command() const { return {}; }

  Q_INVOKABLE void execute();

 private:
  QString id_;
  QString name_;
  std::function<void()> run_;
};

// Quickshell's DesktopEntry for a Start Menu shortcut or a Store app (see AppRecord). The
// service owns the objects and keeps one per id across rescans, so a QML reference survives a
// refresh; `changed` fires when a rescan changes any field.
//
// `icon` is an image URL (image://appicon/<id>), which also passes through
// Quickshell.iconPath(). `execute()` starts the app like `AppsService.launch(id)`.
//
// GUI thread only.
class DesktopEntry : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("DesktopEntry objects come from DesktopEntries")
  Q_PROPERTY(QString id READ id NOTIFY changed FINAL)
  Q_PROPERTY(QString name READ name NOTIFY changed FINAL)
  Q_PROPERTY(QString genericName READ genericName NOTIFY changed FINAL)
  Q_PROPERTY(QString comment READ comment NOTIFY changed FINAL)
  Q_PROPERTY(QString icon READ icon NOTIFY changed FINAL)
  Q_PROPERTY(QString execString READ execString NOTIFY changed FINAL)
  Q_PROPERTY(QStringList command READ command NOTIFY changed FINAL)
  Q_PROPERTY(QString workingDirectory READ workingDirectory NOTIFY changed FINAL)
  Q_PROPERTY(QString startupClass READ startupClass NOTIFY changed FINAL)
  Q_PROPERTY(QStringList categories READ categories NOTIFY changed FINAL)
  Q_PROPERTY(QStringList keywords READ keywords NOTIFY changed FINAL)
  Q_PROPERTY(bool noDisplay READ noDisplay NOTIFY changed FINAL)
  Q_PROPERTY(bool runInTerminal READ runInTerminal NOTIFY changed FINAL)
  Q_PROPERTY(bool isApplication READ isApplication CONSTANT FINAL)
  Q_PROPERTY(bool isStoreApp READ isStoreApp NOTIFY changed FINAL)
  Q_PROPERTY(QString aumid READ aumid NOTIFY changed FINAL)
  Q_PROPERTY(QQmlListProperty<waku::apps::DesktopAction> actions READ actions NOTIFY changed FINAL)

 public:
  using Launch = std::function<bool(const QString& id, LaunchMode mode)>;

  DesktopEntry(AppRecord record, Launch launch, QObject* parent);

  const AppRecord& record() const { return record_; }
  // Replaces the record; emits `changed` when it differs.
  void setRecord(AppRecord record);

  QString id() const { return record_.id; }
  QString name() const { return record_.name; }
  QString genericName() const { return record_.genericName; }
  QString comment() const { return record_.comment; }
  QString icon() const;
  QString execString() const { return execStringFor(record_); }
  QStringList command() const { return commandFor(record_); }
  QString workingDirectory() const { return record_.workingDirectory; }
  QString startupClass() const { return startupClassFor(record_); }
  QStringList categories() const { return record_.categories; }
  QStringList keywords() const { return record_.keywords; }
  bool noDisplay() const { return false; }
  bool runInTerminal() const { return false; }
  bool isApplication() const { return true; }
  bool isStoreApp() const { return record_.kind == AppKind::Store; }
  QString aumid() const { return record_.aumid; }
  QQmlListProperty<DesktopAction> actions();

  // Starts the app (raises it when a Store app already runs).
  Q_INVOKABLE void execute();

 signals:
  void changed();

 private:
  void rebuildActions();

  AppRecord record_;
  Launch launch_;
  QList<DesktopAction*> actions_;
};

}  // namespace waku::apps
