#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointer>
#include <QString>

#include "host/qml/window_enums.h"

namespace waku::host {

class PanelWindow;

// `WlrLayershell.layer`, `.keyboardFocus`, `.namespace` and `.exclusionMode`, attached to a
// PanelWindow (Quickshell.Wayland's layer-shell properties). They read and write the same state
// as the PanelWindow's own `layer`, `keyboardFocus` and `exclusionMode`, so a ported
// StyledWindow loads unchanged. On anything that is not a PanelWindow they do nothing.
class WlrLayershell : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("WlrLayershell is an attached property of PanelWindow")
  QML_ATTACHED(waku::host::WlrLayershell)
  Q_PROPERTY(waku::host::WlrLayer::Enum layer READ layer WRITE setLayer NOTIFY changed FINAL)
  Q_PROPERTY(waku::host::WlrKeyboardFocus::Enum keyboardFocus READ keyboardFocus WRITE
                 setKeyboardFocus NOTIFY changed FINAL)
  Q_PROPERTY(QString namespace READ layerNamespace WRITE setLayerNamespace NOTIFY changed FINAL)
  Q_PROPERTY(waku::host::ExclusionMode::Enum exclusionMode READ exclusionMode WRITE setExclusionMode
                 NOTIFY changed FINAL)

 public:
  explicit WlrLayershell(QObject* attachee = nullptr);
  static WlrLayershell* qmlAttachedProperties(QObject* object) { return new WlrLayershell(object); }

  WlrLayer::Enum layer() const;
  void setLayer(WlrLayer::Enum layer);
  WlrKeyboardFocus::Enum keyboardFocus() const;
  void setKeyboardFocus(WlrKeyboardFocus::Enum focus);
  QString layerNamespace() const;
  void setLayerNamespace(const QString& name);
  ExclusionMode::Enum exclusionMode() const;
  void setExclusionMode(ExclusionMode::Enum mode);

 signals:
  void changed();

 private:
  QPointer<PanelWindow> panel_;
};

}  // namespace waku::host
