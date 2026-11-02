# desktop_state

Lock keys, keyboard layout, idle time and the idle inhibitor, for QML written against
Quickshell (caelestia reads `Hypr.capsLock`, `Hypr.numLock`, `Hypr.kbLayout` and uses
`IdleInhibitor`, `IdleMonitor`).

## Types

| QML name | Class | Notes |
|---|---|---|
| `DesktopState` (singleton) | `DesktopState` | `capsLock`, `numLock`, `scrollLock`, `keyboardLayout` ("EN"), `keyboardLayoutName` ("English (United States)"), `keyboardLayoutId` (layout handle in hex), `idleSeconds` (whole seconds since the last input), `available`, `refresh()` |
| `IdleInhibitor` | `IdleInhibitor` | `enabled`; `window` is accepted and ignored. While enabled the display and the system stay awake |

`IdleMonitor` (Quickshell.Wayland) is not provided; `DesktopState.idleSeconds` covers it.

## Polling

Windows has no cheap documented change notification for these values. The singleton polls every
200 ms through `DesktopProbe`, and only while something observes a change signal: a QML binding or
a connection on `capsLockChanged`, `numLockChanged`, `scrollLockChanged`,
`keyboardLayoutChanged` or `idleSecondsChanged` starts the timer; the last one disconnecting stops
it. Qt calls `disconnectNotify` for C++ connections, so those stop the timer exactly. A destroyed
QML binding does not, and QML keeps a conservative per-signal mask, so once a binding has observed
a signal the timer keeps running until the singleton is destroyed (bar widgets live as long as the
bar, so this costs nothing in practice). A read from script with nothing observing refreshes the value first, without a signal. A low
level keyboard hook was left out: it is a system-wide hook for three bits.

`idleSeconds` is an integer and changes at most once per second, so a binding re-evaluates once
per second, not five times. When only `idleSecondsChanged` is observed the timer runs every second
(`kIdlePollIntervalMs`); a lock key or layout observer moves it to 200 ms. The Win32 probe caches
the layout names per `HKL`, so a tick that sees the same handle makes two calls, not six.

## Windows APIs

All documented. `GetKeyState` (`VK_CAPITAL`, `VK_NUMLOCK`, `VK_SCROLL`, toggle bit),
`GetForegroundWindow`, `GetWindowThreadProcessId`, `GetKeyboardLayout`, `LCIDToLocaleName`,
`GetLocaleInfoEx` (`LOCALE_SISO639LANGNAME`, `LOCALE_SLOCALIZEDDISPLAYNAME`), `GetLastInputInfo`
with `GetTickCount` (32-bit tick difference, safe over the 49-day wrap), and
`SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED)`.

- `GetKeyState` reads the calling thread's view of the keyboard; the toggle bits track the real
  keys while the GUI thread pumps messages.
- `GetLastInputInfo` sees input of the current session only; it does not see remote desktop
  injected input differently from local input.
- `SetThreadExecutionState` with `ES_CONTINUOUS` lasts until it is cleared or the thread ends. It is
  always called from the GUI thread. Several `IdleInhibitor` objects share one state through
  `IdleInhibitCounter`.
- A failed read gives the previous value (`keyboardLayout` empty, `idleSeconds` 0).

## Threads

None. A GUI-thread `QTimer`.

## Depends on

kernel only (`liveDesktop()`).
