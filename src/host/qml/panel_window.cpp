#include "host/qml/panel_window.h"

#include <QQuickWindow>
#include <QScreen>
#include <algorithm>

namespace waku::host {

PanelWindow::PanelWindow(QObject* parent)
    : QsWindow(parent), anchors_(new PanelAnchors(this)), margins_(new PanelMargins(this)) {
  connect(anchors_, &PanelAnchors::changed, this, [this] {
    updateDockEdge();
    if (complete()) applyGeometry();
  });
  connect(margins_, &PanelMargins::changed, this, [this] {
    if (complete()) applyGeometry();
  });
}

DockEdge::Enum PanelWindow::dockEdge() const {
  const std::optional<AppBarEdge> edge = appBarEdge();
  if (!edge) return DockEdge::None;
  switch (*edge) {
    case AppBarEdge::Left:
      return DockEdge::Left;
    case AppBarEdge::Top:
      return DockEdge::Top;
    case AppBarEdge::Right:
      return DockEdge::Right;
    case AppBarEdge::Bottom:
      return DockEdge::Bottom;
  }
  return DockEdge::None;
}

void PanelWindow::updateDockEdge() {
  const DockEdge::Enum edge = dockEdge();
  if (edge == lastDockEdge_) return;
  lastDockEdge_ = edge;
  emit dockEdgeChanged();
}

void PanelWindow::setExclusiveZone(int zone) {
  const bool modeChanges = exclusionMode_ == ExclusionMode::Auto;
  if (exclusiveZone_ == zone && !modeChanges) return;
  exclusiveZone_ = zone;
  if (modeChanges) {
    exclusionMode_ = ExclusionMode::Normal;
    emit exclusionModeChanged();
  }
  emit exclusiveZoneChanged();
}

void PanelWindow::setExclusionMode(ExclusionMode::Enum mode) {
  if (exclusionMode_ == mode) return;
  exclusionMode_ = mode;
  emit exclusionModeChanged();
}

void PanelWindow::setAboveWindows(bool above) {
  if (above == aboveWindows()) return;
  setLayer(above ? WlrLayer::Top : WlrLayer::Bottom);
}

void PanelWindow::setFocusable(bool focusable) {
  if (focusable == this->focusable()) return;
  setKeyboardFocus(focusable ? WlrKeyboardFocus::OnDemand : WlrKeyboardFocus::None);
}

void PanelWindow::setLayer(WlrLayer::Enum layer) {
  if (layer_ == layer) return;
  layer_ = layer;
  emit layerChanged();
  if (complete()) {
    updateFlags();
    stackingChanged();
  }
}

void PanelWindow::setKeyboardFocus(WlrKeyboardFocus::Enum focus) {
  if (keyboardFocus_ == focus) return;
  keyboardFocus_ = focus;
  emit keyboardFocusChanged();
  if (complete()) {
    updateFlags();
    stackingChanged();
  }
}

void PanelWindow::setLayerNamespace(const QString& name) {
  if (namespace_ == name) return;
  namespace_ = name;
  emit layerNamespaceChanged();
}

QRect PanelWindow::geometryOn(const QRect& screen) const {
  return panelGeometry(anchors_->set(), margins_->margins(),
                       QSize(implicitWidth(), implicitHeight()), screen);
}

int PanelWindow::reservedZone() const {
  return panelReservedZone(anchors_->set(), margins_->margins(), exclusionMode_, exclusiveZone_,
                           QSize(width(), height()));
}

void PanelWindow::applyGeometry() {
  QScreen* screen = targetScreen();
  QQuickWindow* window = backingWindow();
  if (!screen || !window) return;
  if (window->screen() != screen) window->setScreen(screen);
  QRect geometry = geometryOn(screen->geometry());
  // The shell may have granted the bar less than the screen edge (another app bar holds it).
  if (const std::optional<AppBarEdge> edge = appBarEdge(); edge && appBarInset_ > 0) {
    geometry.translate(appBarWindowShift(*edge, appBarInset_));
  }
  window->setGeometry(geometry);
  emit layoutChanged();
}

void PanelWindow::setAppBarInset(int inset) {
  inset = std::max(0, inset);
  if (appBarInset_ == inset) return;
  appBarInset_ = inset;
  if (complete()) applyGeometry();
}

Qt::WindowFlags PanelWindow::windowFlags() const {
  Qt::WindowFlags flags = Qt::Tool | Qt::FramelessWindowHint;
  if (layer_ >= WlrLayer::Top) {
    flags |= Qt::WindowStaysOnTopHint;
  } else {
    flags |= Qt::WindowStaysOnBottomHint;
  }
  if (keyboardFocus_ == WlrKeyboardFocus::None) flags |= Qt::WindowDoesNotAcceptFocus;
  return flags;
}

void PanelWindow::afterShow() {
  // Top and Overlay are raised by ShellHost::windowShown (NativeHooks::raiseTopmost).
}

}  // namespace waku::host
