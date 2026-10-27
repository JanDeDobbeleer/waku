#pragma once

#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QPainterPath>
#include <QPointF>
#include <QPushButton>
#include <QString>
#include <optional>
#include <vector>

#include "core/bar/theme_class.h"
#include "core/widgets/popup.h"

namespace yasb::widgets {

// Rolling area chart points for 0-100 history; x spans [0, width], y keeps `stroke/2` padding.
std::vector<QPointF> graphPoints(const std::vector<double>& data, int width, int height,
                                 double stroke = 2.0);

// Catmull-Rom style spline with y control points clamped between neighbours
// (GraphWidget._build_smooth_path).
QPainterPath buildSmoothPath(const std::vector<QPointF>& pts, double tension = 0.2);

class GraphWidget : public QFrame {
  Q_OBJECT

 public:
  explicit GraphWidget(const QString& cssClass = QStringLiteral("graph"), bool showGrid = false,
                       QWidget* parent = nullptr);

  void setData(const std::vector<double>& values);
  bool hasPaths() const { return !fill_.isEmpty(); }

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  void rebuildPaths();

  std::vector<double> data_;
  QPainterPath line_;
  QPainterPath fill_;
  bool showGrid_;
  QFrame* gridProxy_;
};

// Popup that stays open while pinned and can be dragged then; blur/round corners are applied on
// show.
class PinnablePopup : public PopupWidget {
  Q_OBJECT

 public:
  PinnablePopup(QWidget* anchor, bar::GlobalState& globalState, bool blur, bool roundCorners,
                const QString& roundCornersType, const QString& borderColor);

  bool isPinned() const { return pinned_; }
  void setPinned(bool pinned);
  GraphWidget* graph = nullptr;
  QHash<QString, QLabel*> statLabels;
  QPushButton* pinButton = nullptr;

 protected:
  void showEvent(QShowEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  bool pinned_ = false;
  bool blur_;
  bool roundCorners_;
  QString roundCornersType_;
  QString borderColor_;
  bar::GlobalState& globalState_;
  std::optional<QPoint> dragOffset_;
};

struct StatPopupMenuConfig {
  bool blur = true;
  bool roundCorners = true;
  QString roundCornersType = QStringLiteral("normal");
  QString borderColor = QStringLiteral("System");
  QString alignment = QStringLiteral("right");
  QString direction = QStringLiteral("down");
  int offsetLeft = 0;
  int offsetTop = 6;
  bool showGraph = true;
  bool showGraphGrid = true;
  QString pinIcon;
  QString unpinIcon;
};

// (left header or null, left key, left value, right header or null, right key, right value)
struct StatRow {
  std::optional<QString> leftHeader;
  QString leftKey;
  QString leftValue;
  std::optional<QString> rightHeader;
  QString rightKey;
  QString rightValue;
};

PinnablePopup* buildStatPopup(QWidget* anchor, bar::GlobalState& globalState,
                              const StatPopupMenuConfig& menu, const QString& popupClass,
                              const QString& title, const std::vector<double>& history,
                              const std::vector<StatRow>& rows,
                              const QString& graphClass = QStringLiteral("graph"));

}  // namespace yasb::widgets
