#pragma once

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QWidget>
#include <vector>

#include "core/widgets/callbacks.h"
#include "core/widgets/label_format.h"

namespace yasb::widgets {

// Callback strings for the three mouse buttons and the update timer (config "callbacks" block).
struct CallbackBinding {
  QString left = QStringLiteral("do_nothing");
  QString middle = QStringLiteral("do_nothing");
  QString right = QStringLiteral("do_nothing");
  QString timer = QStringLiteral("do_nothing");
};

// Port of core/widgets/base.py. Subclasses (U8) register callbacks, build labels and call
// startTimer().
class BaseWidget : public QWidget {
  Q_OBJECT
 public:
  explicit BaseWidget(const QString& widgetName, const QString& className = QString(),
                      int timerIntervalMs = 0, QWidget* parent = nullptr);

  const QString& widgetName() const { return widgetName_; }
  // screen_name / bar_id come from the properties BarWindow sets when it adds the widget.
  QString screenName() const;
  QString barId() const;
  void setScreenName(const QString& name) { setProperty("screen_name", name); }
  void setHotkeyEnabled(bool enabled) { hotkeyEnabled_ = enabled; }

  CallbackRegistry& callbacks() { return callbacks_; }
  void registerCallback(const QString& name, CallbackFn fn, int arity = 0) {
    callbacks_.add(name, std::move(fn), arity);
  }
  void setCallbackBinding(const CallbackBinding& binding) { binding_ = binding; }
  bool runCallback(const QString& callbackStr) { return callbacks_.run(callbackStr); }

  // Runs the timer callback once now, then every interval (when > 0).
  void startTimer();
  void stopTimer() { timer_.stop(); }

  // Rebuilds the primary and alt label sets from label templates. `hideIcons` hides icon parts.
  // Alt widgets start hidden; their text is the raw template until the subclass renders it.
  void buildWidgetLabel(const QString& content, const QString& contentAlt = QString(),
                        const QString& placeholder = QString(), bool hideIcons = false);
  const std::vector<QLabel*>& labels() const { return labels_; }
  const std::vector<QLabel*>& labelsAlt() const { return labelsAlt_; }
  bool showAlt() const { return showAlt_; }
  // Swaps which label set is visible (toggle_label).
  void toggleAlt();

  // Sets a label's "class" property and repolishes only when it changed.
  static void setLabelClass(QLabel* label, const QString& cssClass);
  void clearHoverState();

  QFrame* widgetFrame() const { return frame_; }
  QFrame* labelContainer() const { return container_; }

 public slots:
  void handleHotkey(const QString& widgetName, const QString& action, const QString& screen);

 protected:
  void mouseReleaseEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  void applyAltVisibility();

  QString widgetName_;
  QHBoxLayout* widgetLayout_ = nullptr;
  QFrame* frame_ = nullptr;
  QFrame* container_ = nullptr;
  QHBoxLayout* containerLayout_ = nullptr;
  std::vector<QLabel*> labels_;
  std::vector<QLabel*> labelsAlt_;
  std::vector<bool> isIcon_;
  bool showAlt_ = false;
  bool hotkeyEnabled_ = true;
  bool hideIcons_ = false;
  CallbackRegistry callbacks_;
  CallbackBinding binding_;
  QTimer timer_;
  int timerInterval_;
};

}  // namespace yasb::widgets
