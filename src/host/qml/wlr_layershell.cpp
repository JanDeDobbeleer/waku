#include "host/qml/wlr_layershell.h"

#include "host/qml/panel_window.h"

namespace wabar::host {

WlrLayershell::WlrLayershell(QObject* attachee)
    : QObject(attachee), panel_(qobject_cast<PanelWindow*>(attachee)) {
  if (!panel_) return;
  connect(panel_, &PanelWindow::layerChanged, this, &WlrLayershell::changed);
  connect(panel_, &PanelWindow::keyboardFocusChanged, this, &WlrLayershell::changed);
  connect(panel_, &PanelWindow::layerNamespaceChanged, this, &WlrLayershell::changed);
  connect(panel_, &PanelWindow::exclusionModeChanged, this, &WlrLayershell::changed);
}

WlrLayer::Enum WlrLayershell::layer() const { return panel_ ? panel_->layer() : WlrLayer::Top; }

void WlrLayershell::setLayer(WlrLayer::Enum layer) {
  if (panel_) panel_->setLayer(layer);
}

WlrKeyboardFocus::Enum WlrLayershell::keyboardFocus() const {
  return panel_ ? panel_->keyboardFocus() : WlrKeyboardFocus::None;
}

void WlrLayershell::setKeyboardFocus(WlrKeyboardFocus::Enum focus) {
  if (panel_) panel_->setKeyboardFocus(focus);
}

QString WlrLayershell::layerNamespace() const {
  return panel_ ? panel_->layerNamespace() : QString();
}

void WlrLayershell::setLayerNamespace(const QString& name) {
  if (panel_) panel_->setLayerNamespace(name);
}

ExclusionMode::Enum WlrLayershell::exclusionMode() const {
  return panel_ ? panel_->exclusionMode() : ExclusionMode::Auto;
}

void WlrLayershell::setExclusionMode(ExclusionMode::Enum mode) {
  if (panel_) panel_->setExclusionMode(mode);
}

}  // namespace wabar::host
