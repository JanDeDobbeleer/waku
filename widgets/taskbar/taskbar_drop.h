#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QTimer>

#include "platform/window_system.h"

class QFrame;
class QBoxLayout;

namespace yasb::widgets {

class AppButton;
class TaskbarWidget;

// Port of TaskbarDropWidget: the drop target behind the taskbar's buttons. It is an event filter on
// the widget's container, so the container keeps being a plain QFrame with the same class names.
//   - dragging one of the container's own buttons reorders it (the pinned order follows);
//   - dragging files or text over a button raises that button's window after 200 ms, so a file can
//     be dropped into it (the drop itself is ignored).
// Owned by (and stopped with) its TaskbarWidget; GUI thread only.
class TaskbarDropHandler final : public QObject {
  Q_OBJECT

 public:
  TaskbarDropHandler(TaskbarWidget* owner, QFrame* container);

  // --- reorder (the event filter calls these; tests call them directly)
  bool dragEnterInternal(AppButton* source);
  bool dragMoveInternal(AppButton* source, const QPoint& pos);
  bool dropInternal(AppButton* source, const QPoint& pos);
  // --- external drag hover
  void dragHoverExternal(const QPoint& pos);
  void dropExternal();
  void dragLeave();

  int currentIndicatorIndex() const { return indicatorIndex_; }
  // The window the hover timer would raise (0 when none).
  platform::WindowHandle hoverTarget() const { return hoverTarget_; }
  bool hoverTimerActive() const { return hoverTimer_.isActive(); }

 signals:
  void dragStarted();
  void dragEnded();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  QBoxLayout* layout() const;
  AppButton* buttonAt(const QPoint& pos) const;
  int indexOf(const QWidget* widget) const;
  int insertIndexAt(const QPoint& pos) const;
  int indexFromHover(const QWidget* hovered, const QWidget* source) const;
  void highlight(AppButton* button);
  void clearHighlight();
  void refreshStyles();
  void onHoverTimeout();

  TaskbarWidget* owner_;
  QFrame* container_;
  QPointer<AppButton> dragged_;
  QPointer<AppButton> highlighted_;
  QString highlightedPreviousStyle_;
  int indicatorIndex_ = -1;
  QTimer hoverTimer_;
  platform::WindowHandle hoverTarget_ = 0;
};

}  // namespace yasb::widgets
