#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace wabar::workspaces {

// Runtime-owned signal hub (replaces Python's EventService). Widgets connect in their constructor;
// emitters deliver signals on the GUI thread.
class EventBus : public QObject {
  Q_OBJECT
 public:
  EventBus() = default;

 signals:
  // Hotkey routed to the widget whose config name is `widgetName` on `screen`; `action` is a
  // callback string.
  void widgetHotkey(const QString& widgetName, const QString& action, const QString& screen);
  // Generic named events (e.g. "komorebi_update", "bar_cli") with a payload.
  void event(const QString& name, const QVariantMap& payload);
};

}  // namespace wabar::workspaces
