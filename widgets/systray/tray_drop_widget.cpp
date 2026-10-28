#include "widgets/systray/tray_drop_widget.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QStyle>
#include <algorithm>

#include "widgets/systray/tray_icon_button.h"

namespace yasb::widgets {
namespace {

void repolish(QWidget* widget) {
  if (QStyle* style = widget->style()) {
    style->unpolish(widget);
    style->polish(widget);
  }
}

}  // namespace

TrayDropWidget::TrayDropWidget(QWidget* parent, int gridCols)
    : QFrame(parent), gridCols_(gridCols) {
  setAcceptDrops(true);
  if (gridCols > 0) {
    grid_ = new QGridLayout(this);
  } else {
    row_ = new QHBoxLayout(this);
    setMaximumHeight(32);
  }
  QLayout* layout = mainLayout();
  layout->setSpacing(0);
  layout->setContentsMargins(0, 0, 0, 0);
}

void TrayDropWidget::setClassToken(QWidget* widget, const QString& name, bool add) {
  QStringList classes =
      widget->property("class").toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (add) {
    if (!classes.contains(name)) classes.append(name);
  } else {
    classes.removeAll(name);
  }
  widget->setProperty("class", classes.join(QLatin1Char(' ')));
  repolish(widget);
}

void TrayDropWidget::dragEnterEvent(QDragEnterEvent* event) {
  auto* source = qobject_cast<TrayIconButton*>(event->source());
  if (!source) return;
  draggedButton_ = source;
  setClassToken(source, QStringLiteral("dragging"), true);
  event->acceptProposedAction();
  emit dragStarted(source->width());
}

void TrayDropWidget::dragMoveEvent(QDragMoveEvent* event) {
  if (!qobject_cast<TrayIconButton*>(event->source())) return;
  TrayIconButton* hovered = iconAt(event->position().toPoint());
  if (hovered && hovered != draggedButton_)
    highlightHover(hovered);
  else
    clearHoverHighlight();
  event->acceptProposedAction();
  emit dragStarted(draggedButton_ ? draggedButton_->width() : 0);
}

void TrayDropWidget::dragLeaveEvent(QDragLeaveEvent*) {
  clearHoverHighlight();
  if (draggedButton_) {
    setClassToken(draggedButton_, QStringLiteral("dragging"), false);
    draggedButton_ = nullptr;
  }
}

void TrayDropWidget::dropEvent(QDropEvent* event) {
  auto* source = qobject_cast<TrayIconButton*>(event->source());
  if (!source) return;
  if (dropIcon(source, event->position().toPoint())) event->acceptProposedAction();
}

bool TrayDropWidget::dropIcon(TrayIconButton* source, const QPoint& position) {
  setClassToken(source, QStringLiteral("dragging"), false);
  clearHoverHighlight();
  return gridCols_ > 0 ? dropGrid(source, position) : dropInline(source, position);
}

bool TrayDropWidget::dropInline(TrayIconButton* source, const QPoint& position) {
  if (!row_) return false;
  int index = insertIndex(position);

  int currentIndex = -1;
  for (int i = 0; i < row_->count(); ++i) {
    if (row_->itemAt(i) && row_->itemAt(i)->widget() == source) {
      currentIndex = i;
      break;
    }
  }

  if (source->parent() == this) {
    if (index == currentIndex || index == currentIndex + 1) {
      emit dragEnded();
      return true;
    }
    if (currentIndex != -1 && index > currentIndex) --index;
    row_->removeWidget(source);
  } else {
    source->setParent(this);
  }

  row_->insertWidget(index, source);
  source->show();
  draggedButton_ = nullptr;
  emit dragEnded();
  emit source->iconMoved(source);
  return true;
}

bool TrayDropWidget::dropGrid(TrayIconButton* source, const QPoint& position) {
  std::vector<TrayIconButton*> icons = allIcons();
  TrayIconButton* hovered = iconAt(position);

  // After the target's centre: the drop lands behind it.
  const auto insertAt = [&](TrayIconButton* target) {
    const auto found = std::find(icons.begin(), icons.end(), target);
    int index = static_cast<int>(found - icons.begin());
    if (position.x() >= target->geometry().center().x()) ++index;
    return index;
  };
  const auto contains = [&](TrayIconButton* icon) {
    return std::find(icons.begin(), icons.end(), icon) != icons.end();
  };

  if (contains(source)) {
    // Reorder within the grid.
    if (hovered && hovered != source && contains(hovered)) {
      icons.erase(std::find(icons.begin(), icons.end(), source));
      // Clamp after the removal shifted the indices.
      const int index = std::min(insertAt(hovered), static_cast<int>(icons.size()));
      icons.insert(icons.begin() + index, source);
    }
  } else {
    // Dropped in from another container.
    source->setParent(this);
    source->show();
    if (hovered && contains(hovered))
      icons.insert(icons.begin() + insertAt(hovered), source);
    else
      icons.push_back(source);
  }

  relayoutGrid(&icons);
  draggedButton_ = nullptr;
  emit dragEnded();
  emit source->iconMoved(source);
  return true;
}

int TrayDropWidget::insertIndex(const QPoint& dropPosition) const {
  QLayout* layout = mainLayout();
  if (layout->count() == 0) return 0;
  for (int i = 0; i < layout->count(); ++i) {
    QLayoutItem* item = layout->itemAt(i);
    QWidget* widget = item ? item->widget() : nullptr;
    if (!widget) continue;
    if (dropPosition.x() < widget->geometry().center().x()) return i;
  }
  return layout->count();
}

void TrayDropWidget::addIconToGrid(TrayIconButton* icon) {
  std::vector<TrayIconButton*> icons = allIcons();
  if (std::find(icons.begin(), icons.end(), icon) == icons.end()) icons.push_back(icon);
  relayoutGrid(&icons);
}

void TrayDropWidget::relayoutGrid(const std::vector<TrayIconButton*>* iconList) {
  if (!grid_) return;
  const std::vector<TrayIconButton*> icons = iconList ? *iconList : allIcons();
  for (TrayIconButton* icon : icons) grid_->removeWidget(icon);
  for (int r = 0; r < grid_->rowCount(); ++r) {
    grid_->setRowMinimumHeight(r, 0);
    grid_->setRowStretch(r, 0);
  }
  for (int c = 0; c < grid_->columnCount(); ++c) {
    grid_->setColumnMinimumWidth(c, 0);
    grid_->setColumnStretch(c, 0);
  }
  const int cols = gridCols_;
  int visibleIndex = 0;
  for (TrayIconButton* icon : icons) {
    // Hidden icons stay in the layout so they are not orphaned; sitting on the current visible
    // cell does not disturb the order of the visible ones.
    if (icon->isHidden()) {
      grid_->addWidget(icon, visibleIndex / cols, visibleIndex % cols);
      grid_->setAlignment(icon, Qt::AlignCenter);
      continue;
    }
    grid_->addWidget(icon, visibleIndex / cols, visibleIndex % cols);
    grid_->setAlignment(icon, Qt::AlignCenter);
    icon->show();
    ++visibleIndex;
  }
  grid_->invalidate();
}

std::vector<TrayIconButton*> TrayDropWidget::allIcons() const {
  std::vector<TrayIconButton*> icons;
  QLayout* layout = mainLayout();
  for (int i = 0; i < layout->count(); ++i) {
    QLayoutItem* item = layout->itemAt(i);
    if (auto* icon = item ? qobject_cast<TrayIconButton*>(item->widget()) : nullptr)
      icons.push_back(icon);
  }
  return icons;
}

TrayIconButton* TrayDropWidget::iconAt(const QPoint& position) const {
  for (QWidget* widget = childAt(position); widget; widget = widget->parentWidget()) {
    if (auto* icon = qobject_cast<TrayIconButton*>(widget)) return icon;
  }
  return nullptr;
}

void TrayDropWidget::highlightHover(TrayIconButton* button) {
  if (hoverButton_ == button) return;
  clearHoverHighlight();
  hoverButton_ = button;
  setClassToken(button, QStringLiteral("drag-over"), true);
}

void TrayDropWidget::clearHoverHighlight() {
  if (hoverButton_) {
    setClassToken(hoverButton_, QStringLiteral("drag-over"), false);
    hoverButton_ = nullptr;
  }
}

void TrayDropWidget::setDropTargetStyle(bool active) {
  setClassToken(this, QStringLiteral("drop-target"), active);
}

void TrayDropWidget::refreshStyles() {
  if (draggedButton_) repolish(draggedButton_);
}

}  // namespace yasb::widgets
