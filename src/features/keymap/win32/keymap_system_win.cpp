#include <windows.h>

#include <QProcessEnvironment>
#include <QString>
#include <vector>

#include "features/keymap/keymap_action.h"
#include "features/keymap/keymap_system.h"
#include "features/keymap/window_match.h"
#include "kernel/win32/shell_launch.h"
#include "kernel/win32/window_focus.h"

namespace waku::keymap {
namespace {

// The executable file name of the process that owns `hwnd` ("firefox.exe"); "" when the process
// cannot be opened (an elevated one). OpenProcess and QueryFullProcessImageNameW are documented.
QString processNameOfWindow(HWND hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == 0) return QString();
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return QString();
  wchar_t buffer[MAX_PATH * 2];
  DWORD size = static_cast<DWORD>(std::size(buffer));
  const bool ok = QueryFullProcessImageNameW(process, 0, buffer, &size) != 0;
  CloseHandle(process);
  if (!ok) return QString();
  const QString path = QString::fromWCharArray(buffer, static_cast<qsizetype>(size));
  return path.mid(path.lastIndexOf(QLatin1Char('\\')) + 1);
}

QString titleOfWindow(HWND hwnd) {
  wchar_t buffer[512];
  const int length = GetWindowTextW(hwnd, buffer, static_cast<int>(std::size(buffer)));
  return length > 0 ? QString::fromWCharArray(buffer, length) : QString();
}

struct Candidate {
  HWND hwnd = nullptr;
  QString process;
  QString title;
};

// The windows a user would call "the app's window": visible, titled, top level, not a tool window.
BOOL CALLBACK collectWindow(HWND hwnd, LPARAM param) {
  if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
  const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if ((exStyle & WS_EX_TOOLWINDOW) != 0 && (exStyle & WS_EX_APPWINDOW) == 0) return TRUE;
  Candidate candidate;
  candidate.hwnd = hwnd;
  candidate.title = titleOfWindow(hwnd);
  if (candidate.title.isEmpty()) return TRUE;
  candidate.process = processNameOfWindow(hwnd);
  reinterpret_cast<std::vector<Candidate>*>(param)->push_back(std::move(candidate));
  return TRUE;
}

class Win32WindowSystem final : public WindowSystem {
 public:
  QString foregroundProcessName() override {
    HWND hwnd = GetForegroundWindow();
    return hwnd ? processNameOfWindow(hwnd) : QString();
  }

  bool activateWindow(const QString& target) override {
    std::vector<Candidate> windows;  // front to back
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&windows));
    // The executable wins over a title that merely contains the word.
    const Candidate* found = nullptr;
    for (const Candidate& candidate : windows) {
      if (processNamesMatch(target, candidate.process)) {
        found = &candidate;
        break;
      }
    }
    if (!found) {
      for (const Candidate& candidate : windows) {
        if (windowMatchesTarget(target, QString(), candidate.title)) {
          found = &candidate;
          break;
        }
      }
    }
    if (!found) return false;
    if (IsIconic(found->hwnd)) ShowWindow(found->hwnd, SW_RESTORE);
    if (GetForegroundWindow() != found->hwnd) {
      kernel::forceForegroundFocus(reinterpret_cast<std::uintptr_t>(found->hwnd));
    }
    return true;
  }

  void allowForeground(unsigned long pid) override {
    AllowSetForegroundWindow(static_cast<DWORD>(pid));
  }
};

class Win32ProcessLauncher final : public ProcessLauncher {
 public:
  explicit Win32ProcessLauncher(QString pathPrefix) : pathPrefix_(std::move(pathPrefix)) {}

  bool launch(const ShellInvocation& invocation) override {
    // Read the environment each time: it is cheap and follows a changed PATH of the session.
    const QProcessEnvironment environment =
        withPathPrefix(QProcessEnvironment::systemEnvironment(), pathPrefix_);
    return kernel::startDetachedHidden(invocation.program, invocation.args, invocation.nativeArgs,
                                       &environment);
  }

 private:
  QString pathPrefix_;
};

}  // namespace

std::unique_ptr<WindowSystem> makeWin32WindowSystem() {
  return std::make_unique<Win32WindowSystem>();
}

std::unique_ptr<ProcessLauncher> makeWin32ProcessLauncher(const QString& pathPrefix) {
  return std::make_unique<Win32ProcessLauncher>(pathPrefix);
}

}  // namespace waku::keymap
