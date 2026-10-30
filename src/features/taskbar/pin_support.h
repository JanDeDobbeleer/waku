#pragma once

#include <QString>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "features/taskbar/window_system.h"

namespace wabar::taskbar {

// What taskbar_pinned.json stores per pinned app besides its id (PinManager metadata).
struct PinMetadata {
  QString title;
  QString shortcutPath;
};

// PinManager._parse_unique_id: "path:C:\a.exe|args" -> {path, C:\a.exe, args}; an id without a
// colon is {unknown, id, ""}. The args split off at the first "|".
struct UniqueIdParts {
  QString type;
  QString value;
  QString args;
};
UniqueIdParts parseUniqueId(const QString& uniqueId);

// PinManager._tool_file: the .msc or .cpl file an mmc.exe or rundll32.exe tool opens, relative
// names resolved against `system32`. Empty when the arguments name no existing tool file.
QString toolFileFromArguments(const QString& arguments, const QString& system32);

// KnownCLSID.RECYCLE_BIN, compared in upper case.
bool isRecycleBinId(const QString& uniqueId);

struct RecycleBinInfo {
  qint64 sizeBytes = 0;
  qint64 numItems = 0;
};

struct QuickAccessFolder {
  QString name;
  QString path;
};

struct ShortcutInfo {
  QString target;
  QString arguments;
  QString workingDirectory;
  QString iconFile;
  int iconIndex = 0;
};
// IShellLink read of a .lnk; nullopt when it cannot be loaded.
std::optional<ShortcutInfo> readShortcut(const QString& path);

// Shell and launch operations behind the taskbar's pinned apps, abstracted so tests never touch the
// start menu, the registry or the recycle bin. The Win32 implementation is stateless for the
// icon calls (safe on the icon worker thread); resolveMetadata and launch run on the GUI thread.
class PinSupport {
 public:
  virtual ~PinSupport() = default;

  // Title and start-menu shortcut for a window about to be pinned (PinManager._resolve_pin_metadata
  // without the command line hints).
  virtual PinMetadata resolveMetadata(const WindowInfo& info, const QString& uniqueId) const = 0;
  // A pinned app's icon: a Store app's shell image, else the shortcut it launches, else its exe,
  // tool file or folder. Null when nothing was found.
  virtual IconBitmap pinnedIcon(const QString& uniqueId, const PinMetadata& metadata,
                                int size) const = 0;
  // The file named by HKCU/HKLM Software\Classes\AppUserModelId\<aumid>\IconUri (toast icon); a
  // path only, the widget loads and pads the image.
  virtual std::optional<QString> toastIconPath(const QString& aumid) const = 0;
  // The generic application icon (IDI_APPLICATION).
  virtual IconBitmap defaultIcon() const = 0;
  // SIID_RECYCLER / SIID_RECYCLERFULL.
  virtual IconBitmap recycleBinIcon(bool empty, int size) const = 0;
  // PinManager.launch_pinned_app.
  virtual void launch(const QString& uniqueId, const PinMetadata& metadata,
                      const QString& extraArguments) = 0;
  // SHQueryRecycleBinW over all bins.
  virtual RecycleBinInfo recycleBinInfo() const = 0;
  // SHEmptyRecycleBinW with its confirmation and progress UI; blocks, so call it off the GUI
  // thread.
  virtual void emptyRecycleBin() = 0;
  // Folders pinned to Quick Access (at most ten), as get_explorer_pinned_folders.
  virtual std::vector<QuickAccessFolder> quickAccessFolders() const = 0;
};

std::shared_ptr<PinSupport> makeWin32PinSupport();

// The bitmap helpers are shared with the apps feature and live in kernel/win32/shell_icons.h.
using kernel::iconBitmapFromHicon;
using kernel::shellItemIcon;
IconBitmap defaultAppIcon();

}  // namespace wabar::taskbar
