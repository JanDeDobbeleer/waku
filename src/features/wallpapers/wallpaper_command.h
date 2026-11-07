#pragma once

#include <QString>
#include <QStringList>

namespace waku::wallpapers {

// What the `wallpaper` commands of `wakuc` ask of the wallpaper service. The command parser
// below depends on this interface only (so its tests use a fake); waku::wallpapers::Wallpapers
// implements it. Every function returns the answer line: "ok: <text>" or "error: <text>".
// `dryRun` never calls the wallpaper setter.
class WallpaperControl {
 public:
  virtual ~WallpaperControl() = default;
  // `target` is a full path to an image or a file name inside the wallpaper folder.
  virtual QString controlSet(const QString& target, bool dryRun) = 0;
  virtual QString controlRandom(bool dryRun) = 0;
  virtual QString controlNext(bool dryRun) = 0;
  virtual QString controlPrevious(bool dryRun) = 0;
  // Toggles the picker.
  virtual QString controlGallery() = 0;
};

// The `wallpaper` command of the control pipe, given the words after "wallpaper" (split as
// control::splitCommand does):
//   set [--dry-run] <file-or-path> | random | next | previous [--dry-run] | gallery
// Bad usage answers "error: usage: ..." and calls nothing; a null `wallpapers` answers "error:
// the wallpaper service is not available". The composition root registers it with the bar's
// control handler (host::ControlCommands), so neither `control` nor `host` knows the verb.
QString answerWallpaperCommand(const QStringList& words, WallpaperControl* wallpapers);

}  // namespace waku::wallpapers
