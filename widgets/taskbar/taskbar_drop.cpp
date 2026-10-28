#include "widgets/taskbar/taskbar_drop.h"

#include <QBoxLayout>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFrame>
#include <QMimeData>
#include <QStyle>

#include "widgets/taskbar/taskbar_widget.h"

namespace yasb::widgets {

TaskbarDropHandler::TaskbarDropHandler(TaskbarWidget* owner, QFrame* container)
    : QObject(container), owner_(owner), container_(container) {
  container_->setAcceptDrops(true);
  container_->installEventFilter(this);
  // External drag hover-to-raise support.
  hoverTimer_.setSingleShot(true);
  hoverTimer_.setInterval(200);
  connect(&hoverTimer_, &QTimer::timeout, this, [this] { onHoverTimeout(); });
}

QBoxLayout* TaskbarDropHandler::layout() const {
  return qobject_cast<QBoxLayout*>(container_->layout());
}

void TaskbarDropHandler::onHoverTimeout() {
  try {
    if (hoverTarget_ != 0) owner_->ensureForeground(hoverTarget_);
  } catch (...) {
  }
}

bool TaskbarDropHandler::eventFilter(QObject* watched, QEvent* event) {
  if (watched != container_) return false;
  try {
    switch (event->type()) {
      case QEvent::DragEnter: {
        auto* drag = static_cast<QDragEnterEvent*>(event);
        if (auto* source = qobject_cast<AppButton*>(drag->source())) {
          if (dragEnterInternal(source))
            drag->acceptProposedAction();
          else
            drag->ignore();
          return true;
        }
        // External drag (files or text).
        const QMimeData* mime = drag->mimeData();
        if (mime && (mime->hasUrls() || mime->hasText())) {
          dragHoverExternal(drag->position().toPoint());
          drag->setDropAction(Qt::IgnoreAction);
          drag->accept();
        } else {
          drag->ignore();
        }
        return true;
      }
      case QEvent::DragMove: {
        auto* drag = static_cast<QDragMoveEvent*>(event);
        if (auto* source = qobject_cast<AppButton*>(drag->source())) {
          if (dragMoveInternal(source, drag->position().toPoint()))
            drag->acceptProposedAction();
          else
            drag->ignore();
          return true;
        }
        const QMimeData* mime = drag->mimeData();
        if (mime && (mime->hasUrls() || mime->hasText())) {
          dragHoverExternal(drag->position().toPoint());
          drag->setDropAction(Qt::IgnoreAction);
          drag->accept();
        } else {
          drag->ignore();
        }
        return true;
      }
      case QEvent::DragLeave:
        dragLeave();
        event->accept();
        return true;
      case QEvent::Drop: {
        auto* drop = static_cast<QDropEvent*>(event);
        auto* source = qobject_cast<AppButton*>(drop->source());
        if (!source) {
          dropExternal();
          drop->ignore();
          return true;
        }
        if (dropInternal(source, drop->position().toPoint()))
          drop->acceptProposedAction();
        else
          drop->ignore();
        return true;
      }
      default:
        break;
    }
  } catch (...) {
    // Exceptions never cross into Qt.
  }
  return false;
}

bool TaskbarDropHandler::dragEnterInternal(AppButton* source) {
  // Only this container's own buttons reorder.
  if (source->parentWidget() != container_) return false;
  dragged_ = source;
  dragged_->setProperty("dragging", true);
  refreshStyles();
  emit dragStarted();
  return true;
}

bool TaskbarDropHandler::dragMoveInternal(AppButton* source, const QPoint& pos) {
  if (source->parentWidget() != container_) return false;
  AppButton* hovered = buttonAt(pos);
  if (hovered && hovered != dragged_) {
    highlight(hovered);
    indicatorIndex_ = indexFromHover(hovered, source);
  } else {
    clearHighlight();
    indicatorIndex_ = -1;
  }
  return true;
}

void TaskbarDropHandler::dragHoverExternal(const QPoint& pos) {
  AppButton* button = buttonAt(pos);
  const platform::WindowHandle hwnd = button ? button->hwnd() : 0;
  if (!button || hwnd == 0) {
    hoverTimer_.stop();
    hoverTarget_ = 0;
    return;
  }
  hoverTarget_ = hwnd;
  hoverTimer_.start(200);
}

void TaskbarDropHandler::dropExternal() {
  hoverTimer_.stop();
  if (hoverTarget_ != 0) {
    try {
      owner_->ensureForeground(hoverTarget_);
    } catch (...) {
    }
  }
  hoverTarget_ = 0;
}

void TaskbarDropHandler::dragLeave() {
  indicatorIndex_ = -1;
  clearHighlight();
  hoverTimer_.stop();
  hoverTarget_ = 0;
  if (dragged_) {
    dragged_->setProperty("dragging", false);
    refreshStyles();
  }
  dragged_ = nullptr;
}

bool TaskbarDropHandler::dropInternal(AppButton* source, const QPoint& pos) {
  // Ignore drops from other taskbar instances to avoid duplicate handling.
  if (source->parentWidget() != container_) {
    emit dragEnded();
    return false;
  }

  // Finalize the reorder.
  clearHighlight();
  source->setProperty("dragging", false);
  refreshStyles();
  // Prefer the index tracked during the move, else compute it from the hovered button, else from
  // the drop position.
  int insertIndex = indicatorIndex_;
  if (insertIndex < 0) {
    AppButton* hovered = buttonAt(pos);
    insertIndex =
        hovered && hovered != source ? indexFromHover(hovered, source) : insertIndexAt(pos);
  }

  const int currentIndex = indexOf(source);
  // Adjust the index after removal when moving forward in the list.
  int adjusted = insertIndex;
  if (currentIndex != -1 && adjusted > currentIndex) --adjusted;
  // The same slot after the adjustment is a no-op.
  if (adjusted == currentIndex) {
    indicatorIndex_ = -1;
    emit dragEnded();
    return true;
  }

  layout()->removeWidget(source);
  layout()->insertWidget(adjusted, source);
  source->show();
  refreshStyles();
  dragged_ = nullptr;
  indicatorIndex_ = -1;

  // Update the pinned order if pinned apps were reordered.
  try {
    owner_->updatePinnedOrderFromLayout();
  } catch (...) {
  }
  emit dragEnded();
  return true;
}

void TaskbarDropHandler::highlight(AppButton* button) {
  if (highlighted_ == button) return;
  clearHighlight();
  highlighted_ = button;
  highlightedPreviousStyle_ = button->styleSheet();
  const QString name = button->objectName();
  if (!name.isEmpty()) {
    button->setStyleSheet(
        QStringLiteral("#%1 { background-color: rgba(255, 255, 255, 0.2); }").arg(name));
  }
  button->update();
}

void TaskbarDropHandler::clearHighlight() {
  AppButton* button = highlighted_;
  if (!button) return;
  button->setStyleSheet(highlightedPreviousStyle_);
  highlightedPreviousStyle_.clear();
  button->update();
  button->style()->unpolish(button);
  button->style()->polish(button);
  highlighted_ = nullptr;
}

AppButton* TaskbarDropHandler::buttonAt(const QPoint& pos) const {
  QWidget* w = container_->childAt(pos);
  while (w && !qobject_cast<AppButton*>(w)) w = w->parentWidget();
  return qobject_cast<AppButton*>(w);
}

int TaskbarDropHandler::indexOf(const QWidget* widget) const {
  QBoxLayout* box = layout();
  for (int i = 0; box && i < box->count(); ++i) {
    if (box->itemAt(i)->widget() == widget) return i;
  }
  return -1;
}

int TaskbarDropHandler::insertIndexAt(const QPoint& pos) const {
  QBoxLayout* box = layout();
  const int count = box ? box->count() : 0;
  for (int i = 0; i < count; ++i) {
    const QWidget* w = box->itemAt(i)->widget();
    if (w && pos.x() < w->geometry().center().x()) return i;
  }
  return count;
}

// Dropping anywhere on a hovered button moves: from its left the item goes after it, from its
// right before it.
int TaskbarDropHandler::indexFromHover(const QWidget* hovered, const QWidget* source) const {
  const int hoveredIndex = indexOf(hovered);
  const int sourceIndex = indexOf(source);
  if (hoveredIndex < 0) return 0;
  if (sourceIndex < 0) return hoveredIndex + 1;
  return sourceIndex < hoveredIndex ? hoveredIndex + 1 : hoveredIndex;
}

void TaskbarDropHandler::refreshStyles() {
  for (QWidget* widget :
       {static_cast<QWidget*>(container_), static_cast<QWidget*>(dragged_.data())}) {
    if (!widget) continue;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
  }
}

}  // namespace yasb::widgets
