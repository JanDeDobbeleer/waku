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
- `testing/fake_notification_source.h`: `FakeSource` for the tests.

## Threads

A worker `std::thread` (MTA) owns the listener and runs reads, removes, clears and toast activation;
a WNF callback runs on a system thread. A second WNF callback belongs to `DndWatcher`. Owners and
stops are listed in the threads table of `docs/architecture.md`.

## Depends on

kernel only. `toast_activation_win.cpp` still takes `activateAppByAumid` from the media feature's
header until the kernel app-activation helper replaces it.
