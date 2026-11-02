#include "host/thumbnail.h"

#include <algorithm>
#include <cmath>

namespace waku::host {

QRect thumbnailDestination(const QRectF& sceneRect, qreal devicePixelRatio, const QSize& source,
                           bool preserveAspect) {
  const qreal scale = devicePixelRatio > 0 ? devicePixelRatio : 1.0;
  const int left = qRound(sceneRect.left() * scale);
  const int top = qRound(sceneRect.top() * scale);
  const int width = qRound(sceneRect.right() * scale) - left;
  const int height = qRound(sceneRect.bottom() * scale) - top;
  if (width <= 0 || height <= 0) return QRect(left, top, 0, 0);
  if (!preserveAspect || source.isEmpty()) return QRect(left, top, width, height);
  const double factor = std::min(static_cast<double>(width) / source.width(),
                                 static_cast<double>(height) / source.height());
  const int fittedWidth =
      std::clamp(static_cast<int>(std::lround(source.width() * factor)), 1, width);
  const int fittedHeight =
      std::clamp(static_cast<int>(std::lround(source.height() * factor)), 1, height);
  return QRect(left + (width - fittedWidth) / 2, top + (height - fittedHeight) / 2, fittedWidth,
               fittedHeight);
}

}  // namespace waku::host
