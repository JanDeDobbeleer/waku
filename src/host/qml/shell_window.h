#pragma once

#include <QtQml/qqmlregistration.h>

#include <QColor>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlListProperty>
#include <QQmlParserStatus>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSize>
#include <memory>

#include "host/panel_geometry.h"
#include "host/qml/shell_screen.h"
#include "host/qml/window_enums.h"

namespace wabar::host {

class ShellHost;
class QsWindow;
class QsWindowAttached;

// The QQuickWindow behind a QsWindow. Items find their QsWindow through it (QsWindow.window).
class ProxiedWindow : public QQuickWindow {
  Q_OBJECT

 public:
  explicit ProxiedWindow(QsWindow* proxy) : proxy_(proxy) {}
  QsWindow* proxy() const { return proxy_; }

 private:
  QsWindow* proxy_;
};

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

// `surfaceFormat` of a window (Quickshell's QsSurfaceFormat): accepted so ported shells load, and
// ignored. wabar always draws into a swap chain with an alpha channel.
class SurfaceFormat : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(bool opaque MEMBER opaque_ NOTIFY opaqueChanged FINAL)

 public:
  explicit SurfaceFormat(QObject* parent = nullptr) : QObject(parent) {}

 signals:
  void opaqueChanged();

 private:
  bool opaque_ = false;
};

// The base of the shell's windows (Quickshell `QsWindow`): a declaration that owns a real window.
// Its children (the default property) go into the window's content item, as in Quickshell. Not
// creatable; PanelWindow and FloatingWindow are.
//
// Life cycle: the window is built when the declaration completes. In a shell loaded by ShellHost
// it is shown once the host installs the shell (a window created later, by a Variants or a
// LazyLoader, is shown at once); without a host it shows itself. wabar's `content` component, when
// set, is built into the window first; if that fails the window is not shown and the host records
// the error (proposal 4.1.3: one broken window does not take the others down).
//
// Threading: GUI thread only. Owns the backing window; destroyed with the shell it belongs to.
class QsWindow : public QObject, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_NAMED_ELEMENT(QsWindow)
  QML_UNCREATABLE("QsWindow is the base of PanelWindow and FloatingWindow")
  QML_ATTACHED(wabar::host::QsWindowAttached)
  Q_CLASSINFO("DefaultProperty", "data")
  Q_PROPERTY(QQmlListProperty<QObject> data READ data FINAL)
  Q_PROPERTY(QQuickItem* contentItem READ contentItem CONSTANT FINAL)
  // Null: the primary screen.
  Q_PROPERTY(
      wabar::host::ShellScreen* screen READ screen WRITE setScreen NOTIFY screenChanged FINAL)
  // Unset: the wabar surface colour (`background`, else Theme.surface; transparent with islands).
  Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
  Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged FINAL)
  Q_PROPERTY(
      int implicitWidth READ implicitWidth WRITE setImplicitWidth NOTIFY implicitWidthChanged FINAL)
  Q_PROPERTY(int implicitHeight READ implicitHeight WRITE setImplicitHeight NOTIFY
                 implicitHeightChanged FINAL)
  // The size the window got.
  Q_PROPERTY(int width READ width NOTIFY widthChanged FINAL)
  Q_PROPERTY(int height READ height NOTIFY heightChanged FINAL)
  Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio NOTIFY screenChanged FINAL)
  // wabar: the screen edge the window is docked to, derived from `anchors` (None for a window that
  // is not a docked PanelWindow), and whether that edge is Left or Right. Read-only. They are named
  // `dockEdge` and `dockVertical`, not `edge` and `vertical`, so a Quickshell shell that declares
  // its own `edge` or `vertical` property on a window keeps loading.
  Q_PROPERTY(wabar::host::DockEdge::Enum dockEdge READ dockEdge NOTIFY dockEdgeChanged FINAL)
  Q_PROPERTY(bool dockVertical READ dockVertical NOTIFY dockEdgeChanged FINAL)
  // The window is on the screen (it is visible, built, and its shell is installed).
  Q_PROPERTY(
      bool backingWindowVisible READ backingWindowVisible NOTIFY backingWindowVisibleChanged FINAL)

  // Accepted and ignored: see SurfaceFormat.
  Q_PROPERTY(wabar::host::SurfaceFormat* surfaceFormat READ surfaceFormat CONSTANT FINAL)

  // wabar extensions.
  // Built into the window when it is created (the Panel preset's content).
  Q_PROPERTY(QQmlComponent* content READ content WRITE setContent NOTIFY contentChanged FINAL)
  Q_PROPERTY(wabar::host::Backdrop::Kind backdrop READ backdrop WRITE setBackdrop NOTIFY
                 backdropChanged FINAL)
  // The surface colour of a wabar panel: the window colour while `color` is unset, and what
  // islands paint with. Unset: Theme.surface.
  Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY backgroundChanged FINAL)
  Q_PROPERTY(bool hasColor READ hasColor NOTIFY colorChanged FINAL)
  Q_PROPERTY(bool hasBackground READ hasBackground NOTIFY backgroundChanged FINAL)
  // HMONITOR of the monitor the window is on, set by the host (Workspaces matches its monitor).
  Q_PROPERTY(qlonglong monitorHandle READ monitorHandle NOTIFY monitorHandleChanged FINAL)
  // The ShellHost that built the window, or null (its `error`, `decorate()`, `windowPressed`).
  Q_PROPERTY(QObject* host READ host NOTIFY hostChanged FINAL)
  Q_PROPERTY(QQuickWindow* backingWindow READ backingWindow CONSTANT FINAL)
  // Whether the window shows the host's error pill (PanelWindow does).
  Q_PROPERTY(bool showsErrors READ showsErrors CONSTANT FINAL)

 public:
  explicit QsWindow(QObject* parent = nullptr);
  ~QsWindow() override;

  static QsWindowAttached* qmlAttachedProperties(QObject* object);
  // The QsWindow behind `window`, or null.
  static QsWindow* of(QQuickWindow* window);

  QQmlListProperty<QObject> data();
  QQuickItem* contentItem() const;
  SurfaceFormat* surfaceFormat() const { return surfaceFormat_; }
  ShellScreen* screen() const { return screen_; }
  void setScreen(ShellScreen* screen);
  QColor color() const { return color_; }
  void setColor(const QColor& color);
  bool visible() const { return visible_; }
  void setVisible(bool visible);
  int implicitWidth() const { return implicitWidth_; }
  void setImplicitWidth(int width);
  int implicitHeight() const { return implicitHeight_; }
  void setImplicitHeight(int height);
  int width() const;
  int height() const;
  qreal devicePixelRatio() const;
  bool backingWindowVisible() const;
  virtual DockEdge::Enum dockEdge() const { return DockEdge::None; }
  bool dockVertical() const {
    return dockEdge() == DockEdge::Left || dockEdge() == DockEdge::Right;
  }
  QQmlComponent* content() const { return content_; }
  void setContent(QQmlComponent* content);
  Backdrop::Kind backdrop() const { return backdrop_; }
  void setBackdrop(Backdrop::Kind backdrop);
  QColor background() const { return background_; }
  void setBackground(const QColor& color);
  bool hasColor() const { return color_.isValid(); }
  bool hasBackground() const { return background_.isValid(); }
  qlonglong monitorHandle() const { return monitorHandle_; }
  QObject* host() const;
  QQuickWindow* backingWindow() const { return window_.get(); }

  // The position and rectangle of `item` in this window's coordinates (Quickshell).
  Q_INVOKABLE QPointF itemPosition(QQuickItem* item) const;
  Q_INVOKABLE QRectF itemRect(QQuickItem* item) const;
  Q_INVOKABLE QPointF mapFromItem(QQuickItem* item, qreal x, qreal y) const;

  // ---- the host's side ----
  // The QScreen the window goes on: `screen`, else the primary of the engine's ScreenList.
  QScreen* targetScreen() const;
  // Empty when the window was built; otherwise why it was not.
  QString buildError() const { return buildError_; }
  // The host lets the window show (its shell is installed), or takes that back.
  void setPresentable(bool presentable);
  void setMonitorHandle(qlonglong handle);
  // Whether the wabar chrome of this window shows the host's error pill.
  virtual bool showsErrors() const { return false; }

  void classBegin() override {}
  void componentComplete() override;

 signals:
  void screenChanged();
  void colorChanged();
  void visibleChanged();
  void implicitWidthChanged();
  void implicitHeightChanged();
  void widthChanged();
  void heightChanged();
  void backingWindowVisibleChanged();
  void dockEdgeChanged();
  void contentChanged();
  void backdropChanged();
  void backgroundChanged();
  void monitorHandleChanged();
  void hostChanged();
  // The first time the backing window is shown: the host applies the native look then.
  void firstShown();

 protected:
  // Where the window goes and what kind of window it is; called while it is hidden or shown.
  virtual void applyGeometry() {}
  virtual Qt::WindowFlags windowFlags() const { return Qt::Window; }
  // Called right after the backing window is shown.
  virtual void afterShow() {}
  void updateFlags();
  void updateVisibility();
  bool complete() const { return complete_; }

 private:
  static void appendData(QQmlListProperty<QObject>* list, QObject* object);
  static qsizetype dataCount(QQmlListProperty<QObject>* list);
  static QObject* dataAt(QQmlListProperty<QObject>* list, qsizetype index);
  static void clearData(QQmlListProperty<QObject>* list);

  void buildChrome();
  bool buildContent();

  std::unique_ptr<ProxiedWindow> window_;
  SurfaceFormat* surfaceFormat_;
  QList<QObject*> data_;
  QPointer<ShellScreen> screen_;
  QPointer<ShellHost> host_;
  QPointer<QQmlComponent> content_;
  QColor color_;
  QColor background_;
  Backdrop::Kind backdrop_ = Backdrop::None;
  QString buildError_;
  qlonglong monitorHandle_ = 0;
  int implicitWidth_ = 100;
  int implicitHeight_ = 100;
  bool visible_ = true;
  bool complete_ = false;
  bool presentable_ = false;
  bool shownOnce_ = false;
};

// `QsWindow.window` and friends, attached to any item (Quickshell `QsWindowAttached`): the shell
// window the item is in, or null (an item inside a PopupWindow, or not yet in a window).
class QsWindowAttached : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(wabar::host::QsWindow* window READ window NOTIFY windowChanged FINAL)
  Q_PROPERTY(QQuickItem* contentItem READ contentItem NOTIFY windowChanged FINAL)

 public:
  explicit QsWindowAttached(QObject* attachee);

  QsWindow* window() const { return window_; }
  QQuickItem* contentItem() const { return window_ ? window_->contentItem() : nullptr; }

  Q_INVOKABLE QPointF itemPosition(QQuickItem* item) const;
  Q_INVOKABLE QRectF itemRect(QQuickItem* item) const;
  Q_INVOKABLE QPointF mapFromItem(QQuickItem* item, qreal x, qreal y) const;

 signals:
  void windowChanged();

 private:
  void update();

  QPointer<QQuickItem> item_;
  QPointer<QsWindow> window_;
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
  Q_PROPERTY(wabar::host::PanelAnchors* anchors READ anchors CONSTANT FINAL)
  Q_PROPERTY(wabar::host::PanelMargins* margins READ margins CONSTANT FINAL)
  // Setting it switches `exclusionMode` from Auto to Normal (as Quickshell does).
  Q_PROPERTY(
      int exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged FINAL)
  Q_PROPERTY(wabar::host::ExclusionMode::Enum exclusionMode READ exclusionMode WRITE
                 setExclusionMode NOTIFY exclusionModeChanged FINAL)
  // `layer` is Top or Overlay.
  Q_PROPERTY(bool aboveWindows READ aboveWindows WRITE setAboveWindows NOTIFY layerChanged FINAL)
  // `keyboardFocus` is not None.
  Q_PROPERTY(bool focusable READ focusable WRITE setFocusable NOTIFY keyboardFocusChanged FINAL)
  Q_PROPERTY(wabar::host::WlrLayer::Enum layer READ layer WRITE setLayer NOTIFY layerChanged FINAL)
  Q_PROPERTY(wabar::host::WlrKeyboardFocus::Enum keyboardFocus READ keyboardFocus WRITE
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

  bool showsErrors() const override { return true; }

 signals:
  void exclusiveZoneChanged();
  void exclusionModeChanged();
  void layerChanged();
  void keyboardFocusChanged();
  void layerNamespaceChanged();

 protected:
  void applyGeometry() override;
  Qt::WindowFlags windowFlags() const override;
  void afterShow() override;

 private:
  void updateDockEdge();

  PanelAnchors* anchors_;
  PanelMargins* margins_;
  DockEdge::Enum lastDockEdge_ = DockEdge::None;
  int exclusiveZone_ = 0;
  ExclusionMode::Enum exclusionMode_ = ExclusionMode::Auto;
  WlrLayer::Enum layer_ = WlrLayer::Top;
  WlrKeyboardFocus::Enum keyboardFocus_ = WlrKeyboardFocus::None;
  QString namespace_;
};

// A normal decorated window (Quickshell `FloatingWindow`): a title bar, movable and resizable by
// the user. It opens centred on its screen at its implicit size.
class FloatingWindow : public QsWindow {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged FINAL)
  Q_PROPERTY(
      QSize minimumSize READ minimumSize WRITE setMinimumSize NOTIFY minimumSizeChanged FINAL)
  Q_PROPERTY(
      QSize maximumSize READ maximumSize WRITE setMaximumSize NOTIFY maximumSizeChanged FINAL)

 public:
  explicit FloatingWindow(QObject* parent = nullptr);

  QString title() const { return title_; }
  void setTitle(const QString& title);
  QSize minimumSize() const { return minimumSize_; }
  void setMinimumSize(const QSize& size);
  QSize maximumSize() const { return maximumSize_; }
  void setMaximumSize(const QSize& size);

 signals:
  void titleChanged();
  void minimumSizeChanged();
  void maximumSizeChanged();

 protected:
  void applyGeometry() override;
  Qt::WindowFlags windowFlags() const override;
  void afterShow() override { placed_ = true; }

 private:
  QString title_;
  QSize minimumSize_;
  QSize maximumSize_;
  bool placed_ = false;
};

// `WlrLayershell.layer`, `.keyboardFocus`, `.namespace` and `.exclusionMode`, attached to a
// PanelWindow (Quickshell.Wayland's layer-shell properties). They read and write the same state
// as the PanelWindow's own `layer`, `keyboardFocus` and `exclusionMode`, so a ported
// StyledWindow loads unchanged. On anything that is not a PanelWindow they do nothing.
class WlrLayershell : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("WlrLayershell is an attached property of PanelWindow")
  QML_ATTACHED(wabar::host::WlrLayershell)
  Q_PROPERTY(wabar::host::WlrLayer::Enum layer READ layer WRITE setLayer NOTIFY changed FINAL)
  Q_PROPERTY(wabar::host::WlrKeyboardFocus::Enum keyboardFocus READ keyboardFocus WRITE
                 setKeyboardFocus NOTIFY changed FINAL)
  Q_PROPERTY(QString namespace READ layerNamespace WRITE setLayerNamespace NOTIFY changed FINAL)
  Q_PROPERTY(wabar::host::ExclusionMode::Enum exclusionMode READ exclusionMode WRITE
                 setExclusionMode NOTIFY changed FINAL)

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

}  // namespace wabar::host
