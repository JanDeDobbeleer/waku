#include "core/bar/adaptive_frame.h"

#include <QEvent>
#include <QLayout>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QResizeEvent>
#include <QTimer>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <optional>

namespace yasb::bar {

BarFrame::BarFrame(QWidget* parent) : QFrame(parent), clickFloor_(new QFrame(this)) {
  clickFloor_->setAttribute(Qt::WA_TransparentForMouseEvents);
  clickFloor_->setAutoFillBackground(true);
  QPalette palette = clickFloor_->palette();
  palette.setColor(QPalette::Window, QColor(0, 0, 0, 1));
  clickFloor_->setPalette(palette);
}

QRect BarFrame::clickRect() const { return rect(); }

void BarFrame::resizeClickFloor() { clickFloor_->setGeometry(clickRect()); }

void BarFrame::resizeEvent(QResizeEvent* event) {
  QFrame::resizeEvent(event);
  resizeClickFloor();
}

AdaptiveBarFrame::AdaptiveBarFrame(QWidget* parent, std::string position, const std::vector<std::string>& exclude)
    : BarFrame(parent), position_(std::move(position)), exclude_(exclude.begin(), exclude.end()) {}

void AdaptiveBarFrame::setRailHeight(int value) {
  railHeight_ = std::max(0, value);
  updateShape(true);
}

void AdaptiveBarFrame::setIslandRadius(int value) {
  islandRadius_ = std::max(0, value);
  updateShape(true);
}

void AdaptiveBarFrame::setGroupPadding(int value) {
  groupPadding_ = std::max(0, value);
  updateShape(true);
}

void AdaptiveBarFrame::setIslandsEnabled(bool value) {
  if (value == islandsEnabled_) return;
  islandsEnabled_ = value;
  updateShape(true);
}

void AdaptiveBarFrame::setEdgeRadius(int value) {
  value = std::max(0, value);
  if (value == edgeRadius_) return;
  edgeRadius_ = value;
  // A translucent window shows its old picture at the new spot until it repaints, and during a
  // stylesheet reload that is only once every widget is restyled, so move it afterwards.
  QWidget* w = window();
  if (w && w->isVisible()) {
    QTimer::singleShot(0, this, [this] { applyEdgeRadius(); });
  } else {
    applyEdgeRadius();
  }
}

void AdaptiveBarFrame::applyEdgeRadius() {
  reserveEdgeSpace();
  if (reposition_) reposition_();
  updateShape(true);
  QWidget* w = window();
  if (w && w->isVisible()) w->repaint();
}

void AdaptiveBarFrame::setBorderWidth(int value) {
  borderWidth_ = std::max(0, value);
  update();
}

void AdaptiveBarFrame::setBorderColor(const QColor& value) {
  borderColor_ = value;
  update();
}

int AdaptiveBarFrame::useEdgeCurves(bool allowed) {
  allowed = allowed && edgeRadius_ > 0;
  if (allowed != edgeCurves_) {
    edgeCurves_ = allowed;
    reserveEdgeSpace();
    updateShape(true);
  }
  return edgeOverhang();
}

QRect AdaptiveBarFrame::clickRect() const {
  const int overhang = edgeOverhang();
  const int top = position_ == "bottom" ? overhang : 0;
  return QRect(0, top, width(), height() - overhang);
}

void AdaptiveBarFrame::setBarLayout(QLayout* layout) {
  setLayout(layout);
  reserveEdgeSpace();
}

void AdaptiveBarFrame::reserveEdgeSpace() {
  QLayout* l = layout();
  if (!l) return;
  const int overhang = edgeOverhang();
  const int above = position_ == "bottom" ? overhang : 0;
  const int below = position_ == "bottom" ? 0 : overhang;
  const QMargins m = l->contentsMargins();
  if (m.top() != above || m.bottom() != below) l->setContentsMargins(0, above, 0, below);
}

bool AdaptiveBarFrame::event(QEvent* e) {
  // super first, so the layout has run before we measure it
  const bool handled = BarFrame::event(e);
  switch (e->type()) {
    case QEvent::LayoutRequest:
      updateShape();
      break;
    case QEvent::Polish:
    case QEvent::StyleChange:
      updateShape(true);
      break;
    default:
      break;
  }
  return handled;
}

void AdaptiveBarFrame::resizeEvent(QResizeEvent* e) {
  BarFrame::resizeEvent(e);
  updateShape(true);
}

void AdaptiveBarFrame::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setClipPath(gaps_);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.fillRect(QRectF(0.0, 0.0, width(), frameBottom()), Qt::transparent);

  if (borderWidth_ > 0 && borderColor_.alpha() > 0) {
    // Clipped to the shape at twice the width so only the half inside the bar lands
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setClipPath(shape_);
    painter.setPen(QPen(borderColor_, borderWidth_ * 2));
    painter.drawPath(borderPath_);
  }
}

// The frame's bottom, out to a whole device pixel: cutting only part of the last row on
// fractional scaling leaves a line under the bar.
double AdaptiveBarFrame::frameBottom() const {
  if (!edgeOverhang()) return height();
  const double ratio = devicePixelRatioF();
  return ratio > 0 ? std::ceil(height() * ratio) / ratio : height();
}

void AdaptiveBarFrame::updateShape(bool force) {
  IslandSpans islands = computeIslandSpans();
  if (!force && islands == islands_) return;

  const QRect changed = force ? rect() : changedArea(islands_, islands);
  islands_ = std::move(islands);
  buildShape(islands_, shape_, borderPath_);

  QPainterPath frame;
  frame.addRect(QRectF(0.0, 0.0, width(), frameBottom()));
  gaps_ = frame.subtracted(shape_);
  update(changed);
}

// The part of the bar the shape moved in, so a one pixel step does not repaint every child.
QRect AdaptiveBarFrame::changedArea(const IslandSpans& before, const IslandSpans& after) const {
  if (before.size() != after.size()) return rect();
  std::vector<int> edges;
  for (std::size_t i = 0; i < before.size(); ++i) {
    if (before[i] != after[i]) {
      edges.insert(edges.end(), {before[i].first, before[i].second, after[i].first, after[i].second});
    }
  }
  if (edges.empty()) return rect();
  const auto [lo, hi] = std::minmax_element(edges.begin(), edges.end());
  const int margin = islandRadius_ + 2;
  return QRect(*lo - margin, 0, *hi - *lo + 2 * margin, height());
}

// Horizontal spans covered by each widget group, padded, clamped and merged. The containers are
// stretching grid cells, so only their content defines an island; an excluded widget ends the
// run it sits in.
IslandSpans AdaptiveBarFrame::computeIslandSpans() const {
  QLayout* l = layout();
  if (!l) return {};

  const int w = width();
  if (!islandsEnabled_) return w > 0 ? IslandSpans{{0, w}} : IslandSpans{};

  const int padding = groupPadding_;
  IslandSpans spans;
  std::vector<int> skipped;

  for (int index = 0; index < l->count(); ++index) {
    QLayoutItem* item = l->itemAt(index);
    QWidget* container = item ? item->widget() : nullptr;
    if (!container) continue;

    // The container's layout may not have positioned its widgets yet on the first paint
    if (QLayout* inner = container->layout()) inner->activate();

    QList<QWidget*> children = container->findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
    std::stable_sort(children.begin(), children.end(), [](QWidget* a, QWidget* b) { return a->x() < b->x(); });

    std::optional<std::pair<int, int>> run;
    for (QWidget* child : std::as_const(children)) {
      if (child->isHidden() || child->width() <= 0) continue;
      const int start = container->x() + child->x();
      const int end = start + child->width();
      const QString name = child->property("widget_name").toString();
      if (!name.isEmpty() && exclude_.count(name.toStdString())) {
        skipped.push_back(start);
        skipped.push_back(end);
        if (run) {
          spans.emplace_back(run->first - padding, run->second + padding);
          run.reset();
        }
      } else if (run) {
        run->second = end;
      } else {
        run = std::make_pair(start, end);
      }
    }
    if (run) spans.emplace_back(run->first - padding, run->second + padding);
  }

  std::sort(spans.begin(), spans.end());
  IslandSpans merged;
  for (auto [start, end] : spans) {
    start = std::max(0, start);
    end = std::min(w, end);
    if (end <= start) continue;
    if (!merged.empty() && start <= merged.back().second) {
      merged.back().second = std::max(merged.back().second, end);
    } else {
      merged.emplace_back(start, end);
    }
  }

  if (!merged.empty()) {
    // An excluded widget against an edge must not be snapped over
    const int minSkipped = skipped.empty() ? 0 : *std::min_element(skipped.begin(), skipped.end());
    const int maxSkipped = skipped.empty() ? 0 : *std::max_element(skipped.begin(), skipped.end());
    if (merged.front().first <= islandRadius_ && (skipped.empty() || minSkipped >= merged.front().first)) {
      merged.front().first = 0;
    }
    if (merged.back().second >= w - islandRadius_ && (skipped.empty() || maxSkipped <= merged.back().second)) {
      merged.back().second = w;
    }
  }
  return merged;
}

void AdaptiveBarFrame::buildShape(const IslandSpans& islands, QPainterPath& shapeOut, QPainterPath& borderOut) const {
  const double w = width();
  // The frame is taller than the bar by edgeradius; that strip holds the edge curves
  const double bottom = frameBottom();
  const double height = this->height() - edgeOverhang();
  const double overhang = bottom - height;
  const double rail = std::min(static_cast<double>(railHeight_), height);
  const bool flipped = position_ == "bottom";

  QPainterPath fullRect;
  fullRect.addRect(0.0, 0.0, w, height);

  if (w <= 0 || height <= rail) {
    const double inner = flipped ? 0.0 : height;
    QPainterPath path;
    path.moveTo(w, inner);
    path.lineTo(0.0, inner);
    shapeOut = fullRect;
    borderOut = path;
    return;
  }

  // Both curves share the island's side, so together they cannot exceed its height
  const double radius = std::min(static_cast<double>(islandRadius_), std::max(0.0, (height - rail) / 2));

  QPainterPath path;
  const bool reachesRight = !islands.empty() && islands.back().second >= w;
  path.moveTo(w, reachesRight ? height + overhang : rail);

  if (islands.empty()) path.lineTo(0.0, rail);

  const int last = static_cast<int>(islands.size()) - 1;
  for (int index = last; index >= 0; --index) {
    const double x0 = islands[index].first;
    const double x1 = islands[index].second;
    const double corner = std::min(radius, (x1 - x0) / 2);

    if (x1 >= w) {
      if (overhang > 0.0) path.arcTo(QRectF(w - 2 * overhang, height, 2 * overhang, 2 * overhang), 0.0, 90.0);
    } else {
      const double nextStart = index < last ? islands[index + 1].first : w;
      const double fillet = std::min(radius, (nextStart - x1) / 2);
      path.lineTo(x1 + fillet, rail);
      if (fillet > 0.0) path.arcTo(QRectF(x1, rail, 2 * fillet, 2 * fillet), 90.0, 90.0);
      path.lineTo(x1, height - corner);
      if (corner > 0.0) path.arcTo(QRectF(x1 - 2 * corner, height - 2 * corner, 2 * corner, 2 * corner), 0.0, -90.0);
    }

    if (x0 <= 0.0) {
      if (overhang > 0.0) {
        path.lineTo(overhang, height);
        path.arcTo(QRectF(0.0, height, 2 * overhang, 2 * overhang), 90.0, 90.0);
      } else {
        path.lineTo(0.0, height);
      }
    } else {
      path.lineTo(x0 + corner, height);
      if (corner > 0.0) path.arcTo(QRectF(x0, height - 2 * corner, 2 * corner, 2 * corner), 270.0, -90.0);
      const double previousEnd = index > 0 ? islands[index - 1].second : 0.0;
      const double fillet = std::min(radius, (x0 - previousEnd) / 2);
      path.lineTo(x0, rail + fillet);
      if (fillet > 0.0) path.arcTo(QRectF(x0 - 2 * fillet, rail, 2 * fillet, 2 * fillet), 0.0, 90.0);
      if (index == 0) path.lineTo(0.0, rail);
    }
  }

  QPainterPath shape(path);
  shape.lineTo(0.0, 0.0);
  shape.lineTo(w, 0.0);
  shape.closeSubpath();

  if (flipped) {
    const QTransform flip = QTransform().translate(0.0, height + overhang).scale(1.0, -1.0);
    shapeOut = flip.map(shape);
    borderOut = flip.map(path);
  } else {
    shapeOut = shape;
    borderOut = path;
  }
}

}  // namespace yasb::bar
