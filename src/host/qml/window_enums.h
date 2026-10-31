#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>

// The enum types of the shell windows. Names and values follow Quickshell (v0.3) so ported QML
// loads unchanged; the `Quickshell` and `Quickshell.Wayland` modules register the same types again
// (src/quickshell/). `Backdrop` is wabar's own.
namespace wabar::host {

// How a PanelWindow takes part in the work area (Quickshell `ExclusionMode`).
//   Ignore  reserves nothing and ignores what other app bars reserve
//   Normal  reserves `exclusiveZone` pixels along the anchored edge
//   Auto    reserves the window's own thickness when it is anchored to one edge
namespace ExclusionMode {
Q_NAMESPACE
QML_NAMED_ELEMENT(ExclusionMode)
enum Enum { Ignore = 0, Normal = 1, Auto = 2 };
Q_ENUM_NS(Enum)
}  // namespace ExclusionMode

// The z-order class of a PanelWindow (wlr-layer-shell `layer`). On Windows: Top is topmost,
// Overlay is topmost and raised each time it is shown, Bottom and Background stay below normal
// windows (Background is not yet attached behind the desktop icons).
namespace WlrLayer {
Q_NAMESPACE
QML_NAMED_ELEMENT(WlrLayer)
enum Enum { Background = 0, Bottom = 1, Top = 2, Overlay = 3 };
Q_ENUM_NS(Enum)
}  // namespace WlrLayer

// Whether a PanelWindow takes keyboard focus (wlr-layer-shell `keyboard_interactivity`).
//   None       never activated (WS_EX_NOACTIVATE): clicks do not steal focus
//   OnDemand   activated when clicked, and when the window is shown at run time (show(), toggle(),
//              `wabarc surface show`); it releases the focus when it hides
//   Exclusive  treated as OnDemand: Windows has no exclusive keyboard grab
namespace WlrKeyboardFocus {
Q_NAMESPACE
QML_NAMED_ELEMENT(WlrKeyboardFocus)
enum Enum { None = 0, Exclusive = 1, OnDemand = 2 };
Q_ENUM_NS(Enum)
}  // namespace WlrKeyboardFocus

// Sides of a rectangle, as flags (Quickshell `Edges`): PopupWindow's `anchor.edges` and
// `anchor.gravity`.
namespace Edges {
Q_NAMESPACE
QML_NAMED_ELEMENT(Edges)
enum Flag { None = 0, Top = 1, Left = 2, Right = 4, Bottom = 8 };
Q_ENUM_NS(Flag)
}  // namespace Edges

// The screen edge a shell window is docked to: `QsWindow.window.dockEdge`. A PanelWindow is docked
// when it is anchored to one edge and stretched or centred across it (host/panel_geometry.h);
// every other window has None. wabar's own: Quickshell has no such property.
namespace DockEdge {
Q_NAMESPACE
QML_NAMED_ELEMENT(DockEdge)
enum Enum { None = 0, Top = 1, Bottom = 2, Left = 3, Right = 4 };
Q_ENUM_NS(Enum)
}  // namespace DockEdge

// The native backdrop of a shell window (DWM): none, the blur behind, or Mica.
namespace Backdrop {
Q_NAMESPACE
QML_NAMED_ELEMENT(Backdrop)
enum Kind { None = 0, Blur = 1, Mica = 2 };
Q_ENUM_NS(Kind)
}  // namespace Backdrop

}  // namespace wabar::host
