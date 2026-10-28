#include "platform/sound.h"

#include <windows.h>
#include <mmsystem.h>

#include <filesystem>

namespace yasb::platform {

void playSound(const std::wstring& wavPath, bool loop) {
  std::error_code ec;
  const bool haveFile = !wavPath.empty() && std::filesystem::exists(wavPath, ec);
  DWORD flags = SND_ASYNC | (loop ? SND_LOOP : 0);
  if (haveFile) {
    PlaySoundW(wavPath.c_str(), nullptr, flags | SND_FILENAME);
  } else {
    PlaySoundW(L"SystemNotification", nullptr, flags | SND_ALIAS);
  }
}

void stopSound() { PlaySoundW(nullptr, nullptr, SND_PURGE); }

}  // namespace yasb::platform
