#pragma once

#include <QtQml/qqmlregistration.h>

#include <QMargins>
#include <QObject>
#include <QRect>
#include <QString>
#include <optional>

#include "host/panel_geometry.h"
#include "host/qml/qs_window.h"
#include "host/qml/window_enums.h"

namespace waku::host {

// `anchors` of a PanelWindow: the screen edges it is attached to.
class PanelAnchors : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(bool top MEMBER top_ NOTIFY changed FINAL)
  Q_PROPERTY(bool bottom MEMBER bottom_ NOTIFY changed FINAL)
  Q_PROPERTY(bool left MEMBER left_ NOTIFY changed FINAL)
  Q_PROPERTY(bool right MEMBER right_ NOTIFY changed FINAL)

 public:
  explicit PanelAnchors(QObject* parent = nullptr) : QObject(parent) {}
  PanelAnchorSet set() const { return {top_, bottom_, left_, right_}; }

 signals:
  void changed();

 private:
  bool top_ = false;
  bool bottom_ = false;
  bool left_ = false;
  bool right_ = false;
};

// `margins` of a PanelWindow: the distance to each anchored edge, logical pixels.
class PanelMargins : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(int left MEMBER left_ NOTIFY changed FINAL)
  Q_PROPERTY(int right MEMBER right_ NOTIFY changed FINAL)
  Q_PROPERTY(int top MEMBER top_ NOTIFY changed FINAL)
  Q_PROPERTY(int bottom MEMBER bottom_ NOTIFY changed FINAL)

 public:
  explicit PanelMargins(QObject* parent = nullptr) : QObject(parent) {}
  QMargins margins() const { return {left_, top_, right_, bottom_}; }

 signals:
  void changed();

 private:
  int left_ = 0;
  int right_ = 0;
  int top_ = 0;
  int bottom_ = 0;
};

// A window attached to screen edges (Quickshell `PanelWindow`): a bar, a dock, a full-screen
// layer. Geometry follows `anchors`, `margins`, `implicitWidth` and `implicitHeight` on its
// screen (host/panel_geometry.h). Anchored to the top or the bottom edge alone it is a Windows
// app bar, and it reserves work area (`exclusiveZone`, `exclusionMode`) only when the host runs
// with --register-app-bar. The rule for a docked window is in panelAppBarEdge()
// (host/panel_geometry.h): one edge anchored, the opposite not, the perpendicular axis stretched
// or centred. That edge is `dockEdge`, on all four sides; the app bar uses the same edge.
//
//     PanelWindow {
//         anchors { top: true; left: true; right: true }
//         implicitHeight: 32
//         color: "transparent"
//         Text { anchors.centerIn: parent; text: "hello" }
//     }
class PanelWindow : public QsWindow {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(waku::host::PanelAnchors* anchors READ anchors CONSTANT FINAL)
  Q_PROPERTY(waku::host::PanelMargins* margins READ margins CONSTANT FINAL)
  // Setting it switches `exclusionMode` from Auto to Normal (as Quickshell does).
  Q_PROPERTY(
      int exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged FINAL)
  Q_PROPERTY(waku::host::ExclusionMode::Enum exclusionMode READ exclusionMode WRITE setExclusionMode
                 NOTIFY exclusionModeChanged FINAL)
  // `layer` is Top or Overlay.
  Q_PROPERTY(bool aboveWindows READ aboveWindows WRITE setAboveWindows NOTIFY layerChanged FINAL)
  // `keyboardFocus` is not None.
  Q_PROPERTY(bool focusable READ focusable WRITE setFocusable NOTIFY keyboardFocusChanged FINAL)
  Q_PROPERTY(waku::host::WlrLayer::Enum layer READ layer WRITE setLayer NOTIFY layerChanged FINAL)
  Q_PROPERTY(waku::host::WlrKeyboardFocus::Enum keyboardFocus READ keyboardFocus WRITE
                 setKeyboardFocus NOTIFY keyboardFocusChanged FINAL)

 public:
  explicit PanelWindow(QObject* parent = nullptr);

  PanelAnchors* anchors() const { return anchors_; }
  PanelMargins* margins() const { return margins_; }
  int exclusiveZone() const { return exclusiveZone_; }
  void setExclusiveZone(int zone);
  ExclusionMode::Enum exclusionMode() const { return exclusionMode_; }
  void setExclusionMode(ExclusionMode::Enum mode);
  bool aboveWindows() const { return layer_ >= WlrLayer::Top; }
  void setAboveWindows(bool above);
  bool focusable() const { return keyboardFocus_ != WlrKeyboardFocus::None; }
  void setFocusable(bool focusable);
  WlrLayer::Enum layer() const { return layer_; }
  void setLayer(WlrLayer::Enum layer);
  WlrKeyboardFocus::Enum keyboardFocus() const { return keyboardFocus_; }
  void setKeyboardFocus(WlrKeyboardFocus::Enum focus);
  QString layerNamespace() const { return namespace_; }
  void setLayerNamespace(const QString& name);

  // The geometry it asks for on `screen` (logical pixels).
  QRect geometryOn(const QRect& screen) const;
  // The app bar edge, if it is one, and the pixels it reserves (before the host's gate).
  std::optional<AppBarEdge> appBarEdge() const { return panelAppBarEdge(anchors_->set()); }
  DockEdge::Enum dockEdge() const override;
  int reservedZone() const;
  // How far (logical pixels) the shell pushed this panel inward from its screen edge to make room
  // for another app bar; applyGeometry keeps the panel there. The host sets it after it registers
  // the bar.
  int appBarInset() const { return appBarInset_; }
  void setAppBarInset(int inset);

  bool showsErrors() const override { return true; }
  bool stackTopmost() const override { return layer_ >= WlrLayer::Top; }
  bool takesFocus() const override { return keyboardFocus_ != WlrKeyboardFocus::None; }

 signals:
  void exclusiveZoneChanged();
  void exclusionModeChanged();
  void layerChanged();
  void keyboardFocusChanged();
  void layerNamespaceChanged();
  // The window was placed again (anchors, margins, size, screen or inset changed): the host asks
  // the shell for the app bar rect again if the request differs.
  void layoutChanged();

 protected:
  void applyGeometry() override;
  Qt::WindowFlags windowFlags() const override;
  void afterShow() override;

 private:
  void updateDockEdge();

  PanelAnchors* anchors_;
  PanelMargins* margins_;
  DockEdge::Enum lastDockEdge_ = DockEdge::None;
  int appBarInset_ = 0;
  int exclusiveZone_ = 0;
  ExclusionMode::Enum exclusionMode_ = ExclusionMode::Auto;
  WlrLayer::Enum layer_ = WlrLayer::Top;
  WlrKeyboardFocus::Enum keyboardFocus_ = WlrKeyboardFocus::None;
  QString namespace_;
};

}  // namespace waku::host
