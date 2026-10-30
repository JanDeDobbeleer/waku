#pragma once

#include <QMargins>
#include <QRect>
#include <QSize>
#include <optional>

#include "host/app_bar.h"

namespace wabar::host {

// The edges a PanelWindow is anchored to (Quickshell `anchors`).
struct PanelAnchorSet {
  bool top = false;
  bool bottom = false;
  bool left = false;
  bool right = false;
};

// Where a PanelWindow goes on its screen, as a pure function so it can be tested without windows.
// On each axis: anchored to both sides it stretches between them (less the margins); anchored to
// one side it keeps its implicit size against that side, `margin` away; anchored to neither it is
// centred. All four anchors is the full screen. Logical pixels, in the screen's coordinate space.
QRect panelGeometry(const PanelAnchorSet& anchors, const QMargins& margins,
                    const QSize& implicitSize, const QRect& screen);

// The screen edge a PanelWindow docks to, as an app bar: Top or Bottom when it is anchored to that
// edge and not to the opposite one. A window anchored to neither or to both (full screen, a
// centred overlay, a vertical panel) is not an app bar.
std::optional<AppBarEdge> panelAppBarEdge(const PanelAnchorSet& anchors);

// The pixels a PanelWindow reserves along its app bar edge, before the host's
// --register-app-bar gate. `mode` is an ExclusionMode value (Ignore 0, Normal 1, Auto 2):
// Ignore reserves nothing, Normal `exclusiveZone` plus the margin on that edge, Auto the window's
// thickness plus the margin. Nothing when the window is not an app bar.
int panelReservedZone(const PanelAnchorSet& anchors, const QMargins& margins, int mode,
                      int exclusiveZone, const QSize& size);

}  // namespace wabar::host
