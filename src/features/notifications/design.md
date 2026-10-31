# notifications

The Action Center toasts (Python `NotificationService`), toast activation, and Do Not Disturb
(Focus Assist, quiet hours). Do Not Disturb lives here because quiet hours is a notifications
concept; control_center reads it through `DEPENDS notifications`.

## Pieces

- `notifications.h/.cpp`: `NotificationItem`, `NotificationSource` (the seam), `NotificationSink` and
  `NotificationService`, which re-posts every source event to its own thread.
- `toast_xml.*`: toast XML parsing (pure, tested).
- `toast_activation.h`: activation entry points; `win32/toast_activation_win.cpp` implements them
  (protocol launch, the app's `INotificationActivationCallback`, bring-forward).
- `dnd.h`: `dndStatus`, `setDndStatus` and `DndWatcher`; `win32/dnd_win.cpp` uses the undocumented
  `IQuietHoursSettings` and a WNF subscription (both allowlisted in `docs/architecture.md`).
- `win32/notifications_system.cpp`: the `UserNotificationListener` source and its worker thread.
- `qml/notification_service.*`, `qml/Notifications.qml`: the `Notifications` singleton and widget.
- `qml/notification_feed.*`: the Quickshell-shaped feed (below).
- `testing/fake_notification_source.h`: `FakeSource` for the tests.

## Feed (Quickshell.Services.Notifications)

For QML written against `Quickshell.Services.Notifications` (caelestia `services/Notifs.qml`,
`NotifData.qml`). The `Notifications` singleton and the feed share one `NotificationService`
(`acquireNotificationService()`), so the listener runs once.

| QML name | Class | Notes |
|---|---|---|
| `NotificationFeed` (singleton) | `NotificationFeed` | `notifications` (model with `values`, newest first), `available`, `toastAdded(toast)` for each toast that arrives while the bar runs, `clear()`, `refresh()` |
| `NotificationServer` | `NotificationServer` | `onNotification: notif => ...` fires like `toastAdded`; the capability flags (`keepOnReload`, `actionsSupported`, `bodyMarkupSupported`, ...) are accepted and ignored; `trackedNotifications` lists toasts with `tracked: true` |
| `Notification` | `NotificationToast` | `id`, `appName`, `appIcon` (the toast's app logo), `summary` (title), `body`, `image` (inline image, else the hero), `urgency`, `time` (date), `timeText`, `tracked`, `aumid`, and the fixed Quickshell members `actions` (empty), `hints` (empty), `expireTimeout` and `lastGeneration` (0), `resident`, `transient`, `hasActionIcons`, `hasInlineReply` (false), `desktopEntry`, `inlineReplyPlaceholder` (empty). Every property has its `...Changed` signal, so a `Connections` handler loads without a warning; the fixed ones never fire. `dismiss()`, `close()`, `activate()`, `expire()` (no-op), `sendInlineReply()` (no-op), signal `closed(reason)` |
| `NotificationAction` | `NotificationAction` | `identifier`, `text`, `invoke()`; uncreatable, never instantiated (`actions` is empty) |
| `NotificationUrgency`, `NotificationCloseReason` | enum holders | `Low`, `Normal`, `Critical`; `Expired`, `Dismissed`, `CloseRequested` |

Toasts that were in the Action Center before the bar started fill the model without `toastAdded`.
`urgency` comes from the toast's `scenario` attribute: `alarm`, `incomingCall` and `urgent` are
`Critical`, everything else `Normal` (Windows has no low priority).

Always empty or fixed on Windows because the OS owns the toast and its popup: `actions` (empty
list), `hints` (empty), `resident`, `transient`, `hasActionIcons`, `hasInlineReply` (false),
`expireTimeout` (0), and `expire()` does nothing. There is no server to register, so no app sends
wabar a notification and `NotificationServer` flags have no effect. `dismiss()` removes the toast
from the Action Center; `activate()` hands it to its app.
The `Quickshell.Services.Notifications` module registers `Notification`, `NotificationServer`,
`NotificationUrgency`, `NotificationCloseReason` and `NotificationAction` again under the same names
(`src/app/wabar/quickshell_types_notifications.h`); `NotificationFeed` stays in `wabar.services`.

## Threads

A worker `std::thread` (MTA) owns the listener and runs reads, removes, clears and toast activation;
a WNF callback runs on a system thread. A second WNF callback belongs to `DndWatcher`. Owners and
stops are listed in the threads table of `docs/architecture.md`.

## Depends on

kernel only. `toast_activation_win.cpp` still takes `activateAppByAumid` from the media feature's
header until the kernel app-activation helper replaces it.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- notifications worker (`std::thread`, MTA) and WNF callback (`NotificationService`'s `SystemNotificationSource` (`src/features/notifications/win32/notifications_system.cpp`), created on first use by `wabar.services.Notifications`): the worker owns the `UserNotificationListener` and runs list reads (100 ms debounce after each WNF change or menu open), remove, clear (all or one app) and toast activation tasks, sleeping on a condition variable (no polling); the WNF callback runs on a system thread, arms the debounced read and forwards a `u32` count to the sink, which posts both results to the GUI thread. Toast activation (`src/features/notifications/win32/toast_activation_win.cpp`) runs on this thread

- `wabar.services.NotificationFeed` (with `NotificationServer` and the `Notification` objects, each property with its `...Changed` signal, and `NotificationAction`) (`src/features/notifications/qml/notification_feed.{h,cpp}`): the Action Center toasts as a Quickshell-shaped model with `toastAdded`, created by the QML engine as a singleton on the GUI thread. A `NotificationHub` (`weak_ptr` in a function-local static from `acquireNotificationHub()`, `shared_ptr` held by the feed and by each `NotificationServer`) owns the toast objects and shares the process's one `NotificationService` with `wabar.services.Notifications` (`acquireNotificationService()`), so the listener thread exists once and is joined when the last holder is destroyed
