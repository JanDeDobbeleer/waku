#pragma once

#include <QObject>
#include <QString>
#include <memory>

namespace wabar::network {

// The Wi-Fi radio switch, asynchronous. Every method returns at once; the work runs off the GUI
// thread and the results arrive as queued signals. Tests substitute a fake; the real backend
// (createSystemWifiRadio, Windows.Devices.Radios) must never be driven from tests.
class WifiRadioBackend : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;

  // Reads the radio state; `stateRead` follows.
  virtual void query() = 0;
  // Asks Windows to switch the radio. `switchFinished` follows, then `stateRead` with the state
  // Windows reports afterwards.
  virtual void setEnabled(bool enabled) = 0;

 signals:
  // `known` is false when there is no Wi-Fi radio or it could not be read.
  void stateRead(bool known, bool on);
  // `error` is empty on success.
  void switchFinished(bool ok, const QString& error);
};

// The real backend; null off the live desktop.
std::unique_ptr<WifiRadioBackend> createSystemWifiRadio();

}  // namespace wabar::network
