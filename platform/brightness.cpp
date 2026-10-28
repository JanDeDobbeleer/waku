// Monitor brightness: DDC/CI through dxva2 for external panels, the active power scheme for the
// internal panel. Port of services/brightness (service.py, displays.py, scheme.py), without the
// contrast feature, the periodic DDC poll and the power-policy notification window.
#include "platform/brightness.h"

#include <windows.h>
#include <highlevelmonitorconfigurationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <powrprof.h>
#include <winrt/Windows.Devices.Display.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>

#include <QDebug>
#include <QMetaObject>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <thread>

#include "platform/winrt_apartment.h"

namespace yasb::platform {
namespace {

constexpr int kDdcAttempts = 5;
constexpr auto kDdcRetrySleep = std::chrono::milliseconds(100);
constexpr BYTE kVcpBrightness = 0x10;

// {7516b95f-f776-4464-8c53-06167f40cc99}
const GUID kVideoSubgroup = {
    0x7516b95f, 0xf776, 0x4464, {0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99}};
// {aded5e82-b909-4619-9949-f5d71dac0bcb}
const GUID kVideoBrightness = {
    0xaded5e82, 0xb909, 0x4619, {0x99, 0x49, 0xf5, 0xd7, 0x1d, 0xac, 0x0b, 0xcb}};

// --- power scheme brightness (scheme.py) --------------------------------------------------------

std::optional<bool> acOnline() {
  SYSTEM_POWER_STATUS status{};
  if (!GetSystemPowerStatus(&status)) return std::nullopt;
  if (status.ACLineStatus == 1) return true;
  if (status.ACLineStatus == 0) return false;
  return std::nullopt;
}

std::optional<int> schemeBrightness() {
  const auto online = acOnline();
  if (!online) return std::nullopt;
  GUID* scheme = nullptr;
  if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS || !scheme) return std::nullopt;
  DWORD value = 0;
  const DWORD status =
      *online ? PowerReadACValueIndex(nullptr, scheme, &kVideoSubgroup, &kVideoBrightness, &value)
              : PowerReadDCValueIndex(nullptr, scheme, &kVideoSubgroup, &kVideoBrightness, &value);
  LocalFree(scheme);
  if (status != ERROR_SUCCESS) return std::nullopt;
  return std::clamp(static_cast<int>(value), 0, 100);
}

bool setSchemeBrightness(int percent) {
  percent = std::clamp(percent, 0, 100);
  GUID* scheme = nullptr;
  if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS || !scheme) return false;
  bool ok = PowerWriteACValueIndex(nullptr, scheme, &kVideoSubgroup, &kVideoBrightness, percent) ==
                ERROR_SUCCESS &&
            PowerWriteDCValueIndex(nullptr, scheme, &kVideoSubgroup, &kVideoBrightness, percent) ==
                ERROR_SUCCESS &&
            PowerSetActiveScheme(nullptr, scheme) == ERROR_SUCCESS;
  LocalFree(scheme);
  return ok;
}

// --- DDC/CI (service.py _vcp_read / _vcp_write) -------------------------------------------------

struct PhysicalMonitors {
  std::vector<PHYSICAL_MONITOR> list;
  ~PhysicalMonitors() {
    if (!list.empty()) DestroyPhysicalMonitors(static_cast<DWORD>(list.size()), list.data());
  }
};

bool openPhysical(HMONITOR monitor, PhysicalMonitors& out) {
  DWORD count = 0;
  if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count) || count == 0) return false;
  out.list.assign(count, PHYSICAL_MONITOR{});
  if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, out.list.data())) {
    out.list.clear();
    return false;
  }
  return true;
}

std::optional<int> vcpRead(HMONITOR monitor, BYTE code) {
  PhysicalMonitors physical;
  if (!openPhysical(monitor, physical)) return std::nullopt;
  for (const PHYSICAL_MONITOR& entry : physical.list) {
    DWORD current = 0;
    DWORD maximum = 0;
    for (int attempt = 0; attempt < kDdcAttempts; ++attempt) {
      if (GetVCPFeatureAndVCPFeatureReply(entry.hPhysicalMonitor, code, nullptr, &current,
                                          &maximum)) {
        return static_cast<int>(
            std::lround(static_cast<double>(current) / std::max<DWORD>(maximum, 1) * 100.0));
      }
      if (attempt + 1 < kDdcAttempts) std::this_thread::sleep_for(kDdcRetrySleep);
    }
  }
  return std::nullopt;
}

bool vcpWrite(HMONITOR monitor, BYTE code, int percent) {
  PhysicalMonitors physical;
  if (!openPhysical(monitor, physical)) return false;
  bool any = false;
  for (const PHYSICAL_MONITOR& entry : physical.list) {
    DWORD current = 0;
    DWORD maximum = 0;
    DWORD maxValue = 100;
    for (int attempt = 0; attempt < kDdcAttempts; ++attempt) {
      if (GetVCPFeatureAndVCPFeatureReply(entry.hPhysicalMonitor, code, nullptr, &current,
                                          &maximum)) {
        maxValue = maximum ? maximum : 100;
        break;
      }
      if (attempt + 1 < kDdcAttempts) std::this_thread::sleep_for(kDdcRetrySleep);
    }
    const long native =
        std::clamp<long>(std::lround(percent * static_cast<double>(maxValue) / 100.0), 0,
                         static_cast<long>(maxValue));
    for (int attempt = 0; attempt < kDdcAttempts; ++attempt) {
      if (SetVCPFeature(entry.hPhysicalMonitor, code, static_cast<DWORD>(native))) {
        any = true;
        break;
      }
      if (attempt + 1 < kDdcAttempts) std::this_thread::sleep_for(kDdcRetrySleep);
    }
  }
  return any;
}

// --- display metadata (displays.py) -------------------------------------------------------------

struct DisplayTarget {
  QString monitorName;
  QString connectionType;
  bool internal = false;
};

QString connectorName(winrt::Windows::Devices::Display::DisplayMonitorPhysicalConnectorKind kind) {
  using Kind = winrt::Windows::Devices::Display::DisplayMonitorPhysicalConnectorKind;
  switch (kind) {
    case Kind::Unknown:
      return QStringLiteral("Unknown");
    case Kind::HD15:
      return QStringLiteral("Hd15");
    case Kind::AnalogTV:
      return QStringLiteral("Analog T V");
    case Kind::Dvi:
      return QStringLiteral("Dvi");
    case Kind::Hdmi:
      return QStringLiteral("Hdmi");
    case Kind::Lvds:
      return QStringLiteral("Lvds");
    case Kind::Sdi:
      return QStringLiteral("Sdi");
    case Kind::DisplayPort:
      return QStringLiteral("Display Port");
  }
  return {};
}

DisplayTarget queryMonitor(const std::wstring& path) {
  try {
    using winrt::Windows::Devices::Display::DisplayMonitor;
    using winrt::Windows::Devices::Display::DisplayMonitorConnectionKind;
    const DisplayMonitor monitor = DisplayMonitor::FromInterfaceIdAsync(winrt::hstring(path)).get();
    DisplayTarget target;
    target.monitorName = QString::fromWCharArray(monitor.DisplayName().c_str()).trimmed();
    target.connectionType = connectorName(monitor.PhysicalConnector());
    target.internal = monitor.ConnectionKind() == DisplayMonitorConnectionKind::Internal;
    return target;
  } catch (...) {
    return {};
  }
}

// Prefers an active INTERNAL panel, else the first active path (displays.py _query_device).
DisplayTarget queryDisplay(const std::wstring& device) {
  if (device.empty()) return {};
  std::optional<DisplayTarget> first;
  std::optional<DisplayTarget> firstActive;
  for (DWORD index = 0; index < 16; ++index) {
    DISPLAY_DEVICEW info{};
    info.cb = sizeof(info);
    if (!EnumDisplayDevicesW(device.c_str(), index, &info, EDD_GET_DEVICE_INTERFACE_NAME)) break;
    if (!info.DeviceID[0]) continue;
    const bool active = (info.StateFlags & DISPLAY_DEVICE_ACTIVE) != 0;
    const DisplayTarget target = queryMonitor(info.DeviceID);
    if (!first) first = target;
    if (!active) continue;
    if (!firstActive) firstActive = target;
    if (target.internal) return target;
  }
  if (firstActive) return *firstActive;
  if (first) return *first;
  return {};
}

QString physicalName(HMONITOR monitor, int index) {
  const QString fallback = QStringLiteral("Monitor %1").arg(index + 1);
  PhysicalMonitors physical;
  if (!openPhysical(monitor, physical)) return fallback;
  const QString clean =
      QString::fromWCharArray(physical.list[0].szPhysicalMonitorDescription).trimmed();
  bool numeric = !clean.isEmpty();
  for (const QChar c : clean) numeric = numeric && c.isDigit();
  return !clean.isEmpty() && !numeric ? clean : fallback;
}

// --- service ------------------------------------------------------------------------------------

struct MonitorInfo {
  qulonglong id = 0;
  std::wstring device;
  QString name;
  QString connection;
  bool internal = false;
  bool ddc = false;
  bool scheme = false;
  bool tested = false;
  bool reported = false;
  std::optional<int> brightness;
};

class SystemBrightness final : public BrightnessControl {
 public:
  explicit SystemBrightness(QObject* parent) : BrightnessControl(parent) {
    enumerate();
    worker_ = std::thread([this] { run(); });
    enqueue([this] { detect(); });
  }

  ~SystemBrightness() override { stop(); }

  void stop() override {
    {
      std::lock_guard lock(mutex_);
      if (stopped_) return;
      stopped_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  std::vector<BrightnessMonitor> monitors() override {
    std::lock_guard lock(mutex_);
    std::vector<BrightnessMonitor> result;
    for (const MonitorInfo& info : monitors_) {
      if (info.ddc || info.scheme) result.push_back({info.id, info.name});
    }
    return result;
  }

  QString monitorSubtitle(qulonglong id, int index) override {
    QString connection;
    {
      std::lock_guard lock(mutex_);
      if (const MonitorInfo* info = find(id)) connection = info->connection;
    }
    return connection.isEmpty() ? QStringLiteral("Monitor %1").arg(index + 1)
                                : QStringLiteral("Monitor %1 · %2").arg(index + 1).arg(connection);
  }

  std::optional<int> brightness(qulonglong id) override {
    std::lock_guard lock(mutex_);
    const MonitorInfo* info = find(id);
    return info ? info->brightness : std::nullopt;
  }

  void setBrightness(qulonglong id, int percent) override {
    percent = std::clamp(percent, 0, 100);
    bool emitChange = false;
    {
      std::lock_guard lock(mutex_);
      wanted_[id] = percent;
      if (MonitorInfo* info = find(id)) {
        emitChange = info->brightness != percent || !info->reported;
        info->brightness = percent;
        info->reported = true;
      }
    }
    if (emitChange) emit brightnessChanged(id, percent);
    enqueue([this] { drainWrites(); }, &drainQueued_);
  }

  void refreshNow() override {
    // The scheme panel is a cheap powrprof read; DDC monitors are re-read on the worker.
    std::optional<qulonglong> owner;
    {
      std::lock_guard lock(mutex_);
      owner = schemeOwner_;
    }
    if (owner) {
      if (const auto value = schemeBrightness()) update(*owner, *value);
    }
    bool anyDdc = false;
    {
      std::lock_guard lock(mutex_);
      for (const MonitorInfo& info : monitors_) anyDdc = anyDdc || (info.tested && info.ddc);
    }
    if (anyDdc) enqueue([this] { refreshDdc(); }, &refreshQueued_);
  }

 private:
  MonitorInfo* find(qulonglong id) {
    for (MonitorInfo& info : monitors_) {
      if (info.id == id) return &info;
    }
    return nullptr;
  }

  void enumerate() {
    struct Context {
      std::vector<MonitorInfo>* out;
    } context{&monitors_};
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM param) -> BOOL {
          auto* ctx = reinterpret_cast<Context*>(param);
          MonitorInfo info;
          info.id = reinterpret_cast<qulonglong>(monitor);
          MONITORINFOEXW details{};
          details.cbSize = sizeof(details);
          if (GetMonitorInfoW(monitor, &details)) info.device = details.szDevice;
          info.name = QStringLiteral("Monitor %1").arg(ctx->out->size() + 1);
          ctx->out->push_back(std::move(info));
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
  }

  void run() {
    ensureWinrtApartment();
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return stopped_ || !tasks_.empty(); });
        if (stopped_) return;
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }
      try {
        task();
      } catch (...) {
        qWarning() << "Brightness task failed";
      }
    }
  }

  // `flag` (guarded by mutex_) collapses repeated requests while one is still queued.
  void enqueue(std::function<void()> task, bool* flag = nullptr) {
    {
      std::lock_guard lock(mutex_);
      if (stopped_) return;
      if (flag) {
        if (*flag) return;
        *flag = true;
      }
      tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
  }

  // Updates the cached value and emits on the GUI thread when it changed.
  void update(qulonglong id, int value) {
    bool changed = false;
    {
      std::lock_guard lock(mutex_);
      MonitorInfo* info = find(id);
      if (!info || wanted_.count(id)) return;
      changed = info->brightness != value || !info->reported;
      info->brightness = value;
      info->reported = true;
    }
    if (changed) post([this, id, value] { emit brightnessChanged(id, value); });
  }

  void post(std::function<void()> fn) {
    QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
  }

  void detect() {
    std::vector<std::pair<qulonglong, std::wstring>> targets;
    {
      std::lock_guard lock(mutex_);
      for (const MonitorInfo& info : monitors_) targets.emplace_back(info.id, info.device);
    }
    int index = 0;
    for (const auto& [id, device] : targets) {
      const DisplayTarget target = queryDisplay(device);
      QString name = target.monitorName;
      if (name.isEmpty()) {
        name = target.internal ? QStringLiteral("Built-in display")
                               : physicalName(reinterpret_cast<HMONITOR>(id), index);
      }
      if (target.internal && name.startsWith(QStringLiteral("Monitor ")))
        name = QStringLiteral("Built-in display");
      std::lock_guard lock(mutex_);
      if (MonitorInfo* info = find(id)) {
        info->name = name;
        info->connection = target.connectionType;
        info->internal = target.internal;
      }
      ++index;
    }
    if (targets.empty()) return;

    // Parallel DDC probe, as the Python thread pool does.
    std::vector<std::pair<qulonglong, std::future<bool>>> probes;
    for (const auto& target : targets) {
      const qulonglong id = target.first;
      probes.emplace_back(
          id, std::async(std::launch::async, [id] {
            return vcpRead(reinterpret_cast<HMONITOR>(id), kVcpBrightness).has_value();
          }));
    }
    std::vector<qulonglong> pendingScheme;
    for (auto& [id, probe] : probes) {
      bool supportsDdc = false;
      try {
        supportsDdc = probe.get();
      } catch (...) {
      }
      if (!supportsDdc) {
        pendingScheme.push_back(id);
        continue;
      }
      {
        std::lock_guard lock(mutex_);
        MonitorInfo* info = find(id);
        if (!info || info->tested) continue;
        // One HMONITOR can expose DDC on the external while WinRT still marks it internal; the
        // scheme wins then.
        if (info->internal) {
          pendingScheme.push_back(id);
          continue;
        }
        info->ddc = true;
        info->tested = true;
      }
      if (const auto value = vcpRead(reinterpret_cast<HMONITOR>(id), kVcpBrightness)) {
        {
          std::lock_guard lock(mutex_);
          if (MonitorInfo* info = find(id)) {
            info->brightness = *value;
            info->reported = true;
          }
        }
        post([this, id = id, v = *value] { emit brightnessChanged(id, v); });
      }
    }
    for (const qulonglong id : pendingScheme) {
      bool canScheme = false;
      {
        std::lock_guard lock(mutex_);
        MonitorInfo* info = find(id);
        if (!info || info->tested) continue;
        if (info->internal) {
          if (schemeOwner_) {
            canScheme = *schemeOwner_ == id;
          } else if (schemeBrightness()) {
            schemeOwner_ = id;
            canScheme = true;
          }
        }
        info->scheme = canScheme;
        info->tested = true;
      }
      if (!canScheme) continue;
      if (const auto value = schemeBrightness()) {
        {
          std::lock_guard lock(mutex_);
          if (MonitorInfo* info = find(id)) {
            info->brightness = *value;
            info->reported = true;
          }
        }
        post([this, id, v = *value] { emit brightnessChanged(id, v); });
      }
    }
  }

  void refreshDdc() {
    std::vector<qulonglong> targets;
    {
      std::lock_guard lock(mutex_);
      refreshQueued_ = false;
      for (const MonitorInfo& info : monitors_) {
        if (info.tested && info.ddc) targets.push_back(info.id);
      }
    }
    std::vector<std::future<void>> jobs;
    for (const qulonglong id : targets) {
      jobs.push_back(std::async(std::launch::async, [this, id] {
        if (const auto value = vcpRead(reinterpret_cast<HMONITOR>(id), kVcpBrightness))
          update(id, *value);
      }));
    }
    for (auto& job : jobs) job.wait();
  }

  // Applies the newest slider value per monitor (keeps only the latest while writes are slow).
  void drainWrites() {
    for (;;) {
      std::map<qulonglong, int> batch;
      {
        std::lock_guard lock(mutex_);
        batch.swap(wanted_);
        if (batch.empty()) {
          drainQueued_ = false;
          return;
        }
      }
      for (const auto& [id, value] : batch) apply(id, value);
    }
  }

  void apply(qulonglong id, int value) {
    bool ddc = false;
    bool scheme = false;
    {
      std::lock_guard lock(mutex_);
      const MonitorInfo* info = find(id);
      if (!info || !info->tested) return;
      ddc = info->ddc;
      scheme = info->scheme;
    }
    if (ddc && vcpWrite(reinterpret_cast<HMONITOR>(id), kVcpBrightness, value)) return;
    if (scheme) setSchemeBrightness(value);
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> tasks_;
  std::thread worker_;
  bool stopped_ = false;
  bool drainQueued_ = false;
  bool refreshQueued_ = false;
  std::vector<MonitorInfo> monitors_;
  std::optional<qulonglong> schemeOwner_;
  std::map<qulonglong, int> wanted_;
};

}  // namespace

qulonglong monitorForWindow(quintptr hwnd) {
  if (!hwnd) return 0;
  return reinterpret_cast<qulonglong>(
      MonitorFromWindow(reinterpret_cast<HWND>(hwnd), MONITOR_DEFAULTTONEAREST));
}

BrightnessControl* createSystemBrightness(QObject* parent) { return new SystemBrightness(parent); }

}  // namespace yasb::platform
