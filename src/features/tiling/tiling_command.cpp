#include "features/tiling/tiling_command.h"

namespace waku::tiling {
namespace {

QString usage() {
  return QStringLiteral(
      "error: usage: tile toggle | float | layout <name> | retile | focus next|previous | status");
}

}  // namespace

QString answerTileCommand(const QStringList& words, TilingControl* tiling) {
  if (words.isEmpty()) return usage();
  if (!tiling) return QStringLiteral("error: the tiler is not available");
  const QString verb = words.first().toLower();
  const qsizetype count = words.size();
  if (verb == QLatin1String("toggle") && count == 1) return tiling->controlFloat(std::nullopt);
  if (verb == QLatin1String("float") && count == 1) return tiling->controlFloat(true);
  if (verb == QLatin1String("retile") && count == 1) return tiling->controlRetile();
  if (verb == QLatin1String("status") && count == 1) return tiling->controlStatus();
  if (verb == QLatin1String("layout") && count == 2) return tiling->controlLayout(words.at(1));
  if (verb == QLatin1String("focus") && count == 2) {
    const QString which = words.at(1).toLower();
    if (which == QLatin1String("next")) return tiling->controlFocus(1);
    if (which == QLatin1String("previous")) return tiling->controlFocus(-1);
  }
  return usage();
}

}  // namespace waku::tiling
