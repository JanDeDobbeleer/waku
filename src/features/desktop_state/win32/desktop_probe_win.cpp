#include <windows.h>

#include <QString>
#include <algorithm>

#include "features/desktop_state/desktop_probe.h"
#include "kernel/qt/live_desktop.h"

namespace wabar::desktop_state {
namespace {

// Documented Win32 only: GetKeyState, GetForegroundWindow, GetWindowThreadProcessId,
// GetKeyboardLayout, LCIDToLocaleName, GetLocaleInfoEx, GetLastInputInfo, GetTickCount,
// SetThreadExecutionState.

QString localeInfo(const wchar_t* localeName, LCTYPE type) {
  wchar_t buffer[128];
  const int size = GetLocaleInfoEx(localeName, type, buffer, static_cast<int>(std::size(buffer)));
  return size > 1 ? QString::fromWCharArray(buffer, size - 1) : QString();
}

class SystemProbe final : public DesktopProbe {
 public:
  LockKeys lockKeys() override {
    LockKeys keys;
    keys.caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
    keys.num = (GetKeyState(VK_NUMLOCK) & 1) != 0;
    keys.scroll = (GetKeyState(VK_SCROLL) & 1) != 0;
    return keys;
  }

  KeyboardLayoutInfo keyboardLayout() override {
    DWORD thread = 0;
    if (const HWND foreground = GetForegroundWindow()) {
      thread = GetWindowThreadProcessId(foreground, nullptr);
    }
    const HKL layout = GetKeyboardLayout(thread);  // thread 0 is the calling thread
    if (!layout) return {};
    const LANGID language = LOWORD(reinterpret_cast<UINT_PTR>(layout));
    wchar_t name[LOCALE_NAME_MAX_LENGTH];
    if (LCIDToLocaleName(MAKELCID(language, SORT_DEFAULT), name, static_cast<int>(std::size(name)),
                         0) <= 0) {
      return {};
    }
    KeyboardLayoutInfo info;
    info.shortName = localeInfo(name, LOCALE_SISO639LANGNAME).toUpper();
    info.displayName = localeInfo(name, LOCALE_SLOCALIZEDDISPLAYNAME);
    info.id = QStringLiteral("%1").arg(static_cast<quint32>(reinterpret_cast<UINT_PTR>(layout)), 8,
                                       16, QLatin1Char('0'));
    return info;
  }

  std::optional<double> idleSeconds() override {
    LASTINPUTINFO info{};
    info.cbSize = sizeof(info);
    if (!GetLastInputInfo(&info)) return std::nullopt;
    // Both are 32-bit tick counts; the unsigned difference survives the 49-day wrap.
    const DWORD elapsed = GetTickCount() - info.dwTime;
    return elapsed / 1000.0;
  }

  void setKeepAwake(bool keepAwake) override {
    const EXECUTION_STATE state =
        keepAwake ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS;
    if (!SetThreadExecutionState(state)) {
      // Nothing to do: the machine just keeps its normal sleep rules.
    }
  }
};

}  // namespace

std::unique_ptr<DesktopProbe> createSystemDesktopProbe() { return std::make_unique<SystemProbe>(); }

IdleInhibitCounter& systemIdleInhibitCounter() {
  // The probe outlives the counter: it is constructed first, destroyed last.
  static const std::unique_ptr<DesktopProbe> probe =
      kernel::liveDesktop() ? createSystemDesktopProbe() : nullptr;
  static IdleInhibitCounter counter(probe.get());
  return counter;
}

}  // namespace wabar::desktop_state
