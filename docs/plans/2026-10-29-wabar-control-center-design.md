# Wabar ControlCenter widget: design

Date: 2026-10-29. Branch `spike/qt-quick-bar`. Reference: Python YASB `control_center` and the Qt
Widgets port in `widgets/control_center/`.

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
| Brightness (DDC/CI, scheme panel) | `platform::BrightnessControl` (`createSystemBrightness`) |
| Focus Assist read, write, watcher | `platform::dndStatus`, `setDndStatus`, `DndWatcher` |
| Dark mode | `platform::themeMode`, `toggleThemeMode` |
| Touch keyboard, URI launch | `platform::toggleTouchKeyboard`, `shellOpen` |
| Power plans and modes, session actions | `platform::powerPlans`, `powerModes`, `lockWorkstation`, `suspendSystem`, `hibernateSystem`, `runShutdown` |
| User name and picture | `platform::windowsUsername`, `userAvatarPath` |

No new platform code and no new undocumented API: every call is already on the allowlist in
`docs/architecture.md`.

## New code

`spike/services/control_center_service.{h,cpp}`: one singleton, `ControlCenter`, for what the
other singletons do not cover. Two injectable structs:

- `Backend`: reads and writes of settings (DND, theme, touch keyboard, power, user).
- `Launcher`: lock, sleep, hibernate, sign out, restart, shut down, and `open(uri)`.

QML-facing API (all `changed`-notified, GUI thread):

- State: `dnd`, `dndActive`, `darkMode`, `brightnessAvailable`, `brightness`,
  `brightnessSources` (list of `{id, name}`), `brightnessSource`, `powerPlans` and `powerModes`
  (lists of `{id, name, active}`), `powerPlanName`, `powerModeName`, `powerModeAvailable`,
  `userName`, `avatarUrl`, `pendingAction`.
- Calls: `refresh()`, `toggleDnd()`, `cycleDnd()`, `toggleTheme()`, `toggleTouchKeyboard()`,
  `screenshot()`, `openSettings(uri)`, `setBrightness(percent)`, `setBrightnessSource(id)`,
  `setPowerPlan(id)`, `setPowerMode(id)`, `requestAction(id)`, `cancelAction()`.

`spike/widgets/ControlCenter.qml`: the widget, with inline components `Choice` (a collapsible
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

`spike/tests/test_control_center.cpp` covers the service with a fake backend, launcher and
brightness control: DND toggle and cycle, external DND events, theme and keyboard, screenshot and
`ms-settings:` filtering, brightness read, write, clamp and source switch, power plan and mode
lists and failures, unsupported modes, immediate versus confirmed actions, cancel, unknown
action, missing launcher, stop on destruction, and the inert offscreen service. The QML is
type-checked by the build (`qt_add_qml_module`); the widget has no test because no fake can be
injected into the engine singleton without a factory seam, which this change does not add.
