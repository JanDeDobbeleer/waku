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
#include <vector>

namespace waku::bluetooth {

// True if an audio endpoint of the device is active, false if all such endpoints are unplugged,
// nullopt if none matches. An endpoint belongs to the device when its name contains `deviceName`
// or, with a non-zero `addressInt`, when one of its connectors leads to a Bluetooth device id that
// holds that address. The address matters: the name a row shows ("AirPods Pro - Find My", or
// "Bluetooth 90:62:...") is often not the name of the audio endpoints ("Headphones (AirPods Pro)").
std::optional<bool> audioIsConnected(IMMDeviceEnumerator* enumerator, const QString& deviceName,
                                     quint64 addressInt = 0);

// The ids of the device's audio endpoints (the rule of audioIsConnected). Finding them walks the
// topology of every endpoint, which takes a while: resolve once per request, then read the state of
// the ids with audioEndpointsConnected, which is cheap.
std::vector<QString> audioEndpointIds(IMMDeviceEnumerator* enumerator, const QString& deviceName,
                                      quint64 addressInt);

// True if one of the endpoints `ids` is active, false if all are unplugged, nullopt if `ids` is
// empty or none can be read.
std::optional<bool> audioEndpointsConnected(IMMDeviceEnumerator* enumerator,
                                            const std::vector<QString>& ids);

// Sends the one-shot reconnect (`connect`) or disconnect KS property to every Bluetooth KS filter
// of the device and waits until the endpoints follow. Returns an error reason ("not_audio_device",
// "no_ks_controls", "oneshot_failed", "cancelled", "timeout"), empty on success.
QString setAudioConnection(IMMDeviceEnumerator* enumerator, quint64 addressInt,
                           const QString& deviceName, bool connect, std::atomic<bool>& cancel,
                           const std::vector<QString>& endpointIds);

// The message the menu shows for a failed `action` ("connect" or "disconnect") and `reason`.
QString connectError(const QString& action, const QString& reason);

}  // namespace waku::bluetooth
