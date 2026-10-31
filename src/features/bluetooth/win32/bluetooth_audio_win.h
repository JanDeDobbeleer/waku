#pragma once

// The Bluetooth audio path of the backend (win32/bluetooth_audio_win.cpp): whether a device's
// audio endpoints are active, and the reconnect / disconnect request. Backend thread only.
// clang-format off
#include <windows.h>
#include <mmdeviceapi.h>
// clang-format on

#include <QString>
#include <atomic>
#include <optional>

namespace wabar::bluetooth {

// True if an audio endpoint whose name contains `deviceName` is active, false if all such
// endpoints are unplugged, nullopt if none matches.
std::optional<bool> audioIsConnected(IMMDeviceEnumerator* enumerator, const QString& deviceName);

// Sends the one-shot reconnect (`connect`) or disconnect KS property to every Bluetooth KS filter
// of the device and waits until the endpoints follow. Returns an error reason ("not_audio_device",
// "no_ks_controls", "oneshot_failed", "cancelled", "timeout"), empty on success.
QString setAudioConnection(IMMDeviceEnumerator* enumerator, quint64 addressInt,
                           const QString& deviceName, bool connect, std::atomic<bool>& cancel);

// The message the menu shows for a failed `action` ("connect" or "disconnect") and `reason`.
QString connectError(const QString& action, const QString& reason);

}  // namespace wabar::bluetooth
