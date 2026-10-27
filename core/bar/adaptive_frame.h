#pragma once

#include <QColor>
#include <QFrame>
#include <QPainterPath>
#include <QRect>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace yasb::bar {

using IslandSpans = std::vector<std::pair<int, int>>;

// Bar surface for style "bar" (port of bar_style.BarFrame).
class BarFrame : public QFrame {
  Q_OBJECT

 public:
  explicit BarFrame(QWidget* parent = nullptr);

  // The area of the frame that takes mouse clicks.
  virtual QRect clickRect() const;

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void resizeClickFloor();
  // Windows passes clicks through fully transparent pixels; this alpha-1 layer takes them.
  QFrame* clickFloor_;
};

// Bar surface for style "adaptive" (port of bar_style.AdaptiveBarFrame): a rail along the outer
// edge, each widget group its own island. The -qproperty-* names are the CSS contract.
class AdaptiveBarFrame : public BarFrame {
  Q_OBJECT
  Q_PROPERTY(int railheight READ railHeight WRITE setRailHeight)
  Q_PROPERTY(int islandradius READ islandRadius WRITE setIslandRadius)
  Q_PROPERTY(int grouppadding READ groupPadding WRITE setGroupPadding)
  Q_PROPERTY(bool islands READ islandsEnabled WRITE setIslandsEnabled)
  Q_PROPERTY(int edgeradius READ edgeRadius WRITE setEdgeRadius)
  Q_PROPERTY(int borderwidth READ borderWidth WRITE setBorderWidth)
  Q_PROPERTY(QColor bordercolor READ borderColor WRITE setBorderColor)

 public:
  // Widgets whose "widget_name" property is in `exclude` never join an island.
  AdaptiveBarFrame(QWidget* parent, std::string position, const std::vector<std::string>& exclude);

  int railHeight() const { return railHeight_; }
  void setRailHeight(int value);
  int islandRadius() const { return islandRadius_; }
  void setIslandRadius(int value);
  int groupPadding() const { return groupPadding_; }
  void setGroupPadding(int value);
  bool islandsEnabled() const { return islandsEnabled_; }
  void setIslandsEnabled(bool value);
  int edgeRadius() const { return edgeRadius_; }
  void setEdgeRadius(int value);
  int borderWidth() const { return borderWidth_; }
  void setBorderWidth(int value);
  QColor borderColor() const { return borderColor_; }
  void setBorderColor(const QColor& value);

  // How far the frame reaches past the bar; zero unless the bar allowed the edge curves.
  int edgeOverhang() const { return edgeCurves_ ? edgeRadius_ : 0; }
  // Told by the bar whether the edge curves apply; returns the height they need.
  int useEdgeCurves(bool allowed);
  QRect clickRect() const override;

  // Invoked after edgeradius changes so the owning window can resize itself.
  void setRepositionHandler(std::function<void()> handler) { reposition_ = std::move(handler); }
  // Installs the layout and keeps the widgets out of the edge-curve strip.
  void setBarLayout(QLayout* layout);

  const IslandSpans& islandSpans() const { return islands_; }
  const QPainterPath& shape() const { return shape_; }
  const QPainterPath& borderPath() const { return borderPath_; }

 protected:
  bool event(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  void applyEdgeRadius();
  void reserveEdgeSpace();
  double frameBottom() const;
  void updateShape(bool force = false);
  QRect changedArea(const IslandSpans& before, const IslandSpans& after) const;
  IslandSpans computeIslandSpans() const;
  void buildShape(const IslandSpans& islands, QPainterPath& shape, QPainterPath& border) const;

  std::string position_;
  std::set<std::string> exclude_;
  int railHeight_ = 4;
  int islandRadius_ = 16;
  int groupPadding_ = 8;
  bool islandsEnabled_ = true;
  int edgeRadius_ = 0;
  bool edgeCurves_ = false;
  int borderWidth_ = 0;
  QColor borderColor_{0, 0, 0, 0};
  IslandSpans islands_;
  QPainterPath shape_;
  QPainterPath gaps_;
  QPainterPath borderPath_;
  std::function<void()> reposition_;
};

}  // namespace yasb::bar
