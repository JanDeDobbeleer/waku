// The real KomorebiManager. Documented APIs only (CreateToolhelp32Snapshot for the process check),
// so no row in the allowlist of docs/architecture.md. komorebic reads its own config path
// (`komorebic configuration`, the komorebi.json under KOMOREBI_CONFIG_HOME) and hands it to
// `replace-configuration`, which rebuilds komorebi and respawns komorebi-bar (design.md,
// "Managing komorebi").
#include <Windows.h>
#include <TlHelp32.h>

#include "features/workspaces/komorebi_workarea.h"
#include "kernel/win32/shell_launch.h"

namespace waku::workspaces::komorebi {
namespace {

bool isProcessRunning(const wchar_t* exeName) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !found;
       ok = Process32NextW(snapshot, &entry)) {
    found = _wcsicmp(entry.szExeFile, exeName) == 0;
  }
  CloseHandle(snapshot);
  return found;
}

class Win32KomorebiManager final : public KomorebiManager {
 public:
  KomorebiManageResult reloadConfiguration() override {
    KomorebiManageResult result;
    result.barRunning = isProcessRunning(L"komorebi.exe") && isProcessRunning(L"komorebi-bar.exe");
    if (!result.barRunning) return result;
    // No path is built here: komorebic prints it, cmd hands it over. Nothing happens when
    // komorebic prints no path.
    result.started = kernel::launchHiddenShell(
        QStringLiteral("for /f \"usebackq delims=\" %i in (`komorebic.exe configuration`) do "
                       "komorebic.exe replace-configuration \"%i\""));
    return result;
  }
};

}  // namespace

std::unique_ptr<KomorebiManager> makeWin32KomorebiManager() {
  return std::make_unique<Win32KomorebiManager>();
}

}  // namespace waku::workspaces::komorebi
