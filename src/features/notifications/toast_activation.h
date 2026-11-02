#pragma once

#include <QString>

namespace waku::notifications {

// Port of services/notifications/toast_activation.py. All functions block; call them off the GUI
// thread (the notification worker runs them).

// True when a toast `launch` string may be opened as a protocol activation: it starts with a URI
// scheme of at least two letters and is not a file: URI (a path the sender chose, not a link).
bool isOpenableToastUri(const QString& launch);

// Hands a toast to its app. "protocol" opens the launch URI; "foreground" and "background" call the
// app's registered INotificationActivationCallback. False when the app registered nothing that
// takes it.
bool activateToast(const QString& aumid, const QString& launch, const QString& activationType);

// Brings the app's window forward, or starts it. False when the app cannot be found.
bool showApp(const QString& aumid);

// The install folder of a packaged app, empty when it has none.
QString packageInstallFolder(const QString& aumid);

}  // namespace waku::notifications
