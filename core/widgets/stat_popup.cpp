#include "core/widgets/stat_popup.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <algorithm>

#include "core/widgets/backdrop.h"
#include "core/widgets/tooltip.h"

namespace yasb::widgets {
namespace {
constexpr double kStroke = 2.0;
constexpr int kGridCell = 16;
}  // namespace

std::vector<QPointF> graphPoints(const std::vector<double>& data, int width, int height,
                                 double stroke) {
  std::vector<QPointF> pts;
  if (data.empty() || width <= 0 || height <= 0) return pts;
  const double pad = stroke / 2;
  const double chartH = height - pad * 2;
  const size_t n = data.size();
  const double step = n > 1 ? static_cast<double>(width) / static_cast<double>(n - 1) : width;
  for (size_t i = 0; i < n; ++i) {
    const double frac = std::max(0.0, std::min(data[i] / 100.0, 1.0));
    pts.emplace_back(static_cast<double>(i) * step, pad + chartH * (1.0 - frac));
  }
  return pts;
}

QPainterPath buildSmoothPath(const std::vector<QPointF>& pts, double tension) {
  QPainterPath path;
  path.moveTo(pts.front());
  if (tension == 0 || pts.size() == 2) {
    for (size_t i = 1; i < pts.size(); ++i) path.lineTo(pts[i]);
    return path;
  }
  const size_t last = pts.size() - 1;
  for (size_t i = 0; i < last; ++i) {
    const QPointF& p0 = pts[i == 0 ? 0 : i - 1];
    const QPointF& p1 = pts[i];
    const QPointF& p2 = pts[i + 1];
    const QPointF& p3 = pts[std::min(last, i + 2)];
    const double cp1x = p1.x() + (p2.x() - p0.x()) * tension;
    double cp1y = p1.y() + (p2.y() - p0.y()) * tension;
    const double cp2x = p2.x() - (p3.x() - p1.x()) * tension;
    double cp2y = p2.y() - (p3.y() - p1.y()) * tension;
    const double lo = std::min(p1.y(), p2.y());
    const double hi = std::max(p1.y(), p2.y());
    cp1y = std::max(lo, std::min(cp1y, hi));
    cp2y = std::max(lo, std::min(cp2y, hi));
    path.cubicTo(cp1x, cp1y, cp2x, cp2y, p2.x(), p2.y());
  }
  return path;
}

GraphWidget::GraphWidget(const QString& cssClass, bool showGrid, QWidget* parent)
    : QFrame(parent), showGrid_(showGrid) {
  setMinimumHeight(20);
  setProperty("class", cssClass);
  setAttribute(Qt::WA_TranslucentBackground);
  gridProxy_ = new QFrame(this);
  gridProxy_->setProperty("class", cssClass + QStringLiteral("-grid"));
  gridProxy_->setFixedSize(0, 0);
}

void GraphWidget::setData(const std::vector<double>& values) {
  data_ = values;
  rebuildPaths();
  update();
}

void GraphWidget::resizeEvent(QResizeEvent* event) {
  QFrame::resizeEvent(event);
  rebuildPaths();
}

void GraphWidget::rebuildPaths() {
  line_ = QPainterPath();
  fill_ = QPainterPath();
  const auto pts = graphPoints(data_, width(), height(), kStroke);
  if (pts.size() < 2) return;
  line_ = buildSmoothPath(pts);
  fill_.moveTo(pts.front().x(), height());
  fill_.lineTo(pts.front());
  fill_.connectPath(line_);
  fill_.lineTo(pts.back().x(), height());
  fill_.closeSubpath();
}

void GraphWidget::paintEvent(QPaintEvent* event) {
  QFrame::paintEvent(event);
  if (fill_.isEmpty()) return;
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  ensurePolished();
  const QColor line = palette().color(foregroundRole());
  const int w = width();
  const int h = height();

  if (showGrid_) {
    p.setRenderHint(QPainter::Antialiasing, false);
    gridProxy_->ensurePolished();
    QColor grid = gridProxy_->palette().color(gridProxy_->foregroundRole());
    if (!grid.isValid() || grid == QColor(0, 0, 0)) {
      grid = line;
      grid.setAlpha(30);
    }
    p.setPen(QPen(grid, 1));
    int y = h - 1;
    for (; y >= 0; y -= kGridCell) p.drawLine(0, y, w - 1, y);
    if (y + kGridCell > 0) p.drawLine(0, 0, w - 1, 0);
    int x = w - 1;
    for (; x >= 0; x -= kGridCell) p.drawLine(x, 0, x, h - 1);
    if (x + kGridCell > 0) p.drawLine(0, 0, 0, h - 1);
    p.setRenderHint(QPainter::Antialiasing);
  }

  QLinearGradient gradient(0, 0, 0, h);
  QColor top = line;
  top.setAlpha(100);
  QColor bottom = line;
  bottom.setAlpha(10);
  gradient.setColorAt(0, top);
  gradient.setColorAt(1, bottom);
  p.setBrush(gradient);
  p.setPen(Qt::NoPen);
  p.drawPath(fill_);

  QPen pen(line, kStroke);
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::BevelJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  p.drawPath(line_);
}

PinnablePopup::PinnablePopup(QWidget* anchor, bar::GlobalState& globalState, bool blur,
                             bool roundCorners, const QString& roundCornersType,
                             const QString& borderColor)
    : PopupWidget(anchor),
      blur_(blur),
      roundCorners_(roundCorners),
      roundCornersType_(roundCornersType),
      borderColor_(borderColor),
      globalState_(globalState) {
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                 Qt::NoDropShadowWindowHint);
}

void PinnablePopup::setPinned(bool pinned) {
  pinned_ = pinned;
  setAutoClose(!pinned);
  if (!pinned) dragOffset_.reset();
}

void PinnablePopup::showEvent(QShowEvent* event) {
  PopupWidget::showEvent(event);
  if (blur_) {
    enableBlur(winId(), globalState_.isDark(), roundCorners_, roundCornersType_, borderColor_);
  }
}

void PinnablePopup::mousePressEvent(QMouseEvent* event) {
  if (pinned_ && event->button() == Qt::LeftButton) {
    dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
    return;
  }
  PopupWidget::mousePressEvent(event);
}

void PinnablePopup::mouseMoveEvent(QMouseEvent* event) {
  if (pinned_ && dragOffset_ && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - *dragOffset_);
    event->accept();
    return;
  }
  PopupWidget::mouseMoveEvent(event);
}

void PinnablePopup::mouseReleaseEvent(QMouseEvent* event) {
  if (pinned_ && event->button() == Qt::LeftButton) {
    dragOffset_.reset();
    event->accept();
    return;
  }
  PopupWidget::mouseReleaseEvent(event);
}

PinnablePopup* buildStatPopup(QWidget* anchor, bar::GlobalState& globalState,
                              const StatPopupMenuConfig& menu, const QString& popupClass,
                              const QString& title, const std::vector<double>& history,
                              const std::vector<StatRow>& rows, const QString& graphClass) {
  auto* popup = new PinnablePopup(anchor, globalState, menu.blur, menu.roundCorners,
                                  menu.roundCornersType, menu.borderColor);
  popup->setProperty("class", popupClass);
  auto* layout = new QVBoxLayout(popup);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* header = new QFrame;
  header->setProperty("class", "header");
  auto* headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(0);
  auto* titleLabel = new QLabel(title);
  titleLabel->setProperty("class", "text");
  headerLayout->addWidget(titleLabel);
  headerLayout->addStretch();

  auto* pin = new QPushButton(menu.pinIcon);
  pin->setCheckable(true);
  pin->setProperty("class", "pin-btn");
  setTooltip(pin, QStringLiteral("Pin this window"), globalState);
  QObject::connect(
      pin, &QPushButton::toggled, popup, [popup, pin, menu, &globalState](bool checked) {
        pin->setText(checked ? menu.unpinIcon : menu.pinIcon);
        pin->setProperty("class", checked ? "pin-btn pinned" : "pin-btn");
        setTooltip(
            pin, checked ? QStringLiteral("Unpin this window") : QStringLiteral("Pin this window"),
            globalState);
        yasb::bar::refreshWidgetStyle(pin);
        popup->setPinned(checked);
      });
  popup->pinButton = pin;
  headerLayout->addWidget(pin);
  layout->addWidget(header);

  if (menu.showGraph) {
    auto* container = new QFrame;
    container->setProperty("class", "graph-container");
    auto* graphLayout = new QVBoxLayout(container);
    graphLayout->setContentsMargins(0, 0, 0, 0);
    graphLayout->setSpacing(0);
    auto* graph = new GraphWidget(graphClass, menu.showGraphGrid);
    graphLayout->addWidget(graph);
    layout->addWidget(container);
    popup->graph = graph;
    if (!history.empty()) graph->setData(history);
  }

  auto* stats = new QFrame;
  stats->setProperty("class", "stats");
  auto* grid = new QGridLayout(stats);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(0);
  for (size_t i = 0; i < rows.size(); ++i) {
    const StatRow& r = rows[i];
    const std::optional<QString>* headers[2] = {&r.leftHeader, &r.rightHeader};
    const QString* keys[2] = {&r.leftKey, &r.rightKey};
    const QString* values[2] = {&r.leftValue, &r.rightValue};
    for (int col = 0; col < 2; ++col) {
      if (!*headers[col]) continue;
      const Qt::Alignment align = col == 1 ? Qt::AlignRight : Qt::AlignLeft;
      auto* item = new QFrame;
      item->setProperty("class", "stat-item");
      auto* itemLayout = new QVBoxLayout(item);
      itemLayout->setContentsMargins(0, 0, 0, 0);
      itemLayout->setSpacing(0);
      auto* label = new QLabel(**headers[col]);
      label->setProperty("class", "stat-label");
      label->setAlignment(align);
      auto* value = new QLabel(*values[col]);
      value->setProperty("class", "stat-value");
      value->setAlignment(align);
      itemLayout->addWidget(label);
      itemLayout->addWidget(value);
      grid->addWidget(item, static_cast<int>(i), col);
      popup->statLabels.insert(*keys[col], value);
    }
  }
  layout->addWidget(stats);

  popup->adjustSize();
  popup->setPosition(menu.alignment, menu.direction, menu.offsetLeft, menu.offsetTop);
  return popup;
}

}  // namespace yasb::widgets
