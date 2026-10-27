#pragma once

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QWidget>
#include <vector>

#include "callbacks.h"
#include "label_format.h"

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

  const QString& widgetName() const { return m_widgetName; }
  // screen_name / bar_id come from the properties BarWindow sets when it adds the widget.
  QString screenName() const;
  QString barId() const;
  void setScreenName(const QString& name) { setProperty("screen_name", name); }
  void setHotkeyEnabled(bool enabled) { m_hotkeyEnabled = enabled; }

  CallbackRegistry& callbacks() { return m_callbacks; }
  void registerCallback(const QString& name, CallbackFn fn, int arity = 0) {
    m_callbacks.add(name, std::move(fn), arity);
  }
  void setCallbackBinding(const CallbackBinding& binding) { m_binding = binding; }
  bool runCallback(const QString& callbackStr) { return m_callbacks.run(callbackStr); }

  // Runs the timer callback once now, then every interval (when > 0).
  void startTimer();
  void stopTimer() { m_timer.stop(); }

  // Rebuilds the primary and alt label sets from label templates. `hideIcons` hides icon parts.
  // Alt widgets start hidden; their text is the raw template until the subclass renders it.
  void buildWidgetLabel(const QString& content, const QString& contentAlt = QString(),
                        const QString& placeholder = QString(), bool hideIcons = false);
  const std::vector<QLabel*>& labels() const { return m_labels; }
  const std::vector<QLabel*>& labelsAlt() const { return m_labelsAlt; }
  bool showAlt() const { return m_showAlt; }
  // Swaps which label set is visible (toggle_label).
  void toggleAlt();

  // Sets a label's "class" property and repolishes only when it changed.
  static void setLabelClass(QLabel* label, const QString& cssClass);
  void clearHoverState();

  QFrame* widgetFrame() const { return m_frame; }
  QFrame* labelContainer() const { return m_container; }

 public slots:
  void handleHotkey(const QString& widgetName, const QString& action, const QString& screen);

 protected:
  void mouseReleaseEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  void applyAltVisibility();

  QString m_widgetName;
  QHBoxLayout* m_widgetLayout = nullptr;
  QFrame* m_frame = nullptr;
  QFrame* m_container = nullptr;
  QHBoxLayout* m_containerLayout = nullptr;
  std::vector<QLabel*> m_labels;
  std::vector<QLabel*> m_labelsAlt;
  std::vector<bool> m_isIcon;
  bool m_showAlt = false;
  bool m_hotkeyEnabled = true;
  bool m_hideIcons = false;
  CallbackRegistry m_callbacks;
  CallbackBinding m_binding;
  QTimer m_timer;
  int m_timerInterval;
};

}  // namespace yasb::widgets
