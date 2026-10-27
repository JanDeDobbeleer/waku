#include "platform/memory.h"

#include <windows.h>
#include <pdh.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace yasb::platform {
namespace {

struct MemoryListInformation {
  ULONG_PTR zeroPageCount;
  ULONG_PTR freePageCount;
  ULONG_PTR modifiedPageCount;
  ULONG_PTR modifiedNoWritePageCount;
  ULONG_PTR badPageCount;
  ULONG_PTR pageCountByPriority[8];
  ULONG_PTR repurposedPageCountByPriority[8];
  ULONG_PTR modifiedPageCountPageFile;
};

using NtQuerySystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

std::uint64_t cachedBytesFallback(std::uint64_t pageSize) {
  PERFORMANCE_INFORMATION perf{};
  perf.cb = sizeof(perf);
  return GetPerformanceInfo(&perf, sizeof(perf))
             ? static_cast<std::uint64_t>(perf.SystemCache) * pageSize
             : 0;
}

std::uint64_t cachedBytes(std::uint64_t pageSize) {
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto query = ntdll ? reinterpret_cast<NtQuerySystemInformationFn>(
                                 GetProcAddress(ntdll, "NtQuerySystemInformation"))
                           : nullptr;
  if (!query) return cachedBytesFallback(pageSize);
  MemoryListInformation info{};
  ULONG returned = 0;
  if (query(80, &info, sizeof(info), &returned) != 0) return cachedBytesFallback(pageSize);
  ULONG_PTR standbyPages = 0;
  for (const ULONG_PTR count : info.pageCountByPriority) standbyPages += count;
  return static_cast<std::uint64_t>(standbyPages + info.modifiedPageCount) * pageSize;
}

double roundOne(double value) { return std::nearbyint(value * 10.0) / 10.0; }

}  // namespace

struct WindowsMemoryDataSource::Impl {
  ~Impl() {
    if (swapQuery) PdhCloseQuery(swapQuery);
  }

  double swapPercent() {
    std::lock_guard lock(swapMutex);
    if (swapFailed) return 0;
    if (!swapQuery) {
      if (PdhOpenQueryW(nullptr, 0, &swapQuery) != ERROR_SUCCESS ||
          PdhAddEnglishCounterW(swapQuery, L"\\Paging File(_Total)\\% Usage", 0, &swapCounter) !=
              ERROR_SUCCESS) {
        if (swapQuery) PdhCloseQuery(swapQuery);
        swapQuery = nullptr;
        swapCounter = nullptr;
        swapFailed = true;
        return 0;
      }
    }
    if (PdhCollectQueryData(swapQuery) != ERROR_SUCCESS) return 0;
    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(swapCounter, PDH_FMT_DOUBLE, nullptr, &value) !=
            ERROR_SUCCESS ||
        value.CStatus != ERROR_SUCCESS) {
      return 0;
    }
    return std::clamp(value.doubleValue, 0.0, 100.0);
  }

  std::mutex swapMutex;
  PDH_HQUERY swapQuery = nullptr;
  PDH_HCOUNTER swapCounter = nullptr;
  bool swapFailed = false;
};

WindowsMemoryDataSource::WindowsMemoryDataSource() : impl_(std::make_unique<Impl>()) {}

WindowsMemoryDataSource::~WindowsMemoryDataSource() = default;

MemorySample WindowsMemoryDataSource::sample() {
  MemorySample out;
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (!GlobalMemoryStatusEx(&status)) return out;

  out.total = status.ullTotalPhys;
  out.available = status.ullAvailPhys;
  out.used = out.total - out.available;
  out.free = out.available;
  out.percent = out.total ? roundOne(static_cast<double>(out.used) / out.total * 100) : 0;

  PERFORMANCE_INFORMATION perf{};
  perf.cb = sizeof(perf);
  if (GetPerformanceInfo(&perf, sizeof(perf)) && perf.PageSize > 0) {
    const std::uint64_t page = perf.PageSize;
    out.swapTotal =
        perf.CommitLimit > perf.PhysicalTotal ? (perf.CommitLimit - perf.PhysicalTotal) * page : 0;
    out.swapPercent = out.swapTotal ? roundOne(impl_->swapPercent()) : 0;
    out.swapUsed = static_cast<std::uint64_t>(out.swapTotal * out.swapPercent / 100.0);
    out.swapFree = out.swapTotal - out.swapUsed;
  }
  out.cached = cachedBytes(perf.PageSize);
  return out;
}

std::shared_ptr<MemoryDataSource> createMemoryDataSource() {
  return std::make_shared<WindowsMemoryDataSource>();
}

}  // namespace yasb::platform
