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

std::mutex g_swapMutex;
PDH_HQUERY g_swapQuery = nullptr;
PDH_HCOUNTER g_swapCounter = nullptr;
bool g_swapFailed = false;

double swapPercent() {
  std::lock_guard lock(g_swapMutex);
  if (g_swapFailed) return 0;
  if (!g_swapQuery) {
    if (PdhOpenQueryW(nullptr, 0, &g_swapQuery) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(g_swapQuery, L"\\Paging File(_Total)\\% Usage", 0, &g_swapCounter) !=
            ERROR_SUCCESS) {
      if (g_swapQuery) PdhCloseQuery(g_swapQuery);
      g_swapQuery = nullptr;
      g_swapCounter = nullptr;
      g_swapFailed = true;
      return 0;
    }
  }
  if (PdhCollectQueryData(g_swapQuery) != ERROR_SUCCESS) return 0;
  PDH_FMT_COUNTERVALUE value{};
  if (PdhGetFormattedCounterValue(g_swapCounter, PDH_FMT_DOUBLE, nullptr, &value) !=
          ERROR_SUCCESS ||
      value.CStatus != ERROR_SUCCESS) {
    return 0;
  }
  return std::clamp(value.doubleValue, 0.0, 100.0);
}

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
    out.swapPercent = out.swapTotal ? roundOne(swapPercent()) : 0;
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
