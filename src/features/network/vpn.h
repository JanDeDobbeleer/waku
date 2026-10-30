#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>
#include <memory>

namespace wabar::network {

// One configured Windows VPN (RAS phone book) entry.
struct VpnEntry {
  QString name;
  bool connected = false;

  bool operator==(const VpnEntry&) const = default;
};

// The configured VPN entries and their control, asynchronous. Every method returns at once; the
// work runs off the GUI thread and the results arrive as queued signals. Tests substitute a fake;
// the real backend (createSystemVpnBackend, RAS) must never be driven from tests.
class VpnBackend : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;

  // Lists the entries; `entriesRead` follows.
  virtual void refresh() = 0;
  // Dials / hangs up one entry; `operationFinished` follows.
  virtual void connectEntry(const QString& name) = 0;
  virtual void disconnectEntry(const QString& name) = 0;

 signals:
  void entriesRead(const QVector<wabar::network::VpnEntry>& entries);
  // `error` is empty on success.
  void operationFinished(const QString& name, bool ok, const QString& error);
};

// The real backend; null off the live desktop.
std::unique_ptr<VpnBackend> createSystemVpnBackend();

}  // namespace wabar::network

Q_DECLARE_METATYPE(wabar::network::VpnEntry)
Q_DECLARE_METATYPE(QVector<wabar::network::VpnEntry>)
