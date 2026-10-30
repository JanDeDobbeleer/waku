#include <windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <QDebug>
#include <QString>
#include <algorithm>
#include <mutex>
#include <vector>

#include "features/system_stats/gpu_engines.h"
#include "features/system_stats/system_stats.h"

namespace wabar::system_stats {
namespace {

// Documented APIs only: PDH ("GPU Engine" and "GPU Adapter Memory" counters, Windows 10 1709 and
// later) and DXGI for the adapter names and dedicated memory sizes.

constexpr int kRenewEvery = 20;  // samples between two rebuilds of the engine query

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
  }

  GpuSample sample() override {
    std::lock_guard lock(mutex_);
    readAdapters();
    GpuSample result;
    result.name = name_;
    result.memoryTotal = memoryTotal_;
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

 private:
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
      }
      adapter->Release();
    }
    factory->Release();
  }

  std::mutex mutex_;
  Query active_;
  Query pending_;
  int samples_ = 0;
  bool failed_ = false;
  bool adaptersRead_ = false;
  std::string name_;
  std::uint64_t memoryTotal_ = 0;
};

}  // namespace

std::shared_ptr<GpuDataSource> createGpuDataSource() {
  return std::make_shared<PdhGpuDataSource>();
}

}  // namespace wabar::system_stats
