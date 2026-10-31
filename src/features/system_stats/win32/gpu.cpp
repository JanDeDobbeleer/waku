// clang-format off
// d3dkmthk.h needs NTSTATUS and the STATUS_* codes: take them from ntstatus.h, not winnt.h.
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
// clang-format on

#include <QDebug>
#include <QString>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <vector>

#include "features/system_stats/gpu_engines.h"
#include "features/system_stats/system_stats.h"
#include "features/system_stats/temperature.h"
#include "kernel/qt/linger.h"
#include "kernel/qt/live_desktop.h"

namespace wabar::system_stats {
namespace {

// Documented APIs only: PDH ("GPU Engine" and "GPU Adapter Memory" counters, Windows 10 1709 and
// later), DXGI for the adapter names and dedicated memory sizes, and the D3DKMT thunks in gdi32
// (d3dkmthk.h, Windows SDK) for the adapter temperature.

constexpr int kRenewEvery = 20;  // samples between two rebuilds of the engine query
// Below this, a second sample returns the previous one (the widgets sample every 2 s or slower).
constexpr std::chrono::milliseconds kMinInterval(1000);

struct Query {
  PDH_HQUERY query = nullptr;
  PDH_HCOUNTER engine = nullptr;
  PDH_HCOUNTER memory = nullptr;

  void close() {
    if (query) PdhCloseQuery(query);
    *this = {};
  }
};

// Opens a query on the GPU counters and takes the baseline sample a rate counter needs. False when
// the engine counter does not exist.
bool open(Query& out) {
  Query q;
  if (PdhOpenQueryW(nullptr, 0, &q.query) != ERROR_SUCCESS) return false;
  if (PdhAddEnglishCounterW(q.query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &q.engine) !=
      ERROR_SUCCESS) {
    q.close();
    return false;
  }
  PdhAddEnglishCounterW(q.query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &q.memory);
  PdhCollectQueryData(q.query);
  out = q;
  return true;
}

// The formatted values of a wildcard counter, or nothing when it has no instances.
template <typename Fn>
void forEachInstance(PDH_HCOUNTER counter, DWORD format, Fn&& fn) {
  if (!counter) return;
  DWORD bytes = 0;
  DWORD count = 0;
  if (PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, nullptr) != PDH_MORE_DATA) {
    return;
  }
  std::vector<std::byte> buffer(bytes);
  auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
  if (PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, items) != ERROR_SUCCESS) return;
  for (DWORD i = 0; i < count; ++i) {
    const DWORD status = items[i].FmtValue.CStatus;
    if (status != PDH_CSTATUS_VALID_DATA && status != PDH_CSTATUS_NEW_DATA) continue;
    fn(items[i]);
  }
}

class PdhGpuDataSource final : public GpuDataSource {
 public:
  ~PdhGpuDataSource() override {
    active_.close();
    pending_.close();
    for (const D3DKMT_HANDLE adapter : adapters_) {
      D3DKMT_CLOSEADAPTER close{};
      close.hAdapter = adapter;
      D3DKMTCloseAdapter(&close);
    }
  }

  GpuSample sample() override {
    std::lock_guard lock(mutex_);
    // Every widget shares this source: two collects close together would rate a few milliseconds.
    const auto now = std::chrono::steady_clock::now();
    if (last_ && now - lastAt_ < kMinInterval) return *last_;
    last_ = collect();
    lastAt_ = now;
    return *last_;
  }

 private:
  GpuSample collect() {
    readAdapters();
    GpuSample result;
    result.name = name_;
    result.memoryTotal = memoryTotal_;
    result.temperature = readTemperature();
    if (failed_) return result;
    if (!active_.query && !open(active_)) {
      failed_ = true;
      qInfo() << "GPU counters unavailable; the GPU values stay 0";
      return result;
    }
    // New processes only show up in a new query: build the next one a sample ahead so the swap
    // keeps a valid baseline.
    if (pending_.query) {
      active_.close();
      active_ = pending_;
      pending_ = {};
    } else if (++samples_ % kRenewEvery == 0) {
      open(pending_);
    }
    if (PdhCollectQueryData(active_.query) != ERROR_SUCCESS) return result;
    result.available = true;
    std::vector<GpuEngineValue> engines;
    forEachInstance(active_.engine, PDH_FMT_DOUBLE, [&](const PDH_FMT_COUNTERVALUE_ITEM_W& item) {
      engines.push_back(
          {QString::fromWCharArray(item.szName).toStdString(), item.FmtValue.doubleValue});
    });
    result.percent = gpuUsage(engines);
    std::uint64_t used = 0;
    forEachInstance(active_.memory, PDH_FMT_LARGE, [&](const PDH_FMT_COUNTERVALUE_ITEM_W& item) {
      used += static_cast<std::uint64_t>(std::max<LONGLONG>(0, item.FmtValue.largeValue));
    });
    result.memoryUsed = used;
    return result;
  }

  // The hardware adapters' names and dedicated memory, read once.
  void readAdapters() {
    if (adaptersRead_) return;
    adaptersRead_ = true;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
      return;
    }
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
      DXGI_ADAPTER_DESC1 desc{};
      if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
        if (name_.empty()) name_ = QString::fromWCharArray(desc.Description).toStdString();
        memoryTotal_ += desc.DedicatedVideoMemory;
        luids_.push_back(desc.AdapterLuid);
      }
      adapter->Release();
    }
    factory->Release();
  }

  // The hottest temperature the drivers of the hardware adapters report (deci-Celsius in
  // D3DKMT_ADAPTER_PERFDATA), or nothing. A driver that reports 0 (the Qualcomm Adreno driver does)
  // or fails the query gives no value. Never throws. The adapters are opened on the first read and
  // kept until the source goes.
  std::optional<double> readTemperature() {
    if (!kernel::liveDesktop(QStringLiteral("system_stats"))) return std::nullopt;
    if (!adaptersOpened_) {
      adaptersOpened_ = true;
      for (const LUID& luid : luids_) {
        D3DKMT_OPENADAPTERFROMLUID open{};
        open.AdapterLuid = luid;
        if (D3DKMTOpenAdapterFromLuid(&open) == STATUS_SUCCESS) adapters_.push_back(open.hAdapter);
      }
    }
    std::optional<double> best;
    for (const D3DKMT_HANDLE adapter : adapters_) {
      D3DKMT_ADAPTER_PERFDATA perf{};
      D3DKMT_QUERYADAPTERINFO query{};
      query.hAdapter = adapter;
      query.Type = KMTQAITYPE_ADAPTERPERFDATA;
      query.pPrivateDriverData = &perf;
      query.PrivateDriverDataSize = sizeof(perf);
      if (D3DKMTQueryAdapterInfo(&query) == STATUS_SUCCESS) {
        const std::optional<double> celsius = celsiusFromDeciCelsius(perf.Temperature);
        if (celsius && (!best || *celsius > *best)) best = celsius;
      }
    }
    return best;
  }

  std::mutex mutex_;
  Query active_;
  Query pending_;
  int samples_ = 0;
  bool failed_ = false;
  bool adaptersRead_ = false;
  std::string name_;
  std::uint64_t memoryTotal_ = 0;
  std::vector<LUID> luids_;
  bool adaptersOpened_ = false;
  std::vector<D3DKMT_HANDLE> adapters_;
  std::optional<GpuSample> last_;
  std::chrono::steady_clock::time_point lastAt_;
};

}  // namespace

// One source for every widget of every engine, kept through a reload (kernel::LingeringHub).
std::shared_ptr<GpuDataSource> createGpuDataSource() {
  static kernel::LingeringHub<GpuDataSource> hub;
  return hub.acquire([] { return std::make_shared<PdhGpuDataSource>(); });
}

}  // namespace wabar::system_stats
