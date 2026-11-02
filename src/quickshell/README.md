# quickshell: the Quickshell compatibility layer

Shells written for Quickshell import `Quickshell`, `Quickshell.Io`, `Quickshell.Wayland` and the
other modules below. This unit builds those modules. It holds no engine logic of its own: every
type is a waku type registered again under Quickshell's name (`QML_FOREIGN`), or a QML file over
a waku type or service. The engine C++ stays in `src/host/qml/` and in the features.

Dependencies point one way: `app` -> `quickshell` -> (`features`, `host`). The unit may include
`host/qml/`, `control/`, `kernel/` and the root and `qml/` headers of any feature. Only `app/` and
`tests/` include it (`tools/check.ps1`, rule S2). `waku` and `waku_tests` link one target of it,
`waku_quickshell_modules`.

A module does not re-export `waku` or `waku.services`: `import Quickshell` shows Quickshell's
names only, so a shell's own `Wallpapers.qml` is never shadowed by a waku service.

Two names are the exception to "the same type as in `waku`": `PopupWindow` and `Variants` of
module `Quickshell` are QML wrappers (`core/qml/PopupWindow.qml`, `core/qml/Variants.qml`) of the
`waku` types. `Variants` behaves the same. `Quickshell.PopupWindow` is not the themed `waku.PopupWindow`: it is
the bare `PopupWindowBase` (anchor, placement, open/close, no surface and no column), as in Quickshell, so
a child with `anchors.fill: parent` fills it. Both are sized by `implicitWidth` and `implicitHeight`.

| Folder | Module | Built by |
|---|---|---|
| `core/` | `Quickshell` | `core/CMakeLists.txt` (`waku_quickshell`) |
| `wayland/` | `Quickshell.Wayland` | `waku_quickshell_qml_module()` |
| `widgets/` | `Quickshell.Widgets` | `waku_quickshell_qml_module()` |
| `hyprland/` | `Quickshell.Hyprland` | `waku_quickshell_qml_module()` |
| `bluetooth/` | `Quickshell.Bluetooth` | `waku_quickshell_qml_module()` |
| `networking/` | `Quickshell.Networking` | `waku_quickshell_qml_module()` |
| `services/upower/` | `Quickshell.Services.UPower` | `waku_quickshell_qml_module()` |
| `services/pipewire/` | `Quickshell.Services.Pipewire` | `waku_quickshell_qml_module()` |
| `services/systemtray/` | `Quickshell.Services.SystemTray` | `waku_quickshell_qml_module()` |
| `io/` | `Quickshell.Io` | `waku_quickshell_service_module()` |
| `services/mpris/` | `Quickshell.Services.Mpris` | `waku_quickshell_service_module()` |
| `services/notifications/` | `Quickshell.Services.Notifications` | `waku_quickshell_service_module()` |

Both functions are in `CMakeLists.txt` here. Tests: `tests/` (the four `test_quickshell*.cpp`),
`src/features/quickshell_io/tests/` and `tests/integration/test_quickshell_glue.cpp`. Behaviour
and limits per type: the QML module table in `docs/architecture.md`.

## Where each type lives

| Quickshell type | Module | waku implementation | Registered by |
|---|---|---|---|
| `ShellRoot`, `Scope`, `Singleton` | `Quickshell` | `src/host/qml/scope.h` | `core/qml/quickshell_types.h` |
| `PanelWindow` | `Quickshell` | `src/host/qml/panel_window.*` | `core/qml/quickshell_types.h` |
| `FloatingWindow` | `Quickshell` | `src/host/qml/floating_window.*` | `core/qml/quickshell_types.h` |
| `QsWindow` | `Quickshell` | `src/host/qml/qs_window.*` | `core/qml/quickshell_types.h` |
| `PopupWindow` | `Quickshell` | `src/host/qml/PopupWindow.qml` | `core/qml/PopupWindow.qml` (wrapper) |
| `Variants` | `Quickshell` | `src/host/qml/Variants.qml` | `core/qml/Variants.qml` (wrapper) |
| `LazyLoader` | `Quickshell` | `src/host/qml/lazy_loader.*` | `core/qml/quickshell_types.h` |
| `GlobalShortcut` | `Quickshell`, `Quickshell.Hyprland` | `src/host/qml/global_shortcut.*` | `core/qml/quickshell_types.h`, `hyprland/GlobalShortcut.qml` |
| `IpcHandler` | `Quickshell` | `src/host/qml/ipc_handler.*` | `core/qml/quickshell_types.h` |
| `PersistentProperties` | `Quickshell` | `src/host/qml/persistent_properties.*` | `core/qml/quickshell_types.h` |
| `ScriptModel` | `Quickshell` | `src/host/qml/script_model.*` | `core/qml/quickshell_types.h` |
| `SystemClock` | `Quickshell` | `src/host/qml/system_clock.*` | `core/qml/quickshell_types.h` |
| `TransformWatcher` | `Quickshell` | `src/host/qml/transform_watcher.*` | `core/qml/quickshell_types.h` |
| `ShellScreen` | `Quickshell` | `src/host/qml/shell_screen.*` | `core/qml/quickshell_types.h` |
| `ExclusionMode`, `Edges` | `Quickshell` | `src/host/qml/window_enums.h` | `core/qml/quickshell_types.h` |
| `Region`, `Intersection`, `RegionShape` | `Quickshell` | `src/host/qml/region.*` | `core/qml/quickshell_types.h` |
| `Quickshell` (singleton) | `Quickshell` | `src/features/quickshell_io/qml/quickshell_singleton.*`, `screens` from `src/host/qml/shell_screen.*` | `core/qml/Quickshell.qml` |
| `DesktopEntries` | `Quickshell` | `src/features/apps/qml/apps_service.*` | `core/qml/DesktopEntries.qml` |
| `DesktopEntry`, `DesktopAction` | `Quickshell` | `src/features/apps/qml/desktop_entry.*` | `core/qml/quickshell_types_core.h` |
| `ElapsedTimer` | `Quickshell` | `src/features/quickshell_io/qml/quickshell_singleton.*` | `core/qml/quickshell_types_core.h` |
| `WlrLayershell`, `WlrLayer`, `WlrKeyboardFocus` | `Quickshell.Wayland` | `src/host/qml/wlr_layershell.*`, `src/host/qml/window_enums.h` | `wayland/qml/wayland_types.h` |
| `IdleInhibitor` | `Quickshell.Wayland` | `src/features/desktop_state/qml/idle_inhibitor.*` | `wayland/qml/wayland_types.h` |
| `ToplevelManager`, `Toplevel` | `Quickshell.Wayland` | `waku.services` `TaskbarService`, `TaskbarModel` | `wayland/ToplevelManager.qml`, `wayland/Toplevel.qml` |
| `IdleMonitor` | `Quickshell.Wayland` | `waku.services` `DesktopState` (`idleSeconds`) | `wayland/IdleMonitor.qml` |
| `QsMenuOpener`, `QsMenuHandle` | `Quickshell` | plain QML (no entries: a tray item's `menu` is null) | `core/qml/QsMenuOpener.qml`, `core/qml/QsMenuHandle.qml` |
| `Process` | `Quickshell.Io` | `src/features/quickshell_io/qml/qs_process.*` | `io/qml/quickshell_types_io.h` |
| `DataStreamParser`, `StdioCollector`, `SplitParser` | `Quickshell.Io` | `src/features/quickshell_io/qml/data_stream_parsers.*` | `io/qml/quickshell_types_io.h` |
| `FileView`, `FileViewAdapter`, `FileViewError` | `Quickshell.Io` | `src/features/quickshell_io/qml/file_view.*` | `io/qml/quickshell_types_io.h` |
| `JsonObject`, `JsonAdapter` | `Quickshell.Io` | `src/features/quickshell_io/qml/json_adapter.*` | `io/qml/quickshell_types_io.h` |
| `Mpris`, `MprisPlayer`, `MprisPlaybackState`, `MprisLoopState` | `Quickshell.Services.Mpris` | `src/features/media/qml/mpris.*` | `services/mpris/qml/quickshell_types_mpris.h` |
| `NotificationServer`, `Notification`, `NotificationAction`, `NotificationUrgency`, `NotificationCloseReason` | `Quickshell.Services.Notifications` | `src/features/notifications/qml/notification_feed.*` | `services/notifications/qml/quickshell_types_notifications.h` |
| `IconImage`, `ClippingRectangle`, `ClippingWrapperRectangle`, `WrapperRectangle`, `WrapperItem`, `WrapperMouseArea` | `Quickshell.Widgets` | plain QML | `widgets/*.qml` |
| `Hyprland`, `HyprlandFocusGrab`, `HyprlandMonitor`, `HyprlandWorkspace`, `HyprlandToplevel`, `HyprlandEvent` | `Quickshell.Hyprland` | plain QML (empty models, no-op `dispatch`) | `hyprland/*.qml` |
| `UPower`, `UPowerDevice`, `PowerProfiles` and their enums | `Quickshell.Services.UPower` | `waku.services` `Battery`, `ControlCenter` | `services/upower/*.qml`, `services/upower/qml/upower_base.h` |
| `Pipewire`, `PwNode`, `PwNodeAudio`, `PwLink`, `PwLinkGroup`, `PwObjectTracker`, `PwNodeLinkTracker` | `Quickshell.Services.Pipewire` | `waku.services` `AudioOutput`, `AudioInput`, `AudioStreams` | `services/pipewire/*.qml` |
| `SystemTray`, `SystemTrayItem`, `Status` | `Quickshell.Services.SystemTray` | `waku.services` `Systray` | `services/systemtray/SystemTray.qml`, `services/systemtray/qml/system_tray_item.*` |
| `Bluetooth`, `BluetoothAdapter`, `BluetoothDevice` and their state enums | `Quickshell.Bluetooth` | `waku.services` `Bluetooth` | `bluetooth/*.qml` |
| `Networking` (`wifiEnabled`, `wifiHardwareEnabled`) | `Quickshell.Networking` (a noctalia-qs addition) | `waku.services` `Network` | `networking/Networking.qml` |

`src/app/waku/quickshell_glue.h` joins the `Quickshell` singleton to the shell host (`shellDir`,
`reload()`). It stays in `app/`: it is composition-root wiring, not a registration.
