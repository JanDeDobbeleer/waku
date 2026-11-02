#pragma once

#include <QString>

namespace waku::kernel {

// One policy for "may this run touch the real desktop?". Services whose real constructors read or
// change the machine (Explorer hooks, WinRT listeners, the wallpaper, Bluetooth, ...) ask it
// instead of testing the platform name themselves.
//
// The desktop is not live on the offscreen platform (tests, screenshots) and, per capability, when
// the capability was switched off by a command-line flag (`--no-tray`).
//
// Threading: any thread. The disable flags are set once at startup, before the first service is
// built, and read afterwards.

// True unless the application runs on the offscreen platform. Needs a QGuiApplication; without one
// the answer is true.
bool liveDesktop();

// True when liveDesktop() and `capability` (for example "tray") is not disabled.
bool liveDesktop(const QString& capability);

// Switches one capability off (or back on). Capability names are free-form, lower case.
void setCapabilityDisabled(const QString& capability, bool disabled);

// True when setCapabilityDisabled(capability, true) is in effect, whatever the platform.
bool capabilityDisabled(const QString& capability);

}  // namespace waku::kernel
