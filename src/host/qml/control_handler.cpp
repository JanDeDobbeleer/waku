#include "host/qml/control_handler.h"

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

#include "host/qml/bar_host.h"

namespace wabar {

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

ControlHandler makeControlHandler(BarHost& host, bool safe, std::function<void()> quit,
                                  WallpaperControlProvider wallpapers) {
  if (!quit) quit = [] { QCoreApplication::quit(); };
  QPointer<BarHost> guard(&host);
  return [guard, safe, quit = std::move(quit),
          wallpapers = std::move(wallpapers)](const QString& command) -> QString {
    if (!guard) return QStringLiteral("error: the bar is shutting down");
    if (command == QLatin1String("reload")) {
      return guard->reload() ? QStringLiteral("ok") : QStringLiteral("error: ") + guard->error();
    }
    if (command == QLatin1String("status")) {
      const QString error = guard->error();
      return QStringLiteral("running; bars=%1; safe=%2; error=%3")
          .arg(guard->windows().size())
          .arg(safe ? QStringLiteral("yes") : QStringLiteral("no"))
          .arg(error.isEmpty() ? QStringLiteral("none") : error);
    }
    if (command == QLatin1String("quit")) {
      // After the answer has been written.
      QTimer::singleShot(100, [quit] { quit(); });
      return QStringLiteral("ok");
    }
    if (command == QLatin1String("wallpaper") || command.startsWith(QLatin1String("wallpaper "))) {
      QStringList words;
      if (!splitCommand(command, &words)) return QStringLiteral("error: unterminated quote");
      words.removeFirst();
      return answerWallpaperCommand(words, wallpapers ? wallpapers() : nullptr);
    }
    return QStringLiteral("error: unknown command '%1' (try: reload, status, quit, wallpaper)")
        .arg(command.left(40));
  };
}

}  // namespace wabar
