#pragma once

// The `Quickshell` module's types: wabar's own (host/qml/), registered again under Quickshell's
// names. No code: each struct only carries the registration (QML_FOREIGN).

#include <QtQml/qqmlregistration.h>

#include <QObject>

#include "host/qml/global_shortcut.h"
#include "host/qml/lazy_loader.h"
#include "host/qml/region.h"
#include "host/qml/shell_config.h"
#include "host/qml/shell_screen.h"
#include "host/qml/shell_window.h"
#include "host/qml/window_enums.h"

namespace wabar::quickshell {

struct QsShellRoot {
  Q_GADGET
  QML_FOREIGN(wabar::host::ShellRoot)
  QML_NAMED_ELEMENT(ShellRoot)
};

struct QsScope {
  Q_GADGET
  QML_FOREIGN(wabar::host::Scope)
  QML_NAMED_ELEMENT(Scope)
};

struct QsQsWindow {
  Q_GADGET
  QML_FOREIGN(wabar::host::QsWindow)
  QML_NAMED_ELEMENT(QsWindow)
  QML_UNCREATABLE("QsWindow is the base of PanelWindow and FloatingWindow")
};

struct QsPanelWindow {
  Q_GADGET
  QML_FOREIGN(wabar::host::PanelWindow)
  QML_NAMED_ELEMENT(PanelWindow)
};

struct QsFloatingWindow {
  Q_GADGET
  QML_FOREIGN(wabar::host::FloatingWindow)
  QML_NAMED_ELEMENT(FloatingWindow)
};

struct QsLazyLoader {
  Q_GADGET
  QML_FOREIGN(wabar::host::LazyLoader)
  QML_NAMED_ELEMENT(LazyLoader)
};

struct QsGlobalShortcut {
  Q_GADGET
  QML_FOREIGN(wabar::host::GlobalShortcut)
  QML_NAMED_ELEMENT(GlobalShortcut)
};

struct QsShellScreen {
  Q_GADGET
  QML_FOREIGN(wabar::host::ShellScreen)
  QML_NAMED_ELEMENT(ShellScreen)
  QML_UNCREATABLE("ShellScreen comes from the screen list")
};

namespace QsExclusionMode {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(wabar::host::ExclusionMode)
QML_NAMED_ELEMENT(ExclusionMode)
}  // namespace QsExclusionMode

namespace QsEdges {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(wabar::host::Edges)
QML_NAMED_ELEMENT(Edges)
}  // namespace QsEdges

struct QsRegion {
  Q_GADGET
  QML_FOREIGN(wabar::host::Region)
  QML_NAMED_ELEMENT(Region)
};

namespace QsIntersection {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(wabar::host::Intersection)
QML_NAMED_ELEMENT(Intersection)
}  // namespace QsIntersection

namespace QsRegionShape {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(wabar::host::RegionShape)
QML_NAMED_ELEMENT(RegionShape)
}  // namespace QsRegionShape

}  // namespace wabar::quickshell
