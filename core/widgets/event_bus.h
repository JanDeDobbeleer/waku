#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace yasb::widgets {

// Process-wide signal hub (replaces Python's EventService). Widgets connect in their constructor;
// emitters live in U9 (hotkeys, CLI). Signals are delivered on the GUI thread.
class EventBus : public QObject {
  Q_OBJECT
 public:
  static EventBus& instance() {
    static EventBus bus;
    return bus;
  }

 signals:
  // Hotkey routed to the widget whose config name is `widgetName` on `screen`; `action` is a callback string.
  void widgetHotkey(const QString& widgetName, const QString& action, const QString& screen);
  // Generic named events (e.g. "komorebi_update", "bar_cli") with a payload.
  void event(const QString& name, const QVariantMap& payload);

 private:
  EventBus() = default;
};

}  // namespace yasb::widgets
