#include "features/tiling/tiling_command.h"

namespace waku::tiling {
namespace {

QString usage() {
  return QStringLiteral(
      "error: usage: tile toggle | float | layout <name> | retile | status | "
      "focus next|previous|left|right|up|down | move left|right|up|down | promote | "
      "resize horizontal|vertical increase|decrease | flip horizontal|vertical | monocle | pause | "
      "close | minimize");
}

std::optional<Direction> parseDirection(const QString& word) {
  const QString lower = word.toLower();
  if (lower == QLatin1String("left")) return Direction::Left;
  if (lower == QLatin1String("right")) return Direction::Right;
  if (lower == QLatin1String("up")) return Direction::Up;
  if (lower == QLatin1String("down")) return Direction::Down;
  return std::nullopt;
}

std::optional<Axis> parseAxis(const QString& word) {
  const QString lower = word.toLower();
  if (lower == QLatin1String("horizontal")) return Axis::Horizontal;
  if (lower == QLatin1String("vertical")) return Axis::Vertical;
  return std::nullopt;
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
    if (const std::optional<Direction> direction = parseDirection(which)) {
      return tiling->controlFocusDirection(*direction);
    }
  }
  if (verb == QLatin1String("move") && count == 2) {
    if (const std::optional<Direction> direction = parseDirection(words.at(1))) {
      return tiling->controlMove(*direction);
    }
  }
  if (verb == QLatin1String("promote") && count == 1) return tiling->controlPromote();
  if (verb == QLatin1String("resize") && count == 3) {
    const std::optional<Axis> axis = parseAxis(words.at(1));
    const QString how = words.at(2).toLower();
    if (axis && how == QLatin1String("increase")) return tiling->controlResize(*axis, 1);
    if (axis && how == QLatin1String("decrease")) return tiling->controlResize(*axis, -1);
  }
  if (verb == QLatin1String("flip") && count == 2) {
    if (const std::optional<Axis> axis = parseAxis(words.at(1))) return tiling->controlFlip(*axis);
  }
  if (verb == QLatin1String("monocle") && count == 1) return tiling->controlMonocle();
  if (verb == QLatin1String("pause") && count == 1) return tiling->controlPause();
  if (verb == QLatin1String("close") && count == 1) return tiling->controlClose();
  if (verb == QLatin1String("minimize") && count == 1) return tiling->controlMinimize();
  return usage();
}

}  // namespace waku::tiling
