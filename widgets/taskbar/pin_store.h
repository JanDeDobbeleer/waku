#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <map>

namespace yasb::widgets {

// The data side of PinManager: taskbar_pinned.json ({"pinned_apps": {id: metadata}, "pinned_order":
// [id]}) and the signal Python's TaskbarSignalBus sends to every taskbar. Pinned apps are global,
// so one store is shared by every taskbar widget (a service of the polling-worker registry, key
// `taskbar_pins`); it lives on the GUI thread and is only touched from there.
class PinStore : public QObject {
  Q_OBJECT

 public:
  // `filePath` is %LOCALAPPDATA%\YASB\taskbar_pinned.json in production (see defaultFile()).
  explicit PinStore(QString filePath, QObject* parent = nullptr);

  static QString defaultFile();
  const QString& filePath() const { return filePath_; }

  // Re-reads the file when it changed since the last load or save (PinManager's mtime cache). A
  // missing or unreadable file means no pinned apps.
  void load();
  void save();

  bool contains(const QString& uniqueId) const { return apps_.count(uniqueId) != 0; }
  const QStringList& order() const { return order_; }
  QString title(const QString& uniqueId) const { return text(uniqueId, "title"); }
  QString shortcutPath(const QString& uniqueId) const { return text(uniqueId, "shortcut_path"); }
  bool isEmpty() const { return apps_.empty(); }

  // PinManager.pin_app (position -1 appends); saves and emits ("pin", id).
  void pin(const QString& uniqueId, const QJsonObject& metadata, int position = -1);
  // PinManager.unpin_app; saves and emits ("unpin", id). False when the id was not pinned.
  bool unpin(const QString& uniqueId);
  // PinManager.update_pinned_order; saves and emits ("reorder", "") when the order changed.
  void updateOrder(const QStringList& newOrder);

 signals:
  // action is "pin", "unpin" or "reorder"; the id is empty for "reorder".
  void pinnedAppsChanged(const QString& action, const QString& uniqueId);

 private:
  QString text(const QString& uniqueId, const char* key) const;
  qint64 fileStamp() const;

  QString filePath_;
  std::map<QString, QJsonObject> apps_;
  QStringList order_;
  qint64 stamp_ = -1;
};

}  // namespace yasb::widgets
