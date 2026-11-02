#pragma once

// The `Quickshell` module's types: waku's own (host/qml/), registered again under Quickshell's
// names. No code: each struct only carries the registration (QML_FOREIGN).

#include <QtQml/qqmlregistration.h>

#include <QObject>

#include "host/qml/floating_window.h"
#include "host/qml/global_shortcut.h"
#include "host/qml/ipc_handler.h"
#include "host/qml/lazy_loader.h"
#include "host/qml/panel_window.h"
#include "host/qml/persistent_properties.h"
#include "host/qml/qs_window.h"
#include "host/qml/region.h"
#include "host/qml/scope.h"
#include "host/qml/script_model.h"
#include "host/qml/shell_screen.h"
#include "host/qml/system_clock.h"
#include "host/qml/transform_watcher.h"
#include "host/qml/window_enums.h"

namespace waku::quickshell {

struct QsShellRoot {
  Q_GADGET
  QML_FOREIGN(waku::host::ShellRoot)
  QML_NAMED_ELEMENT(ShellRoot)
};

struct QsScope {
  Q_GADGET
  QML_FOREIGN(waku::host::Scope)
  QML_NAMED_ELEMENT(Scope)
};

struct QsSingleton {
  Q_GADGET
  QML_FOREIGN(waku::host::Singleton)
  QML_NAMED_ELEMENT(Singleton)
};

struct QsTransformWatcher {
  Q_GADGET
  QML_FOREIGN(waku::host::TransformWatcher)
  QML_NAMED_ELEMENT(TransformWatcher)
};

struct QsQsWindow {
  Q_GADGET
  QML_FOREIGN(waku::host::QsWindow)
  QML_NAMED_ELEMENT(QsWindow)
  QML_UNCREATABLE("QsWindow is the base of PanelWindow and FloatingWindow")
};

struct QsPanelWindow {
  Q_GADGET
  QML_FOREIGN(waku::host::PanelWindow)
  QML_NAMED_ELEMENT(PanelWindow)
};

struct QsFloatingWindow {
  Q_GADGET
  QML_FOREIGN(waku::host::FloatingWindow)
  QML_NAMED_ELEMENT(FloatingWindow)
};

struct QsLazyLoader {
  Q_GADGET
  QML_FOREIGN(waku::host::LazyLoader)
  QML_NAMED_ELEMENT(LazyLoader)
};

struct QsGlobalShortcut {
  Q_GADGET
  QML_FOREIGN(waku::host::GlobalShortcut)
  QML_NAMED_ELEMENT(GlobalShortcut)
};

struct QsIpcHandler {
  Q_GADGET
  QML_FOREIGN(waku::host::IpcHandler)
  QML_NAMED_ELEMENT(IpcHandler)
};

struct QsPersistentProperties {
  Q_GADGET
  QML_FOREIGN(waku::host::PersistentProperties)
  QML_NAMED_ELEMENT(PersistentProperties)
};

struct QsScriptModel {
  Q_GADGET
  QML_FOREIGN(waku::host::ScriptModel)
  QML_NAMED_ELEMENT(ScriptModel)
};

struct QsSystemClock {
  Q_GADGET
  QML_FOREIGN(waku::host::SystemClock)
  QML_NAMED_ELEMENT(SystemClock)
};

struct QsShellScreen {
  Q_GADGET
  QML_FOREIGN(waku::host::ShellScreen)
  QML_NAMED_ELEMENT(ShellScreen)
  QML_UNCREATABLE("ShellScreen comes from the screen list")
};

namespace QsExclusionMode {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(waku::host::ExclusionMode)
QML_NAMED_ELEMENT(ExclusionMode)
}  // namespace QsExclusionMode

namespace QsEdges {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(waku::host::Edges)
QML_NAMED_ELEMENT(Edges)
}  // namespace QsEdges

struct QsRegion {
  Q_GADGET
  QML_FOREIGN(waku::host::Region)
  QML_NAMED_ELEMENT(Region)
};

namespace QsIntersection {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(waku::host::Intersection)
QML_NAMED_ELEMENT(Intersection)
}  // namespace QsIntersection

namespace QsRegionShape {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(waku::host::RegionShape)
QML_NAMED_ELEMENT(RegionShape)
}  // namespace QsRegionShape

}  // namespace waku::quickshell
