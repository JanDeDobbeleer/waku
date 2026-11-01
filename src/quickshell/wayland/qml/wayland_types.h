#pragma once

// The `Quickshell.Wayland` module's types: the layer-shell attached properties and enums of
// host/qml/ registered under Quickshell's names. No code: each declaration only carries the
// registration (QML_FOREIGN).

#include <QtQml/qqmlregistration.h>

#include <QObject>

#include "features/desktop_state/qml/idle_inhibitor.h"
#include "host/qml/window_enums.h"
#include "host/qml/wlr_layershell.h"

namespace wabar::quickshell {

struct QsWlrLayershell {
  Q_GADGET
  QML_FOREIGN(wabar::host::WlrLayershell)
  QML_NAMED_ELEMENT(WlrLayershell)
  QML_UNCREATABLE("WlrLayershell is an attached property of PanelWindow")
};

struct QsIdleInhibitor {
  Q_GADGET
  QML_FOREIGN(wabar::desktop_state::IdleInhibitor)
  QML_NAMED_ELEMENT(IdleInhibitor)
};

namespace QsWlrLayer {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(wabar::host::WlrLayer)
QML_NAMED_ELEMENT(WlrLayer)
}  // namespace QsWlrLayer

namespace QsWlrKeyboardFocus {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(wabar::host::WlrKeyboardFocus)
QML_NAMED_ELEMENT(WlrKeyboardFocus)
}  // namespace QsWlrKeyboardFocus

}  // namespace wabar::quickshell
