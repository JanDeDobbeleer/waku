#pragma once

#include <QObject>
#include <memory>

namespace waku::network {

// Tells the Network service that the network state may have changed (a connection came or went,
// the Wi-Fi signal moved), so it samples then instead of only on a timer. `changed` may fire in
// bursts and spuriously; the service debounces it and reads the state itself. Tests substitute a
// fake; the real watcher (createSystemChangeWatcher) registers with Windows and must never be
// driven from tests.
class NetworkChangeWatcher : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;

 signals:
  // On the GUI thread.
  void changed();
};

// The real watcher; null off the live desktop or when no registration worked.
std::unique_ptr<NetworkChangeWatcher> createSystemChangeWatcher();

}  // namespace waku::network
