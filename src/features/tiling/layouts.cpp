#include "features/tiling/layouts.h"

#include <algorithm>
#include <cmath>

namespace waku::tiling {
namespace {

const QString kMainStack = QStringLiteral("mainStack");
const QString kMainStackTop = QStringLiteral("mainStackTop");
const QString kColumns = QStringLiteral("columns");
const QString kRows = QStringLiteral("rows");
const QString kBsp = QStringLiteral("bsp");
const QString kGrid = QStringLiteral("grid");
const QString kCenterMain = QStringLiteral("centerMain");

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

// Splits [start, start + length) into a first span of `fraction` of the room left by `gap` and a
// second span that takes the rest. Needs length >= gap + 2; the caller checks.
std::pair<std::pair<int, int>, std::pair<int, int>> splitSpan(int start, int length, int gap,
                                                              double fraction) {
  const int usable = length - gap;
  const int first = std::clamp(static_cast<int>(usable * fraction + 0.5), 1, usable - 1);
  const int secondAt = start + first + gap;
  return {{start, first}, {secondAt, std::max(1, start + length - secondAt)}};
}

// Binary space partition: each window but the last takes the first part of what is left and the
// rest is split again, across then down alternately. The first split uses `ratio`, the others half.
// When `strict` and a part is too small to cut, the result is empty (the caller tries a smaller
// gap).
std::vector<QRect> bspSlots(const QRect& inner, int count, int gap, double ratio, bool strict) {
  std::vector<QRect> rects;
  QRect rest = inner;
  for (int i = 0; i < count - 1; ++i) {
    bool vertical = i % 2 == 0;  // a vertical cut makes a left and a right part
    const double fraction = i == 0 ? ratio : 0.5;
    if (vertical && rest.width() < gap + 2) vertical = false;
    if (!vertical && rest.height() < gap + 2) vertical = rest.width() >= gap + 2;
    if (vertical) {
      const auto [a, b] = splitSpan(rest.x(), rest.width(), gap, fraction);
      rects.emplace_back(a.first, rest.y(), a.second, rest.height());
      rest = QRect(b.first, rest.y(), b.second, rest.height());
    } else if (rest.height() >= gap + 2) {
      const auto [a, b] = splitSpan(rest.y(), rest.height(), gap, fraction);
      rects.emplace_back(rest.x(), a.first, rest.width(), a.second);
      rest = QRect(rest.x(), b.first, rest.width(), b.second);
    } else {
      if (strict) return {};
      rects.push_back(rest);  // no room left to cut: the remaining windows share this slot
    }
  }
  rects.push_back(rest);
  return rects;
}

// A near-square grid: ceil(sqrt(n)) columns; the last row spreads its windows over the full width.
std::vector<QRect> gridSlots(const QRect& inner, int count, int gap) {
  std::vector<QRect> rects;
  const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
  const int rowCount = (count + columns - 1) / columns;
  int placed = 0;
  for (const auto& [y, h] : spans(inner.y(), inner.height(), rowCount, gap)) {
    const int inRow = std::min(columns, count - placed);
    for (const auto& [x, w] : spans(inner.x(), inner.width(), inRow, gap)) {
      rects.emplace_back(x, y, w, h);
    }
    placed += inRow;
  }
  return rects;
}

// A main window in the centre (`ratio` of the width) and the others in a column on each side. The
// first stacked window goes right, the next left, and so on.
std::vector<QRect> centerMainSlots(const QRect& inner, int count, int gap, double ratio) {
  const int others = count - 1;
  const int rightCount = (others + 1) / 2;
  const int leftCount = others / 2;
  const int usable = std::max(3, inner.width() - 2 * gap);
  const int mainWidth = std::clamp(static_cast<int>(usable * ratio + 0.5), 1, usable - 2);
  const int leftWidth = std::max(1, (usable - mainWidth) / 2);
  const int mainX = inner.x() + leftWidth + gap;
  const int rightX = mainX + mainWidth + gap;
  const int rightWidth = std::max(1, inner.x() + inner.width() - rightX);
  std::vector<QRect> rects;
  rects.emplace_back(mainX, inner.y(), mainWidth, inner.height());
  const auto right = spans(inner.y(), inner.height(), rightCount, gap);
  const auto left = spans(inner.y(), inner.height(), leftCount, gap);
  for (int i = 0; i < others; ++i) {
    if (i % 2 == 0) {
      rects.emplace_back(rightX, right[static_cast<size_t>(i / 2)].first, rightWidth,
                         right[static_cast<size_t>(i / 2)].second);
    } else {
      rects.emplace_back(inner.x(), left[static_cast<size_t>(i / 2)].first, leftWidth,
                         left[static_cast<size_t>(i / 2)].second);
    }
  }
  return rects;
}

}  // namespace

QStringList layoutNames() {
  return {kMainStack, kMainStackTop, kColumns, kRows, kBsp, kGrid, kCenterMain};
}

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

  const QString name = canonicalLayout(layout);
  if (name == kColumns) {
    for (const auto& [x, w] : spans(inner.x(), inner.width(), count, gap)) {
      rects.emplace_back(x, inner.y(), w, inner.height());
    }
    return rects;
  }
  if (name == kRows) {
    for (const auto& [y, h] : spans(inner.y(), inner.height(), count, gap)) {
      rects.emplace_back(inner.x(), y, inner.width(), h);
    }
    return rects;
  }

  if (count == 1) {
    rects.push_back(inner);
    return rects;
  }
  if (name == kBsp) {
    // Deep cuts shrink the parts: halve the gap until every part can be cut.
    for (int g = gap;; g /= 2) {
      const QRect box(area.x() + g, area.y() + g, area.width() - 2 * g, area.height() - 2 * g);
      std::vector<QRect> parts = bspSlots(box, count, g, ratio, g > 0);
      if (!parts.empty()) return parts;
    }
  }
  if (name == kGrid) return gridSlots(inner, count, gap);
  if (name == kCenterMain && count > 2) return centerMainSlots(inner, count, gap, ratio);
  if (name == kMainStackTop) {
    const int usableHeight = std::max(2, inner.height() - gap);
    const int mainHeight =
        std::clamp(static_cast<int>(usableHeight * ratio + 0.5), 1, usableHeight - 1);
    rects.emplace_back(inner.x(), inner.y(), inner.width(), mainHeight);
    const int rowY = inner.y() + mainHeight + gap;
    const int rowHeight = std::max(1, inner.y() + inner.height() - rowY);
    for (const auto& [x, w] : spans(inner.x(), inner.width(), count - 1, gap)) {
      rects.emplace_back(x, rowY, w, rowHeight);
    }
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
