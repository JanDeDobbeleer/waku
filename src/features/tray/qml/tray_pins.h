#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QString>
#include <QWindow>
#include <vector>

#include "features/tray/qml/tray_icon_model.h"
#include "features/tray/tray_state.h"

namespace wabar::tray {

class TrayPins;

// One side of a Tray widget, pinned or unpinned: rows are icon ids in display order. The roles
// are TrayIconModel's, resolved by looking the id up in that model. Only TrayPins changes the
// rows. GUI thread only.
class TrayPinList : public QAbstractListModel {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged FINAL)

 public:
  explicit TrayPinList(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  const std::vector<int>& ids() const { return ids_; }

 signals:
  void countChanged();

 private:
  friend class TrayPins;
  void setSource(const TrayIconModel* source) { source_ = source; }
  int rowOfId(int id) const;
  void insertAt(int row, int id);
  void removeId(int id);
  void moveRow(int from, int to);  // `to` is the final row
  void reset(std::vector<int> ids);
  void sourceChanged(int id);  // the source row of `id` changed: forward it

  const TrayIconModel* source_ = nullptr;
  std::vector<int> ids_;
};

// Pin state and order of the tray icons for one screen, one instance per Tray widget (not a
// singleton). Splits `icons` into `pinned` and `unpinned`, each in saved order, and keeps the
// per-screen state file (Python YASB's systray_state_<screen>.json format, in `stateDir`).
//
// A new icon is placed by its saved state (savedStateFor; an unknown one is unpinned at the end).
// Icons that leave `icons` leave the lists but keep their saved entry, so they return in place.
// Arrival and removal never save; every change made through the invokables does. Icons that share
// a key (same executable, no guid) are separate rows with one shared saved state: they are
// pinned, unpinned and moved together.
//
// Thread: GUI thread only. No thread of its own; the state file is read and written inline.
class TrayPins : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(wabar::tray::TrayIconModel* icons READ icons WRITE setIcons NOTIFY iconsChanged FINAL)
  Q_PROPERTY(QString screenId READ screenId WRITE setScreenId NOTIFY screenIdChanged FINAL)
  Q_PROPERTY(QScreen* screen READ screen WRITE setScreen NOTIFY screenChanged FINAL)
  Q_PROPERTY(QWindow* window READ window WRITE setWindow NOTIFY windowChanged FINAL)
  Q_PROPERTY(QString stateDir READ stateDir WRITE setStateDir NOTIFY stateDirChanged FINAL)
  Q_PROPERTY(wabar::tray::TrayPinList* pinned READ pinned CONSTANT FINAL)
  Q_PROPERTY(wabar::tray::TrayPinList* unpinned READ unpinned CONSTANT FINAL)

 public:
  explicit TrayPins(QObject* parent = nullptr);

  TrayIconModel* icons() const { return icons_; }
  // Set once; later calls are ignored.
  void setIcons(TrayIconModel* icons);
  QString screenId() const { return screenId_; }
  // Loads that screen's state file and rebuilds both lists. Empty means "None".
  void setScreenId(const QString& id);
  QScreen* screen() const { return screen_; }
  // Sets screenId to the screen's cleaned identity (cleanScreenId of manufacturer, name and
  // serial number; "None" for null) and reloads as setScreenId does.
  void setScreen(QScreen* screen);
  QWindow* window() const { return window_; }
  // QML cannot hand over a QScreen (a Window's `screen` there is a ScreenInfo), so a Tray gives
  // its window instead: `screen` follows window->screen(), including when the window moves.
  void setWindow(QWindow* window);
  QString stateDir() const { return stateDir_; }
  // Defaults to kernel::appDataDir(). Set it before screenId (tests); later it reloads.
  void setStateDir(const QString& dir);
  TrayPinList* pinned() { return &pinned_; }
  TrayPinList* unpinned() { return &unpinned_; }

  // Moves the icon to the end of the pinned or the unpinned list.
  Q_INVOKABLE void setPinned(int id, bool pinned);
  // Moves (or pins, or unpins) the icon to `toIndex` of that list, clamped.
  Q_INVOKABLE void move(int id, bool pinned, int toIndex);
  // Moves the icon within its list by `delta` places, clamped; nothing happens at the ends.
  Q_INVOKABLE void moveBy(int id, int delta);
  Q_INVOKABLE bool isPinned(int id) const;

 signals:
  void iconsChanged();
  void screenIdChanged();
  void screenChanged();
  void windowChanged();
  void stateDirChanged();

 private:
  void reload();
  void onRowsInserted(int first, int last);
  void onRowsRemoved();
  QString effectiveScreenId() const;
  int positionFor(const std::vector<int>& list, int id) const;
  int sortKey(int id) const;
  QString keyOf(int id) const;
  QStringList keysOf(const std::vector<int>& ids) const;
  void applyMove(int id, bool pinned, int toIndex);
  void persist();

  TrayIconModel* icons_ = nullptr;
  QString screenId_;
  QPointer<QScreen> screen_;
  QPointer<QWindow> window_;
  QString stateDir_;
  bool loaded_ = false;
  wabar::tray::IconStateMap currentState_;
  TrayPinList pinned_;
  TrayPinList unpinned_;
};

}  // namespace wabar::tray
