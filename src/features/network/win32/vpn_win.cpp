#include <windows.h>
// ras.h needs windows.h first.
#include <ras.h>
#include <raserror.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <atomic>
#include <string>
#include <vector>

#include "features/network/vpn.h"
#include "kernel/qt/live_desktop.h"

namespace wabar::network {
namespace {

constexpr int kHangUpWaitMs = 4000;

QString rasError(DWORD code) {
  wchar_t text[256] = {};
  if (RasGetErrorStringW(code, text, 256) == ERROR_SUCCESS && text[0]) {
    return QString::fromWCharArray(text).trimmed();
  }
  return QStringLiteral("RAS error %1").arg(code);
}

std::vector<RASCONNW> activeConnections() {
  DWORD bytes = 0;
  DWORD count = 0;
  const DWORD probe = RasEnumConnectionsW(nullptr, &bytes, &count);
  if (probe != ERROR_BUFFER_TOO_SMALL || bytes == 0) return {};
  std::vector<RASCONNW> list(bytes / sizeof(RASCONNW) + 1);
  list[0].dwSize = sizeof(RASCONNW);
  bytes = static_cast<DWORD>(list.size() * sizeof(RASCONNW));
  if (RasEnumConnectionsW(list.data(), &bytes, &count) != ERROR_SUCCESS) return {};
  list.resize(count);
  return list;
}

QVector<VpnEntry> readEntries() {
  QVector<VpnEntry> entries;
  DWORD bytes = 0;
  DWORD count = 0;
  const DWORD probe = RasEnumEntriesW(nullptr, nullptr, nullptr, &bytes, &count);
  if (probe != ERROR_BUFFER_TOO_SMALL || bytes == 0) return entries;
  std::vector<RASENTRYNAMEW> list(bytes / sizeof(RASENTRYNAMEW) + 1);
  list[0].dwSize = sizeof(RASENTRYNAMEW);
  bytes = static_cast<DWORD>(list.size() * sizeof(RASENTRYNAMEW));
  if (RasEnumEntriesW(nullptr, nullptr, list.data(), &bytes, &count) != ERROR_SUCCESS)
    return entries;
  const std::vector<RASCONNW> active = activeConnections();
  for (DWORD i = 0; i < count; ++i) {
    VpnEntry entry;
    entry.name = QString::fromWCharArray(list[i].szEntryName);
    for (const RASCONNW& connection : active) {
      if (entry.name == QString::fromWCharArray(connection.szEntryName)) entry.connected = true;
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}

// Blocks until Windows has dialled (or failed); pool threads only. Returns an empty string on
// success.
QString dial(const QString& name) {
  RASDIALPARAMSW params = {};
  params.dwSize = sizeof(params);
  name.left(RAS_MaxEntryName).toWCharArray(params.szEntryName);
  BOOL passwordSaved = FALSE;
  const DWORD got = RasGetEntryDialParamsW(nullptr, &params, &passwordSaved);
  if (got != ERROR_SUCCESS) return rasError(got);
  HRASCONN connection = nullptr;
  const DWORD result = RasDialW(nullptr, nullptr, &params, 0, nullptr, &connection);
  SecureZeroMemory(params.szPassword, sizeof(params.szPassword));
  if (result == ERROR_SUCCESS) return {};
  if (connection) RasHangUpW(connection);
  return rasError(result);
}

QString hangUp(const QString& name) {
  for (const RASCONNW& connection : activeConnections()) {
    if (name != QString::fromWCharArray(connection.szEntryName)) continue;
    const DWORD result = RasHangUpW(connection.hrasconn);
    if (result != ERROR_SUCCESS) return rasError(result);
    // RasHangUp returns before the connection is gone; wait until its handle is invalid.
    RASCONNSTATUSW status = {};
    status.dwSize = sizeof(status);
    for (int waited = 0; waited < kHangUpWaitMs; waited += 100) {
      if (RasGetConnectStatusW(connection.hrasconn, &status) == ERROR_INVALID_HANDLE) break;
      Sleep(100);
    }
    return {};
  }
  return QStringLiteral("The VPN is not connected");
}

// RAS, every call on a QThreadPool thread (RasDial blocks until the dial ends). Results return as
// queued signals through qApp and are dropped when this object is gone. Nothing to join: a task
// ends with its call.
class SystemVpnBackend final : public VpnBackend {
 public:
  void refresh() override {
    if (refreshing_.exchange(true)) return;
    QPointer<SystemVpnBackend> guard(this);
    QThreadPool::globalInstance()->start([guard] {
      QVector<VpnEntry> entries;
      try {
        entries = readEntries();
      } catch (...) {
      }
      post(guard, [guard, entries = std::move(entries)] {
        guard->refreshing_ = false;
        emit guard->entriesRead(entries);
      });
    });
  }

  void connectEntry(const QString& name) override { run(name, true); }
  void disconnectEntry(const QString& name) override { run(name, false); }

 private:
  void run(const QString& name, bool dialUp) {
    QPointer<SystemVpnBackend> guard(this);
    QThreadPool::globalInstance()->start([guard, name, dialUp] {
      QString error;
      try {
        error = dialUp ? dial(name) : hangUp(name);
      } catch (...) {
        error = QStringLiteral("The VPN request failed");
      }
      post(guard,
           [guard, name, error] { emit guard->operationFinished(name, error.isEmpty(), error); });
    });
  }

  template <class Fn>
  static void post(const QPointer<SystemVpnBackend>& guard, Fn fn) {
    if (!qApp) return;
    QMetaObject::invokeMethod(
        qApp,
        [guard, fn = std::move(fn)]() mutable {
          if (guard) fn();
        },
        Qt::QueuedConnection);
  }

  std::atomic<bool> refreshing_{false};
};

}  // namespace

std::unique_ptr<VpnBackend> createSystemVpnBackend() {
  if (!kernel::liveDesktop()) return nullptr;
  return std::make_unique<SystemVpnBackend>();
}

}  // namespace wabar::network
