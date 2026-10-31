#include <windows.h>

#include <QString>
#include <algorithm>
#include <cwctype>

#include "features/system_stats/system_stats.h"
#include "kernel/qt/linger.h"

namespace wabar::system_stats {
namespace {

// Documented Win32 only: GetLogicalDrives, GetDriveTypeW, GetDiskFreeSpaceExW,
// GetVolumeInformationW, GetWindowsDirectoryW.

class WindowsDiskDataSource final : public DiskDataSource {
 public:
  std::vector<DiskSample> sample() override {
    std::vector<DiskSample> result;
    // An empty card reader must not raise the "no disk in drive" dialog.
    DWORD oldMode = 0;
    const bool modeSet = SetThreadErrorMode(SEM_FAILCRITICALERRORS, &oldMode) != FALSE;
    const wchar_t systemLetter = systemDriveLetter();
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
      if (!(mask & (1u << i))) continue;
      const wchar_t letter = static_cast<wchar_t>(L'A' + i);
      const wchar_t root[] = {letter, L':', L'\\', L'\0'};
      const UINT type = GetDriveTypeW(root);
      // Network drives are left out: an unreachable share would stall the sample.
      if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_CDROM) continue;
      ULARGE_INTEGER available{};
      ULARGE_INTEGER total{};
      ULARGE_INTEGER free{};
      if (!GetDiskFreeSpaceExW(root, &available, &total, &free) || total.QuadPart == 0) continue;
      DiskSample disk;
      disk.mount = QString::fromWCharArray(root).toStdString();
      wchar_t label[MAX_PATH + 1] = {};
      wchar_t fileSystem[MAX_PATH + 1] = {};
      if (GetVolumeInformationW(root, label, MAX_PATH, nullptr, nullptr, nullptr, fileSystem,
                                MAX_PATH)) {
        disk.name = QString::fromWCharArray(label).toStdString();
        disk.fileSystem = QString::fromWCharArray(fileSystem).toStdString();
      }
      disk.total = total.QuadPart;
      disk.free = free.QuadPart;
      disk.used = disk.total - std::min(disk.total, disk.free);
      disk.percent = 100.0 * static_cast<double>(disk.used) / static_cast<double>(disk.total);
      disk.system = letter == systemLetter;
      disk.removable = type == DRIVE_REMOVABLE || type == DRIVE_CDROM;
      result.push_back(std::move(disk));
    }
    if (modeSet) SetThreadErrorMode(oldMode, nullptr);
    return result;
  }

 private:
  static wchar_t systemDriveLetter() {
    wchar_t path[MAX_PATH] = {};
    if (GetWindowsDirectoryW(path, MAX_PATH) < 2 || path[1] != L':') return L'C';
    return static_cast<wchar_t>(towupper(path[0]));
  }
};

}  // namespace

// One source for every widget of every engine, kept through a reload (kernel::LingeringHub).
std::shared_ptr<DiskDataSource> createDiskDataSource() {
  static kernel::LingeringHub<DiskDataSource> hub;
  return hub.acquire([] { return std::make_shared<WindowsDiskDataSource>(); });
}

}  // namespace wabar::system_stats
