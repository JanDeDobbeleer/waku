#pragma once

#include <QtQml/qqmlregistration.h>

#include <QMetaMethod>
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>

#include "features/desktop_state/desktop_probe.h"

namespace waku::desktop_state {

// Lock keys, keyboard layout and idle time as a QML singleton.
//
// Windows has no change notification for these that is both documented and cheap, so the
// singleton polls every 200 ms, and only while something observes it: a binding or a connection
// on one of the change signals starts the timer, the last one going away stops it. A read from
// script without a binding refreshes the value on the spot, so it is never stale. GUI thread
// only; no worker thread.
//
// `idleSeconds` counts whole seconds since the last keyboard or mouse input and changes at most
// once per second. `keyboardLayout` is the layout of the foreground window's thread ("EN"),
// `keyboardLayoutName` its localized name ("English (United States)"), `keyboardLayoutId` the
// layout handle in hex. Without a live desktop (the offscreen platform) nothing is read and the
// values stay false, empty and 0.
class DesktopState : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available CONSTANT FINAL)
  Q_PROPERTY(bool capsLock READ capsLock NOTIFY capsLockChanged FINAL)
  Q_PROPERTY(bool numLock READ numLock NOTIFY numLockChanged FINAL)
  Q_PROPERTY(bool scrollLock READ scrollLock NOTIFY scrollLockChanged FINAL)
  Q_PROPERTY(QString keyboardLayout READ keyboardLayout NOTIFY keyboardLayoutChanged FINAL)
  Q_PROPERTY(QString keyboardLayoutName READ keyboardLayoutName NOTIFY keyboardLayoutChanged FINAL)
  Q_PROPERTY(QString keyboardLayoutId READ keyboardLayoutId NOTIFY keyboardLayoutChanged FINAL)
  Q_PROPERTY(int idleSeconds READ idleSeconds NOTIFY idleSecondsChanged FINAL)

 public:
  static constexpr int kPollIntervalMs = 200;

  explicit DesktopState(QObject* parent = nullptr);
  // Test seam: a fake probe; null means no probe.
  DesktopState(std::unique_ptr<DesktopProbe> probe, QObject* parent);
  ~DesktopState() override;

  bool available() const { return probe_ != nullptr; }
  bool capsLock();
  bool numLock();
  bool scrollLock();
  QString keyboardLayout();
  QString keyboardLayoutName();
  QString keyboardLayoutId();
  int idleSeconds();

  // True while the poll timer runs.
  bool polling() const { return timer_.isActive(); }
  // Reads the machine now and emits the change signals for what differs.
  Q_INVOKABLE void refresh();

 signals:
  void capsLockChanged();
  void numLockChanged();
  void scrollLockChanged();
  void keyboardLayoutChanged();
  void idleSecondsChanged();

 protected:
  void connectNotify(const QMetaMethod& signal) override;
  void disconnectNotify(const QMetaMethod& signal) override;

 private:
  bool observed() const;
  void updatePolling();
  // Without a running timer a read from script refreshes first, without emitting.
  void freshen();
  void poll(bool emitSignals);

  std::unique_ptr<DesktopProbe> probe_;
  QTimer timer_;
  LockKeys keys_;
  KeyboardLayoutInfo layout_;
  int idleSeconds_ = 0;
};

}  // namespace waku::desktop_state
