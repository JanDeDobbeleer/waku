// clang-format off
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <winrt/base.h>
// clang-format on

#include "platform/app_volume.h"

#include <QDebug>
#include <algorithm>
#include <iterator>
#include <vector>

#include "platform/media_session.h"

namespace yasb::platform {
namespace {

using winrt::com_ptr;

class SystemAppVolumeSession final : public AppVolumeSession {
 public:
  explicit SystemAppVolumeSession(com_ptr<ISimpleAudioVolume> volume)
      : volume_(std::move(volume)) {}

  bool volume(float* out) override { return SUCCEEDED(volume_->GetMasterVolume(out)); }
  bool setVolume(float level) override {
    return SUCCEEDED(volume_->SetMasterVolume(level, nullptr));
  }
  bool muted(bool* out) override {
    BOOL value = FALSE;
    if (FAILED(volume_->GetMute(&value))) return false;
    *out = value != FALSE;
    return true;
  }
  bool setMuted(bool muted) override { return SUCCEEDED(volume_->SetMute(muted, nullptr)); }

 private:
  com_ptr<ISimpleAudioVolume> volume_;
};

struct SessionInfo {
  com_ptr<ISimpleAudioVolume> volume;
  unsigned long pid = 0;
};

QString exeBaseName(unsigned long pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return {};
  wchar_t buffer[MAX_PATH * 2]{};
  DWORD size = static_cast<DWORD>(std::size(buffer));
  QString name;
  if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
    name = QString::fromWCharArray(buffer, static_cast<qsizetype>(size));
    const qsizetype slash = std::max(name.lastIndexOf(u'\\'), name.lastIndexOf(u'/'));
    if (slash >= 0) name = name.mid(slash + 1);
  }
  CloseHandle(process);
  return name;
}

std::vector<SessionInfo> enumerateSessions() {
  std::vector<SessionInfo> result;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // already initialised is fine
  com_ptr<IMMDeviceEnumerator> enumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(enumerator.put()))))
    return result;
  com_ptr<IMMDevice> device;
  if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put())))
    return result;
  com_ptr<IAudioSessionManager2> manager;
  if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                              manager.put_void())))
    return result;
  com_ptr<IAudioSessionEnumerator> sessions;
  if (FAILED(manager->GetSessionEnumerator(sessions.put()))) return result;
  int count = 0;
  if (FAILED(sessions->GetCount(&count))) return result;
  for (int i = 0; i < count; ++i) {
    com_ptr<IAudioSessionControl> control;
    if (FAILED(sessions->GetSession(i, control.put()))) continue;
    auto control2 = control.try_as<IAudioSessionControl2>();
    auto volume = control.try_as<ISimpleAudioVolume>();
    if (!control2 || !volume) continue;
    DWORD pid = 0;
    if (FAILED(control2->GetProcessId(&pid)) || pid == 0) continue;  // system sounds session
    result.push_back({std::move(volume), pid});
  }
  return result;
}

class SystemAppVolumeSource final : public AppVolumeSource {
 public:
  std::unique_ptr<AppVolumeSession> bind(const QString& aumid) override {
    if (aumid.isEmpty()) return nullptr;
    try {
      std::vector<SessionInfo> sessions = enumerateSessions();
      for (SessionInfo& session : sessions) {
        const QString sessionAumid = aumidForPid(session.pid);
        if (!sessionAumid.isEmpty() && sessionAumid.compare(aumid, Qt::CaseInsensitive) == 0)
          return std::make_unique<SystemAppVolumeSession>(std::move(session.volume));
      }
      const QString exe = exeNameForAumid(aumid);
      if (exe.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        for (SessionInfo& session : sessions) {
          if (exeBaseName(session.pid).compare(exe, Qt::CaseInsensitive) == 0)
            return std::make_unique<SystemAppVolumeSession>(std::move(session.volume));
        }
      }
    } catch (const winrt::hresult_error& error) {
      qWarning().noquote() << "Failed to bind app volume session:"
                           << QString::fromWCharArray(error.message().c_str());
    }
    return nullptr;
  }
};

}  // namespace

std::shared_ptr<AppVolumeSource> createSystemAppVolumeSource() {
  return std::make_shared<SystemAppVolumeSource>();
}

}  // namespace yasb::platform
