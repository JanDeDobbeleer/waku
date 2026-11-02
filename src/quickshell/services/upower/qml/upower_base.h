#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>

namespace waku::quickshell {

// The base of the `UPower` singleton (UPower.qml). It exists only for `onBattery`: QML cannot
// declare a property whose name starts with "on" followed by a capital (it reads as a signal
// handler), and Quickshell's UPower has one. UPower.qml sets the value with setOnBattery().
class UPowerBase : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool onBattery READ onBattery NOTIFY onBatteryChanged FINAL)

 public:
  explicit UPowerBase(QObject* parent = nullptr) : QObject(parent) {}

  bool onBattery() const { return onBattery_; }
  Q_INVOKABLE void setOnBattery(bool value) {
    if (value == onBattery_) return;
    onBattery_ = value;
    emit onBatteryChanged();
  }

 signals:
  void onBatteryChanged();

 private:
  bool onBattery_ = false;
};

}  // namespace waku::quickshell
