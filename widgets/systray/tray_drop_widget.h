#pragma once

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPoint>
#include <QPointer>
#include <vector>

#include "widgets/systray/tray_icon_button.h"

namespace yasb::widgets {

// Port of DropWidget (services/systray/systray_widget.py): a container for tray icon buttons that
// accepts drops from other TrayIconButtons. With `gridCols` > 0 it lays icons out in a grid (the
// popup), otherwise in a row (the bar). CSS classes "dragging", "drag-over" and "drop-target" are
// toggled on the buttons and on the container while a drag runs.
class TrayDropWidget : public QFrame {
  Q_OBJECT
 public:
  explicit TrayDropWidget(QWidget* parent = nullptr, int gridCols = 0);

  QLayout* mainLayout() const { return row_ ? static_cast<QLayout*>(row_) : grid_; }
  QHBoxLayout* rowLayout() const { return row_; }
  QGridLayout* gridLayout() const { return grid_; }
  int gridCols() const { return gridCols_; }

  // Adds `class` token `name` to a widget's "class" property (or removes it) and repolishes it.
  static void setClassToken(QWidget* widget, const QString& name, bool add);

  // The drop itself (dropEvent without the QDropEvent). Public so tests can drive it.
  // Returns true when the drop was accepted.
  bool dropIcon(TrayIconButton* source, const QPoint& position);

  int insertIndex(const QPoint& dropPosition) const;
  void addIconToGrid(TrayIconButton* icon);
  // Lays the icons out in the grid: all of them when `icons` is null, otherwise in that order.
  void relayoutGrid(const std::vector<TrayIconButton*>* icons = nullptr);
  void setDropTargetStyle(bool active);
  void refreshStyles();
  std::vector<TrayIconButton*> allIcons() const;

 signals:
  void dragStarted(int iconWidth);
  void dragEnded();

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  bool dropInline(TrayIconButton* source, const QPoint& position);
  bool dropGrid(TrayIconButton* source, const QPoint& position);
  TrayIconButton* iconAt(const QPoint& position) const;
  void highlightHover(TrayIconButton* button);
  void clearHoverHighlight();

  int gridCols_;
  QHBoxLayout* row_ = nullptr;
  QGridLayout* grid_ = nullptr;
  QPointer<TrayIconButton> draggedButton_;
  QPointer<TrayIconButton> hoverButton_;
};

}  // namespace yasb::widgets
