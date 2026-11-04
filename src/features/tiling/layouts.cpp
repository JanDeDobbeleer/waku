#include "features/tiling/layouts.h"

#include <algorithm>
#include <cmath>

namespace waku::tiling {
namespace {

const QString kMainStack = QStringLiteral("mainStack");
const QString kColumns = QStringLiteral("columns");

// `count` equal spans over [start, start + length) separated by `gap`; the last absorbs the
// remainder. Each span is at least 1.
std::vector<std::pair<int, int>> spans(int start, int length, int count, int gap) {
  std::vector<std::pair<int, int>> out;
  if (count <= 0) return out;
  const int usable = std::max(count, length - gap * (count - 1));
  const int each = usable / count;
  int at = start;
  for (int i = 0; i < count; ++i) {
    const int size = i == count - 1 ? std::max(1, start + length - at) : each;
    out.emplace_back(at, size);
    at += size + gap;
  }
  return out;
}

// Like spans(), but each span is proportional to its weight (at least 1 pixel).
std::vector<std::pair<int, int>> weightedSpans(int start, int length, int gap,
                                               const std::vector<double>& weights) {
  std::vector<std::pair<int, int>> out;
  const int count = static_cast<int>(weights.size());
  if (count <= 0) return out;
  double total = 0;
  for (const double w : weights) total += std::max(w, 0.01);
  const int usable = std::max(count, length - gap * (count - 1));
  int at = start;
  for (int i = 0; i < count; ++i) {
    const int size =
        i == count - 1 ? std::max(1, start + length - at)
                       : std::max(1, static_cast<int>(usable * std::max(weights[i], 0.01) / total));
    out.emplace_back(at, size);
    at += size + gap;
  }
  return out;
}

}  // namespace

QStringList layoutNames() { return {kMainStack, kColumns}; }

QString canonicalLayout(const QString& name) {
  QString key = name;
  key.remove(QLatin1Char('-')).remove(QLatin1Char('_')).remove(QLatin1Char(' '));
  for (const QString& known : layoutNames()) {
    if (key.compare(known, Qt::CaseInsensitive) == 0) return known;
  }
  return {};
}

std::vector<QRect> layoutSlots(const QString& layout, const QRect& area, int count, int gap,
                               double ratio, const std::vector<double>& weights) {
  std::vector<QRect> rects;
  if (count <= 0 || area.isEmpty()) return rects;
  // A gap the area cannot hold shrinks, so every slot stays positive and inside the area.
  gap = std::clamp(gap, 0, std::min(area.width(), area.height()) / (2 * (count + 1)));
  ratio = std::clamp(ratio, 0.1, 0.9);
  const QRect inner(area.x() + gap, area.y() + gap, area.width() - 2 * gap,
                    area.height() - 2 * gap);

  if (canonicalLayout(layout) == kColumns) {
    for (const auto& [x, w] : spans(inner.x(), inner.width(), count, gap)) {
      rects.emplace_back(x, inner.y(), w, inner.height());
    }
    return rects;
  }

  if (count == 1) {
    rects.push_back(inner);
    return rects;
  }
  const int usableWidth = std::max(2, inner.width() - gap);
  const int mainWidth = std::clamp(static_cast<int>(usableWidth * ratio + 0.5), 1, usableWidth - 1);
  rects.emplace_back(inner.x(), inner.y(), mainWidth, inner.height());
  const int stackX = inner.x() + mainWidth + gap;
  const int stackWidth = std::max(1, inner.x() + inner.width() - stackX);
  std::vector<double> stackWeights(static_cast<size_t>(count - 1), 1.0);
  for (size_t i = 1; i < weights.size() && i < static_cast<size_t>(count); ++i) {
    stackWeights[i - 1] = weights[i];
  }
  const bool even = std::all_of(stackWeights.begin(), stackWeights.end(),
                                [&](double w) { return w == stackWeights.front(); });
  const auto rows = even ? spans(inner.y(), inner.height(), count - 1, gap)
                         : weightedSpans(inner.y(), inner.height(), gap, stackWeights);
  for (const auto& [y, h] : rows) rects.emplace_back(stackX, y, stackWidth, h);
  return rects;
}

std::vector<QRect> flipSlots(std::vector<QRect> rects, const QRect& area, bool horizontal,
                             bool vertical) {
  for (QRect& slot : rects) {
    if (horizontal) slot.moveLeft(area.left() + (area.right() - slot.right()));
    if (vertical) slot.moveTop(area.top() + (area.bottom() - slot.bottom()));
  }
  return rects;
}

std::optional<size_t> neighbourSlot(const std::vector<QRect>& rects, size_t from,
                                    Direction direction) {
  if (from >= rects.size()) return std::nullopt;
  const QPointF origin = QRectF(rects[from]).center();
  std::optional<size_t> best;
  double bestScore = 0;
  for (size_t i = 0; i < rects.size(); ++i) {
    if (i == from) continue;
    const QPointF centre = QRectF(rects[i]).center();
    const double dx = centre.x() - origin.x();
    const double dy = centre.y() - origin.y();
    double along = 0;
    double aside = 0;
    switch (direction) {
      case Direction::Left:
        along = -dx;
        aside = std::abs(dy);
        break;
      case Direction::Right:
        along = dx;
        aside = std::abs(dy);
        break;
      case Direction::Up:
        along = -dy;
        aside = std::abs(dx);
        break;
      case Direction::Down:
        along = dy;
        aside = std::abs(dx);
        break;
    }
    if (along <= 0) continue;
    const double score = along + 2 * aside;
    if (!best || score < bestScore) {
      best = i;
      bestScore = score;
    }
  }
  return best;
}

QRect outerRect(const QRect& slot, const QRect& windowRect, const QRect& visibleRect) {
  const int left = visibleRect.left() - windowRect.left();
  const int top = visibleRect.top() - windowRect.top();
  const int right = (windowRect.x() + windowRect.width()) - (visibleRect.x() + visibleRect.width());
  const int bottom =
      (windowRect.y() + windowRect.height()) - (visibleRect.y() + visibleRect.height());
  return QRect(slot.x() - left, slot.y() - top, slot.width() + left + right,
               slot.height() + top + bottom);
}

}  // namespace waku::tiling
