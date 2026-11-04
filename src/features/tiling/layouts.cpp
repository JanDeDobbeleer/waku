#include "features/tiling/layouts.h"

#include <algorithm>

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
                               double ratio) {
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
  for (const auto& [y, h] : spans(inner.y(), inner.height(), count - 1, gap)) {
    rects.emplace_back(stackX, y, stackWidth, h);
  }
  return rects;
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
