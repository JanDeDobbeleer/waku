#include "platform/pdh.h"

#include <windows.h>
#include <pdh.h>

#include <QDebug>
#include <algorithm>
#include <mutex>

namespace yasb::platform {
namespace {

constexpr DWORD kWin1124H2Build = 26100;

int logicalCores() {
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  return std::max<DWORD>(1, info.dwNumberOfProcessors);
}

int physicalCores(int logical) {
  DWORD bytes = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
  if (bytes == 0) return std::max(1, logical / 2);
  std::vector<std::byte> buffer(bytes);
  auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &bytes))
    return std::max(1, logical / 2);
  int result = 0;
  for (DWORD offset = 0; offset < bytes;) {
    auto* entry =
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
    if (entry->Relationship == RelationProcessorCore) ++result;
    if (entry->Size == 0) break;
    offset += entry->Size;
  }
  return result > 0 ? result : std::max(1, logical / 2);
}

double readDouble(PDH_HCOUNTER counter) {
  if (!counter) return 0;
  PDH_FMT_COUNTERVALUE value{};
  if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS ||
      value.CStatus != ERROR_SUCCESS) {
    return 0;
  }
  return value.doubleValue;
}

double roundOne(double value) { return std::nearbyint(value * 10.0) / 10.0; }

}  // namespace

struct PdhCpuDataSource::Impl {
  PDH_HQUERY query = nullptr;
  PDH_HCOUNTER total = nullptr;
  PDH_HCOUNTER performance = nullptr;
  PDH_HCOUNTER baseFrequency = nullptr;
  std::vector<PDH_HCOUNTER> perCore;
  int logical = logicalCores();
  int physical = physicalCores(logical);
  double maxFrequency = 0;
  bool failed = false;
  bool logged = false;
  std::mutex mutex;

  ~Impl() {
    if (query) PdhCloseQuery(query);
  }

  bool initialize() {
    if (query) return true;
    if (failed) return false;
    PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query);
    if (status != ERROR_SUCCESS) {
      fail(QStringLiteral("Failed to open PDH query"), status);
      return false;
    }

    OSVERSIONINFOW os{};
    os.dwOSVersionInfoSize = sizeof(os);
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion =
        ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    const DWORD build = rtlGetVersion && rtlGetVersion(&os) == 0 ? os.dwBuildNumber : 0;
    const bool modern = build >= kWin1124H2Build;
    const wchar_t* counter = modern ? L"\\Processor Information(_Total)\\% Processor Time"
                                    : L"\\Processor Information(_Total)\\% Processor Utility";
    status = PdhAddEnglishCounterW(query, counter, 0, &total);
    if (status != ERROR_SUCCESS && !modern) {
      qInfo() << "PDH Processor Utility unavailable; falling back to Processor Time";
      status = PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\% Processor Time", 0,
                                     &total);
    }
    if (status != ERROR_SUCCESS) {
      fail(QStringLiteral("Failed to add CPU percent counter"), status);
      return false;
    }

    PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\% Processor Performance", 0,
                          &performance);
    PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\Processor Frequency", 0,
                          &baseFrequency);
    perCore.reserve(static_cast<size_t>(logical));
    for (int i = 0; i < logical; ++i) {
      PDH_HCOUNTER item = nullptr;
      std::wstring path = L"\\Processor Information(0," + std::to_wstring(i) + L")\\";
      path += modern ? L"% Processor Time" : L"% Processor Utility";
      status = PdhAddEnglishCounterW(query, path.c_str(), 0, &item);
      if (status != ERROR_SUCCESS && !modern) {
        path = L"\\Processor Information(0," + std::to_wstring(i) + L")\\% Processor Time";
        PdhAddEnglishCounterW(query, path.c_str(), 0, &item);
      }
      perCore.push_back(item);
    }

    PdhCollectQueryData(query);
    if (baseFrequency) {
      PdhCollectQueryData(query);
      PDH_FMT_COUNTERVALUE value{};
      if (PdhGetFormattedCounterValue(baseFrequency, PDH_FMT_LARGE, nullptr, &value) ==
              ERROR_SUCCESS &&
          value.CStatus == ERROR_SUCCESS) {
        maxFrequency = static_cast<double>(value.largeValue);
      }
    }
    return true;
  }

  void fail(const QString& message, PDH_STATUS status) {
    if (!logged) {
      qWarning().noquote() << message << "(status" << status
                           << "); CPU widget will show default values";
      logged = true;
    }
    if (query) PdhCloseQuery(query);
    query = nullptr;
    failed = true;
  }
};

PdhCpuDataSource::PdhCpuDataSource() : impl_(std::make_unique<Impl>()) {}
PdhCpuDataSource::~PdhCpuDataSource() = default;

CpuSample PdhCpuDataSource::sample() {
  std::lock_guard lock(impl_->mutex);
  CpuSample result;
  result.coresLogical = impl_->logical;
  result.coresPhysical = impl_->physical;
  result.percentPerCore.assign(static_cast<size_t>(impl_->logical), 0);
  result.frequencyMax = impl_->maxFrequency;
  result.frequencyCurrent = impl_->maxFrequency;
  if (!impl_->initialize()) return result;
  if (PdhCollectQueryData(impl_->query) != ERROR_SUCCESS) return result;
  result.percent = std::clamp(roundOne(readDouble(impl_->total)), 0.0, 100.0);
  const double performance = readDouble(impl_->performance);
  if (performance > 0)
    result.frequencyCurrent = roundOne(impl_->maxFrequency * performance / 100.0);
  for (size_t i = 0; i < impl_->perCore.size(); ++i) {
    result.percentPerCore[i] = std::clamp(roundOne(readDouble(impl_->perCore[i])), 0.0, 100.0);
  }
  return result;
}

std::shared_ptr<CpuDataSource> createCpuDataSource() {
  return std::make_shared<PdhCpuDataSource>();
}

}  // namespace yasb::platform
