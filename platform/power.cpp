#include "platform/power.h"

#include <windows.h>
#include <powrprof.h>

#include <QDebug>
#include <QStringList>
#include <algorithm>
#include <vector>

namespace yasb::platform {
namespace {

constexpr wchar_t kPowerSchemesKey[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes";

// Overlay GUIDs of the Windows 11 Power Mode setting (undocumented.py).
const QUuid kModeBestEfficiency(0x961cc777, 0x2547, 0x4f9d, 0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b,
                                0x8a, 0x7a);
const QUuid kModeBestPerformance(0xded574b5, 0x45a0, 0x4f42, 0x87, 0x37, 0x46, 0x34, 0x5c, 0x09,
                                 0xc2, 0x38);
// GUID_POWERSCHEME_PERSONALITY {245D8541-3943-4422-B025-13A784F679B7}
const QUuid kPersonality(0x245d8541, 0x3943, 0x4422, 0xb0, 0x25, 0x13, 0xa7, 0x84, 0xf6, 0x79,
                         0xb7);

QUuid toQUuid(const GUID& guid) { return QUuid(guid); }
GUID toGuid(const QUuid& uuid) { return static_cast<GUID>(uuid); }

std::optional<QUuid> activeScheme() {
  GUID* scheme = nullptr;
  if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS || !scheme) return std::nullopt;
  const QUuid result = toQUuid(*scheme);
  LocalFree(scheme);
  return result;
}

// powrprof exports that are missing on older Windows 10 builds (docs/architecture.md allowlist).
using ModeGetFn = DWORD(WINAPI*)(GUID*);
using ModeSetFn = DWORD(WINAPI*)(GUID*);

HMODULE powrprofModule() {
  static const HMODULE module = LoadLibraryW(L"powrprof.dll");
  return module;
}

template <typename Fn>
Fn modeExport(const char* name) {
  const HMODULE module = powrprofModule();
  return module ? reinterpret_cast<Fn>(GetProcAddress(module, name)) : nullptr;
}

std::optional<DWORD> readRegistryDword(const std::wstring& subKey, const wchar_t* value) {
  DWORD data = 0;
  DWORD size = sizeof(data);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, subKey.c_str(), value, RRF_RT_REG_DWORD, nullptr, &data,
                   &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  return data;
}

std::optional<PowerMode> modeFor(const QUuid& guid) {
  for (const PowerMode& mode : powerModes()) {
    if (mode.guid == guid) return mode;
  }
  return std::nullopt;
}

bool runHidden(const QString& commandLine) {
  std::wstring mutableLine = commandLine.toStdWString();
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info{};
  if (!CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &startup, &info)) {
    return false;
  }
  CloseHandle(info.hThread);
  CloseHandle(info.hProcess);
  return true;
}

bool enableShutdownPrivilege() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    return false;
  TOKEN_PRIVILEGES privileges{};
  privileges.PrivilegeCount = 1;
  privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  bool ok = LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid) &&
            AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr) &&
            GetLastError() == ERROR_SUCCESS;
  CloseHandle(token);
  return ok;
}

}  // namespace

std::vector<PowerPlan> powerPlans(std::optional<QUuid>* active) {
  std::vector<PowerPlan> plans;
  for (ULONG index = 0;; ++index) {
    GUID guid{};
    DWORD size = sizeof(guid);
    if (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, index,
                       reinterpret_cast<UCHAR*>(&guid), &size) != ERROR_SUCCESS) {
      break;
    }
    std::vector<UCHAR> buffer(1024);
    DWORD nameSize = static_cast<DWORD>(buffer.size());
    PowerReadFriendlyName(nullptr, &guid, nullptr, nullptr, buffer.data(), &nameSize);
    const auto* chars = reinterpret_cast<const wchar_t*>(buffer.data());
    QString name = QString::fromWCharArray(chars, static_cast<int>(nameSize / sizeof(wchar_t)));
    name.remove(QChar(0));
    plans.push_back({toQUuid(guid), name.trimmed()});
  }
  if (active) *active = activeScheme();
  return plans;
}

unsigned long setPowerPlan(const QUuid& guid) {
  const GUID value = toGuid(guid);
  return PowerSetActiveScheme(nullptr, &value);
}

std::vector<PowerMode> powerModes() {
  return {{QStringLiteral("Best power efficiency"), kModeBestEfficiency},
          {QStringLiteral("Balanced"), QUuid()},
          {QStringLiteral("Best performance"), kModeBestPerformance}};
}

bool powerModeSupported() {
  const auto plan = activeScheme();
  if (!plan) return false;
  const std::wstring key = std::wstring(kPowerSchemesKey) + L"\\" +
                           plan->toString(QUuid::WithoutBraces).toStdWString() + L"\\" +
                           kPersonality.toString(QUuid::WithoutBraces).toStdWString();
  const auto personality = readRegistryDword(key, L"ACSettingIndex");
  return personality && *personality == 2;
}

std::optional<PowerMode> activePowerMode() {
  if (const auto getter = modeExport<ModeGetFn>("PowerGetUserConfiguredACPowerMode")) {
    GUID guid{};
    if (getter(&guid) == 0) return modeFor(toQUuid(guid));
  }
  wchar_t buffer[128] = {};
  DWORD size = sizeof(buffer);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, kPowerSchemesKey, L"ActiveOverlayAcPowerScheme",
                   RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  const QUuid guid = QUuid::fromString(QString::fromWCharArray(buffer).trimmed());
  return modeFor(guid);
}

bool setPowerMode(const QUuid& guid) {
  GUID value = toGuid(guid);
  bool ok = true;
  const auto setAc = modeExport<ModeSetFn>("PowerSetUserConfiguredACPowerMode");
  const auto setDc = modeExport<ModeSetFn>("PowerSetUserConfiguredDCPowerMode");
  if (!setAc || setAc(&value) != 0) ok = false;
  if (!setDc || setDc(&value) != 0) ok = false;
  return ok;
}

bool isWindows10() {
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
  static const bool windows10 = [] {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto getVersion =
        ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (!getVersion || getVersion(&info) != 0) return false;
    return info.dwMajorVersion == 10 && info.dwBuildNumber < 22000;
  }();
  return windows10;
}

void lockWorkstation() { LockWorkStation(); }

void suspendSystem() {
  if (enableShutdownPrivilege()) {
    if (!SetSuspendState(FALSE, TRUE, FALSE)) qWarning() << "Sleep operation failed";
    return;
  }
  runHidden(QStringLiteral("rundll32.exe powrprof.dll,SetSuspendState Sleep"));
}

void hibernateSystem() { runHidden(QStringLiteral("shutdown /h")); }

bool runShutdown(const QStringList& arguments) {
  return runHidden(QStringLiteral("shutdown ") + arguments.join(QLatin1Char(' ')));
}

}  // namespace yasb::platform
