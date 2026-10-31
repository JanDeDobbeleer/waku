# Wabar ControlCenter widget: design

Date: 2026-10-29. Reference: Python YASB `control_center` (the Qt Widgets port that preceded it is
at the git tag `yasb-native-final`).

Layout (feature `control_center`, namespace `wabar::control_center`): `power.h`, `app_theme.h`,
`touch_keyboard.h` and `user_info.h` at the root with their Windows code in `win32/`; the
service and widget in `qml/`; the test in `tests/`. DEPENDS audio media notifications brightness:
the DND state comes from `features/notifications/dnd.h`, the brightness control from `features/brightness/brightness.h`.

## What it does

A bar button (glyph, tooltip) that opens a popup below the bar. The popup holds up to five
sections, in the order of `sectionsOrder`:

1. `systemControls`: profile picture and name (opens the account page), lock, a Settings pages
   list, a power list (sleep, hibernate, sign out, restart, shut down).
2. `quickActions`: a grid (`columns`) of toggles. Ids: `dnd`, `cycleDnd`, `mute`, `micMute`,
   `screenshot`, `touchKeyboard`, `darkMode`.
3. `sliders`: brightness, volume, microphone. Each has a slider and a source selector (monitor,
   output device, input device).
4. `power`: power plan and power mode selectors.
5. `media`: thumbnail, title, artist, previous, play and pause, next.

A right click on the button opens the Settings app.

## What is reused

| Need | Reused |
|---|---|
| Volume, mute, devices (output and input) | `AudioOutput`, `AudioInput` singletons (`audio_service`) |
| Track, transport buttons | `Media` singleton (`media_service`) |
| Slider, button, popup | `Slider.qml`, `Pill.qml`, `PopupWindow.qml` |
| Brightness (DDC/CI, scheme panel) | `brightness::BrightnessControl` (`createSystemBrightness`) |
| Focus Assist read, write, watcher | `notifications::dndStatus`, `setDndStatus`, `DndWatcher` |
| Dark mode | `control_center::themeMode`, `toggleThemeMode` |
| Touch keyboard, URI launch | `control_center::toggleTouchKeyboard`, `kernel::shellOpen` |
| Power plans and modes, session actions | `control_center::powerPlans`, `powerModes`, `lockWorkstation`, `suspendSystem`, `hibernateSystem`, `runShutdown` |
| User name and picture | `control_center::windowsUsername`, `userAvatarPath` |

No new platform code and no new undocumented API: every call is already on the allowlist in
`docs/architecture.md`.

## New code

`qml/control_center_service.{h,cpp}`: one singleton, `ControlCenter`, for what the
other singletons do not cover. Two injectable structs:

- `Backend`: reads and writes of settings (DND, theme, touch keyboard, power, user).
- `Launcher`: lock, sleep, hibernate, sign out, restart, shut down, and `open(uri)`.

QML-facing API (GUI thread). Each property notifies with its group, and every group change also emits `changed`: `dndChanged` (`dnd`, `dndActive`), `themeChanged` (`darkMode`), `brightnessChanged` (`brightnessAvailable`, `brightness`, `brightnessSource`), `brightnessSourcesChanged` (`brightnessSources`, when the monitor set changes), `powerChanged` (plans and modes), `userChanged` (`userName`, `avatarUrl`), `pendingActionChanged`:

- State: `dnd`, `dndActive`, `darkMode`, `brightnessAvailable`, `brightness`,
  `brightnessSources` (list of `{id, name}`), `brightnessSource`, `powerPlans` and `powerModes`
  (lists of `{id, name, active}`), `powerPlanName`, `powerModeName`, `powerModeAvailable`,
  `userName`, `avatarUrl`, `pendingAction`.
- Calls: `refresh()`, `toggleDnd()`, `cycleDnd()`, `toggleTheme()`, `toggleTouchKeyboard()`,
  `screenshot()`, `openSettings(uri)`, `setBrightness(percent)`, `setBrightnessSource(id)`,
  `setPowerPlan(id)`, `setPowerMode(id)`, `requestAction(id)`, `cancelAction()`.

`qml/ControlCenter.qml`: the widget, with inline components `Choice` (a collapsible
list) and `SliderRow`. Options: `glyph`, `sectionsOrder`, `quickActions`, `columns`,
`showSystemControls`, `showQuickActions`, `showSliders`, `showBrightness`, `showVolume`,
`showMicrophone`, `showSourceSelectors`, `showPower`, `showMedia`, `profileImageSize`,
`thumbnailSize`, `powerPlanTitle`, `powerModeTitle`. They follow the Python names
(`sections_order`, `columns`, `show`, `show_slider`, `show_source_selector`) in camel case.

## Decisions

- **Confirmation lives in the service, not only in QML.** `requestAction("shutdown")` returns
  false the first time and sets `pendingAction`; the same id again runs it. A different id, 4
  seconds, or `cancelAction()` (the popup closes) clears it. A test can then prove that one call
  never shuts down a machine, and a QML bug cannot skip the check. Lock, sleep and hibernate run
  at once: they lose no work.
- **Dangerous actions go through `Launcher` only.** Tests pass a recording launcher. The real one
  runs sign out, restart and shut down after the event loop ends (`aboutToQuit`), as the Widgets
  port does, so the bar is gone first.
- **`openSettings` accepts `ms-settings:` URIs only.** It is a QML-callable function; it must not
  be a general launcher (`Shell` is that, on purpose).
- **Real backends never start offscreen.** The default constructor is inert on the offscreen
  platform (no registry, power, DND, brightness or session call), as `Bluetooth` and
  `Notifications` are. Tests use the seam constructor.
- **The slow first reads run off the GUI thread.** The real object reads the user's name and
  picture, DND (an out-of-process COM call), the theme and the power plans and modes on a pool
  thread after it is built, and announces them when they arrive; a read the GUI thread made in the
  meantime (a refresh, a DND event, a switch) wins. The seam constructor reads them at once.
- **Toggling DND uses the mirrored state.** The DND watcher keeps `dnd` current, so a click does
  not ask Focus Assist first; only a state not read yet does.
- **DND "unknown" counts as on, empty as off.** Matches the Python and Widgets behaviour (only
  `disabled` is off).
- **Brightness target is the first monitor, switchable by the source selector.** The selector is
  shown when more than one monitor is controllable. DDC polling stays the platform default (60 s)
  and `refresh()` asks for a read when the popup opens.
- **Mute and mic mute are not in the service.** The widget flips `AudioOutput.muted` and
  `AudioInput.muted` directly, so the Volume widget and the control center cannot disagree.
- **Power mode is hidden** where the overlays do not exist (Windows 10) or the plan does not
  support them; the plan selector then takes the full width, as Python does.
- **Popup alignment follows `PopupWindow`** (centred on the button, clamped to the screen).

## Left out

- Custom quick actions that run a command (`command` ids in the Python options), the per-action
  `label` and `icon` overrides, `label_position`, and the CSS-class styling. Quick actions use
  fixed labels and symbols from the widget; this can follow when the wabar theme model has a
  place for them.
- Popup `alignment`, `direction`, `offset_*`, `border_color`, `round_corners`: owned by
  `PopupWindow` and the theme.
- Per-section icons as options (the widget uses plain symbols that need no icon font).
- The beep after a volume drag; the in-app screenshot editor (the system snipping overlay is
  opened, as in the Widgets port).
- Matching the brightness monitor to the monitor the bar is on (`monitorForWindow`): the first
  monitor is used until the selector says otherwise.

## Tests

`src/features/control_center/tests/test_control_center.cpp` covers the service with a fake backend, launcher and
brightness control: DND toggle and cycle, external DND events, theme and keyboard, screenshot and
`ms-settings:` filtering, brightness read, write, clamp and source switch, power plan and mode
lists and failures, unsupported modes, immediate versus confirmed actions, cancel, unknown
action, missing launcher, stop on destruction, and the inert offscreen service. The QML is
type-checked by the build (`qt_add_qml_module`); the widget has no test because no fake can be
injected into the engine singleton without a factory seam, which this change does not add.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- `wabar.services.ControlCenter` (`src/features/control_center/qml/control_center_service.{h,cpp}`): the system side of every ControlCenter widget of a `wabar` run, created by the QML engine as a singleton: Do Not Disturb (its own `wabar::notifications::DndWatcher`, WNF subscription stopped by its destructor), app theme, touch keyboard, power plans and modes, user name and picture, and the session and power actions behind an injectable launcher (shutdown, restart and sign out need a second request). Owns one `wabar::brightness::BrightnessControl` (DDC/CI worker thread plus the power-setting window thread, joined by `stop()` in the destructor). Volume, microphone and the player are not repeated: the widget uses `AudioOutput`, `AudioInput` and `Media`. Inert on the offscreen platform
