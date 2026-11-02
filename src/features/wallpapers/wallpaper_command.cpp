#include "features/wallpapers/wallpaper_command.h"

namespace waku::wallpapers {

QString answerWallpaperCommand(const QStringList& words, WallpaperControl* wallpapers) {
  static const QString usage = QStringLiteral(
      "error: usage: wallpaper set [--dry-run] <file-or-path> | random [--dry-run] | next "
      "[--dry-run] | previous [--dry-run] | gallery");
  if (words.isEmpty()) return usage;
  const QString verb = words.first();
  bool dryRun = false;
  QStringList arguments;
  for (qsizetype i = 1; i < words.size(); ++i) {
    if (words.at(i) == QLatin1String("--dry-run")) {
      dryRun = true;
    } else {
      arguments << words.at(i);
    }
  }
  const bool known = verb == QLatin1String("set") || verb == QLatin1String("random") ||
                     verb == QLatin1String("next") || verb == QLatin1String("previous") ||
                     verb == QLatin1String("gallery");
  if (!known) {
    return QStringLiteral("error: unknown wallpaper command '%1'").arg(verb.left(40)) +
           QStringLiteral(" (try: set, random, next, previous, gallery)");
  }
  if (verb == QLatin1String("set") ? arguments.size() != 1 : !arguments.isEmpty()) return usage;
  if (verb == QLatin1String("gallery") && dryRun) return usage;
  if (!wallpapers) return QStringLiteral("error: the wallpaper service is not available");
  if (verb == QLatin1String("set")) return wallpapers->controlSet(arguments.first(), dryRun);
  if (verb == QLatin1String("random")) return wallpapers->controlRandom(dryRun);
  if (verb == QLatin1String("next")) return wallpapers->controlNext(dryRun);
  if (verb == QLatin1String("previous")) return wallpapers->controlPrevious(dryRun);
  return wallpapers->controlGallery();
}

}  // namespace waku::wallpapers
