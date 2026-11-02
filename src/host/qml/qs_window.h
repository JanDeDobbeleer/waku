#pragma once

// The base of the shell's windows: QsWindow, its attached object, its backing window and
// its surface format. The concrete windows are in panel_window.h and floating_window.h, the
// Quickshell.Wayland attached type in wlr_layershell.h.

#include <QtQml/qqmlregistration.h>

#include <QColor>
#include <QKeyEvent>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlListProperty>
#include <QQmlParserStatus>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSize>
#include <QTimer>
#include <memory>

#include "host/mask_poller.h"
#include "host/qml/region.h"
#include "host/qml/shell_screen.h"
#include "host/qml/window_enums.h"

namespace waku::host {

class ShellHost;
class QsWindow;
class QsWindowAttached;

// The QQuickWindow behind a QsWindow. Items find their QsWindow through it (QsWindow.window).
class ProxiedWindow : public QQuickWindow {
  Q_OBJECT

 public:
  explicit ProxiedWindow(QsWindow* proxy) : proxy_(proxy) {}
  // Null once the QsWindow is being destroyed.
  QsWindow* proxy() const { return proxy_; }
  void detach() { proxy_ = nullptr; }

 protected:
  // Escape hides a window with `dismissOnEscape` when no item took the key.
  void keyPressEvent(QKeyEvent* event) override;

 private:
  QsWindow* proxy_;
};

// `surfaceFormat` of a window (Quickshell's QsSurfaceFormat): accepted so ported shells load, and
// ignored. waku always draws into a swap chain with an alpha channel.
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
// LazyLoader, is shown at once); without a host it shows itself. waku's `content` component, when
// set, is built into the window first; if that fails the window is not shown and the host records
// the error (proposal 4.1.3: one broken window does not take the others down).
//
// Threading: GUI thread only. Owns the backing window; destroyed with the shell it belongs to.
class QsWindow : public QObject, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_NAMED_ELEMENT(QsWindow)
  QML_UNCREATABLE("QsWindow is the base of PanelWindow and FloatingWindow")
  QML_ATTACHED(waku::host::QsWindowAttached)
  Q_CLASSINFO("DefaultProperty", "data")
  Q_PROPERTY(QQmlListProperty<QObject> data READ data FINAL)
  Q_PROPERTY(QQuickItem* contentItem READ contentItem CONSTANT)
  // Null: the primary screen.
  Q_PROPERTY(waku::host::ShellScreen* screen READ screen WRITE setScreen NOTIFY screenChanged)
  // Unset: the waku surface colour (`background`, else Theme.surface; transparent with islands).
  Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
  // On or off the screen. A window with `visible: false` has no native window until it is first
  // shown. Setting it to false starts the `hideDelay`; the window leaves the screen after it.
  Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged FINAL)
  Q_PROPERTY(
      int implicitWidth READ implicitWidth WRITE setImplicitWidth NOTIFY implicitWidthChanged FINAL)
  Q_PROPERTY(int implicitHeight READ implicitHeight WRITE setImplicitHeight NOTIFY
                 implicitHeightChanged FINAL)
  // The size the window got.
  Q_PROPERTY(int width READ width NOTIFY widthChanged FINAL)
  Q_PROPERTY(int height READ height NOTIFY heightChanged FINAL)
  Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio NOTIFY screenChanged FINAL)
  // waku: the screen edge the window is docked to, derived from `anchors` (None for a window that
  // is not a docked PanelWindow), and whether that edge is Left or Right. Read-only. They are named
  // `dockEdge` and `dockVertical`, not `edge` and `vertical`, so a Quickshell shell that declares
  // its own `edge` or `vertical` property on a window keeps loading.
  Q_PROPERTY(waku::host::DockEdge::Enum dockEdge READ dockEdge NOTIFY dockEdgeChanged FINAL)
  Q_PROPERTY(bool dockVertical READ dockVertical NOTIFY dockEdgeChanged FINAL)
  // The window is on the screen (it is visible, built, and its shell is installed).
  Q_PROPERTY(
      bool backingWindowVisible READ backingWindowVisible NOTIFY backingWindowVisibleChanged FINAL)

  // Accepted and ignored: see SurfaceFormat.
  Q_PROPERTY(waku::host::SurfaceFormat* surfaceFormat READ surfaceFormat CONSTANT FINAL)
  // The input region (Quickshell `mask`): only points inside it take input; the rest click through
  // to the window below. Null: the whole window takes input. An empty `Region {}`: none does. See
  // MaskPoller (host/mask_poller.h) for how, and for the live-desktop gate.
  Q_PROPERTY(waku::host::Region* mask READ mask WRITE setMask NOTIFY maskChanged FINAL)

  // waku extensions.
  // The id of the window for `wakuc surface ...` and hotkeys; unique per shell. One name can
  // belong to several windows (a Variants makes one per screen) and every verb applies to all of
  // them. Not FINAL: a ported shell that declares its own `name` property on a window (caelestia's
  // StyledWindow does) still loads, and the surface verbs use that property (surfaceName()).
  Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
  // Milliseconds between `visible` turning false and the window leaving the screen, so the content
  // can animate out. Showing the window again during the delay cancels the hide.
  Q_PROPERTY(int hideDelay READ hideDelay WRITE setHideDelay NOTIFY hideDelayChanged)
  // Hide the window when it was active and lost the activation / when Escape is pressed and no
  // item took the key. Meant for windows with keyboard focus (PanelWindow `keyboardFocus`
  // OnDemand); a window that cannot be active is never dismissed by blur.
  Q_PROPERTY(bool dismissOnBlur READ dismissOnBlur WRITE setDismissOnBlur NOTIFY
                 dismissOnBlurChanged FINAL)
  Q_PROPERTY(bool dismissOnEscape READ dismissOnEscape WRITE setDismissOnEscape NOTIFY
                 dismissOnEscapeChanged FINAL)
  // Built into the window when it is created (the Panel preset's content).
  Q_PROPERTY(QQmlComponent* content READ content WRITE setContent NOTIFY contentChanged FINAL)
  Q_PROPERTY(waku::host::Backdrop::Kind backdrop READ backdrop WRITE setBackdrop NOTIFY
                 backdropChanged FINAL)
  // The surface colour of a waku panel: the window colour while `color` is unset, and what
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
  Region* mask() const { return mask_.data(); }
  void setMask(Region* mask);
  // The mask as a value, or nothing when there is none.
  std::optional<RegionNode> maskNode() const;
  MaskPoller* maskPoller() const { return poller_.get(); }
  ShellScreen* screen() const { return screen_; }
  void setScreen(ShellScreen* screen);
  QColor color() const { return color_; }
  void setColor(const QColor& color);
  bool visible() const { return visible_; }
  void setVisible(bool visible);
  QString name() const { return name_; }
  void setName(const QString& name);
  // The id the surface verbs and the visibility memory use: the `name` property as the QML object
  // has it (so a component that declares its own `name`, as Caelestia's StyledWindow does, is found
  // by that), else waku's `name`, else `objectName`. Empty: the window is not addressable.
  QString surfaceName() const;
  int hideDelay() const { return hideDelay_; }
  void setHideDelay(int milliseconds);
  // Whether the window waits out its `hideDelay` before it leaves the screen (tests).
  bool hidePending() const { return hideTimer_.isActive(); }
  bool dismissOnBlur() const { return dismissOnBlur_; }
  void setDismissOnBlur(bool dismiss);
  bool dismissOnEscape() const { return dismissOnEscape_; }
  void setDismissOnEscape(bool dismiss);
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
  // Null only while the QsWindow is being destroyed: its teardown can wake bindings that read it.
  QQuickWindow* backingWindow() const { return window_.get(); }

  // The position and rectangle of `item` in this window's coordinates (Quickshell).
  Q_INVOKABLE QPointF itemPosition(QQuickItem* item) const;
  Q_INVOKABLE QRectF itemRect(QQuickItem* item) const;
  Q_INVOKABLE QPointF mapFromItem(QQuickItem* item, qreal x, qreal y) const;

  // waku: `visible = true`, `visible = false` and the flip of it. The signals below tell when
  // the native window really appeared and left. A window with keyboard focus (OnDemand) takes the
  // keyboard focus when shown this way, from a hotkey or from `wakuc` too.
  Q_INVOKABLE void show() { setVisible(true); }
  Q_INVOKABLE void hide() { setVisible(false); }
  Q_INVOKABLE void toggle() { setVisible(!visible_); }

  // ---- the host's side ----
  // The QScreen the window goes on: `screen`, else the primary of the engine's ScreenList.
  QScreen* targetScreen() const;
  // Empty when the window was built; otherwise why it was not.
  QString buildError() const { return buildError_; }
  // The host lets the window show (its shell is installed), or takes that back.
  void setPresentable(bool presentable);
  // The window is deleted later (its owner called deleteLater): it leaves the screen and stops
  // counting as a window of its shell now.
  void retire();
  void setMonitorHandle(qlonglong handle);
  // Whether the waku chrome of this window shows the host's error pill.
  virtual bool showsErrors() const { return false; }
  // The native window is kept above the others / can take the keyboard focus.
  virtual bool stackTopmost() const { return false; }
  virtual bool takesFocus() const { return true; }

  void classBegin() override {}
  void componentComplete() override;

 signals:
  void screenChanged();
  void colorChanged();
  void visibleChanged();
  void nameChanged();
  void hideDelayChanged();
  void dismissOnBlurChanged();
  void dismissOnEscapeChanged();
  // The native window appeared / left the screen (after `hideDelay`). waku extensions.
  void shown();
  void hidden();
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
  void maskChanged();
  // The first time the backing window is shown: the host applies the native look then.
  void firstShown();

 protected:
  // Where the window goes and what kind of window it is; called while it is hidden or shown.
  virtual void applyGeometry() {}
  virtual Qt::WindowFlags windowFlags() const { return Qt::Window; }
  // Called right after the backing window is shown.
  virtual void afterShow() {}
  void updateFlags();
  // The layer or the keyboard focus changed: apply it to a window that is on the screen.
  void stackingChanged();
  void updateVisibility();
  void updateMaskPoller();
  void connectMask();
  bool complete() const { return complete_; }

 private:
  static void appendData(QQmlListProperty<QObject>* list, QObject* object);
  static qsizetype dataCount(QQmlListProperty<QObject>* list);
  static QObject* dataAt(QQmlListProperty<QObject>* list, qsizetype index);
  static void clearData(QQmlListProperty<QObject>* list);

  void buildChrome();
  bool buildContent();
  void hideNow();
  void onActiveChanged();

  std::unique_ptr<ProxiedWindow> window_;
  SurfaceFormat* surfaceFormat_;
  QList<QObject*> data_;
  QPointer<ShellScreen> screen_;
  QPointer<ShellHost> host_;
  QPointer<QQmlComponent> content_;
  QPointer<Region> mask_;
  std::unique_ptr<MaskPoller> poller_;
  QMetaObject::Connection maskConnection_;
  QColor color_;
  QColor background_;
  Backdrop::Kind backdrop_ = Backdrop::None;
  QString buildError_;
  QString name_;
  QTimer hideTimer_;
  int hideDelay_ = 0;
  bool dismissOnBlur_ = false;
  bool dismissOnEscape_ = false;
  // The window was active since it was shown (so losing the activation means the user left).
  bool wasActive_ = false;
  // A runtime show wants the keyboard focus; the initial show of a shell does not take it.
  bool activateOnShow_ = false;
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
  Q_PROPERTY(waku::host::QsWindow* window READ window NOTIFY windowChanged FINAL)
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

}  // namespace waku::host
