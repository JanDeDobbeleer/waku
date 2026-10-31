#include "host/qml/shell_window.h"

#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <algorithm>

#include "host/qml/shell_host.h"

namespace wabar::host {

namespace {

constexpr auto kChromeUrl = "qrc:/qt/qml/wabar/WindowChrome.qml";

QString describe(const QList<QQmlError>& errors, const QString& fallback) {
  if (errors.isEmpty()) return fallback;
  const QQmlError& error = errors.first();
  const QString file = QFileInfo(error.url().toLocalFile().isEmpty() ? error.url().fileName()
                                                                     : error.url().toLocalFile())
                           .fileName();
  return QStringLiteral("%1:%2:%3: %4")
      .arg(file)
      .arg(error.line())
      .arg(error.column())
      .arg(error.description());
}

// Keeps `item` the size of `container`.
void fill(QQuickItem* item, QQuickItem* container) {
  item->setSize(container->size());
  QObject::connect(container, &QQuickItem::widthChanged, item,
                   [item, container] { item->setWidth(container->width()); });
  QObject::connect(container, &QQuickItem::heightChanged, item,
                   [item, container] { item->setHeight(container->height()); });
}

}  // namespace

// ---- ProxiedWindow ----

void ProxiedWindow::keyPressEvent(QKeyEvent* event) {
  // Not accepted until an item takes the key: a TextInput keeps its own use of Escape.
  event->ignore();
  QQuickWindow::keyPressEvent(event);
  if (!event->isAccepted() && event->key() == Qt::Key_Escape && proxy_ &&
      proxy_->dismissOnEscape()) {
    event->accept();
    proxy_->hide();
  }
}

// ---- QsWindow ----

QsWindow::QsWindow(QObject* parent)
    : QObject(parent),
      window_(std::make_unique<ProxiedWindow>(this)),
      surfaceFormat_(new SurfaceFormat(this)) {
  window_->setColor(Qt::transparent);
  connect(window_.get(), &QWindow::widthChanged, this, &QsWindow::widthChanged);
  connect(window_.get(), &QWindow::heightChanged, this, &QsWindow::heightChanged);
  connect(window_.get(), &QWindow::visibleChanged, this, &QsWindow::backingWindowVisibleChanged);
  connect(window_.get(), &QWindow::visibleChanged, this, [this] { updateMaskPoller(); });
  connect(window_.get(), &QWindow::activeChanged, this, &QsWindow::onActiveChanged);
  hideTimer_.setSingleShot(true);
  connect(&hideTimer_, &QTimer::timeout, this, [this] {
    if (!visible_) hideNow();
  });
}

QsWindow::~QsWindow() {
  if (host_) host_->release(this);
  poller_.reset();
  // Hiding the window must not run dismissOnBlur or the visibility bookkeeping of a dying window.
  disconnect(window_.get(), nullptr, this, nullptr);
  hideTimer_.stop();
  window_->hide();
  // The declared items are this object's QObject children and die after this destructor; take
  // them out of the window first so its content item does not outlive them in a broken state.
  for (QObject* object : data_) {
    if (auto* item = qobject_cast<QQuickItem*>(object)) item->setParentItem(nullptr);
  }
  // Deleting the window unparents the chrome and content items, which wakes their bindings. From
  // here on QsWindow.of() finds no window and the getters see none (reset() nulls window_ first).
  window_->detach();
  window_.reset();
}

QsWindowAttached* QsWindow::qmlAttachedProperties(QObject* object) {
  return new QsWindowAttached(object);
}

QsWindow* QsWindow::of(QQuickWindow* window) {
  auto* proxied = qobject_cast<ProxiedWindow*>(window);
  return proxied ? proxied->proxy() : nullptr;
}

QQmlListProperty<QObject> QsWindow::data() {
  return QQmlListProperty<QObject>(this, this, &QsWindow::appendData, &QsWindow::dataCount,
                                   &QsWindow::dataAt, &QsWindow::clearData);
}

void QsWindow::appendData(QQmlListProperty<QObject>* list, QObject* object) {
  auto* self = static_cast<QsWindow*>(list->data);
  if (!object) return;
  self->data_.append(object);
  if (auto* item = qobject_cast<QQuickItem*>(object)) {
    item->setParentItem(self->contentItem());
  }
  if (!object->parent()) object->setParent(self);
}

qsizetype QsWindow::dataCount(QQmlListProperty<QObject>* list) {
  return static_cast<QsWindow*>(list->data)->data_.size();
}

QObject* QsWindow::dataAt(QQmlListProperty<QObject>* list, qsizetype index) {
  return static_cast<QsWindow*>(list->data)->data_.at(index);
}

void QsWindow::clearData(QQmlListProperty<QObject>* list) {
  auto* self = static_cast<QsWindow*>(list->data);
  for (QObject* object : self->data_) {
    if (auto* item = qobject_cast<QQuickItem*>(object)) item->setParentItem(nullptr);
  }
  self->data_.clear();
}

QQuickItem* QsWindow::contentItem() const { return window_ ? window_->contentItem() : nullptr; }

void QsWindow::setScreen(ShellScreen* screen) {
  if (screen_ == screen) return;
  if (screen_) disconnect(screen_, nullptr, this, nullptr);
  screen_ = screen;
  if (screen_) connect(screen_, &ShellScreen::geometryChanged, this, [this] { applyGeometry(); });
  emit screenChanged();
  if (complete_) applyGeometry();
}

void QsWindow::setColor(const QColor& color) {
  if (color_ == color) return;
  color_ = color;
  emit colorChanged();
}

void QsWindow::setMask(Region* mask) {
  if (mask_ == mask) return;
  mask_ = mask;
  emit maskChanged();
  if (poller_) {
    connectMask();
    poller_->markDirty();
  }
  updateMaskPoller();
}

// Marks the poller dirty when the mask or any region below it changes.
void QsWindow::connectMask() {
  QObject::disconnect(maskConnection_);
  if (mask_ && poller_) {
    maskConnection_ =
        connect(mask_.data(), &Region::treeChanged, poller_.get(), &MaskPoller::markDirty);
  }
}

std::optional<RegionNode> QsWindow::maskNode() const {
  if (!mask_) return std::nullopt;
  return mask_->node();
}

// The poller exists only on a live desktop: the host hands it a cursor source then (and tests
// hand it a fake).
void QsWindow::updateMaskPoller() {
  if (!window_) return;
  if (!poller_) {
    if (!mask_ || !host_ || !host_->maskHub()) return;
    poller_ = std::make_unique<MaskPoller>(window_.get(), host_->maskHub(),
                                           [this] { return maskNode(); });
    // A frame means something on the window moved or changed; the poller then tests the cursor
    // against the mask again even if the cursor rests.
    connect(window_.get(), &QQuickWindow::frameSwapped, poller_.get(), &MaskPoller::markDirty);
    connectMask();
  }
  poller_->refresh();
}

void QsWindow::setVisible(bool visible) {
  if (visible_ == visible) return;
  visible_ = visible;
  if (visible) hideTimer_.stop();
  // Before the shell is installed the window is only being set up: that is not the user showing it.
  const bool live = complete_ && presentable_;
  if (live) activateOnShow_ = visible;
  emit visibleChanged();
  updateVisibility();
  if (live && host_ && !surfaceName().isEmpty()) host_->noteVisibility(surfaceName(), visible);
}

QString QsWindow::surfaceName() const {
  // QObject::property goes through the QML object's own meta object, so a `name` declared by the
  // component shadows the C++ getter here.
  const QVariant declared = property("name");
  if (declared.metaType().id() == QMetaType::QString && !declared.toString().isEmpty()) {
    return declared.toString();
  }
  return name_.isEmpty() ? objectName() : name_;
}

void QsWindow::setName(const QString& name) {
  if (name_ == name) return;
  name_ = name;
  emit nameChanged();
}

void QsWindow::setHideDelay(int milliseconds) {
  milliseconds = std::max(0, milliseconds);
  if (hideDelay_ == milliseconds) return;
  hideDelay_ = milliseconds;
  emit hideDelayChanged();
}

void QsWindow::setDismissOnBlur(bool dismiss) {
  if (dismissOnBlur_ == dismiss) return;
  dismissOnBlur_ = dismiss;
  emit dismissOnBlurChanged();
}

void QsWindow::setDismissOnEscape(bool dismiss) {
  if (dismissOnEscape_ == dismiss) return;
  dismissOnEscape_ = dismiss;
  emit dismissOnEscapeChanged();
}

void QsWindow::onActiveChanged() {
  if (!window_) return;
  if (window_->isActive()) {
    wasActive_ = true;
    return;
  }
  // Active and not any more: the user went elsewhere. A window that was never active (the focus
  // was refused) is not dismissed, or it would vanish the moment it appeared.
  if (dismissOnBlur_ && wasActive_ && visible_ && takesFocus()) {
    wasActive_ = false;
    setVisible(false);
  }
}

void QsWindow::setImplicitWidth(int width) {
  if (implicitWidth_ == width) return;
  implicitWidth_ = width;
  emit implicitWidthChanged();
  if (complete_) applyGeometry();
}

void QsWindow::setImplicitHeight(int height) {
  if (implicitHeight_ == height) return;
  implicitHeight_ = height;
  emit implicitHeightChanged();
  if (complete_) applyGeometry();
}

int QsWindow::width() const { return window_ ? window_->width() : 0; }
int QsWindow::height() const { return window_ ? window_->height() : 0; }

qreal QsWindow::devicePixelRatio() const {
  QScreen* screen = targetScreen();
  return screen ? screen->devicePixelRatio() : 1.0;
}

bool QsWindow::backingWindowVisible() const { return window_ && window_->isVisible(); }

void QsWindow::setContent(QQmlComponent* content) {
  if (content_ == content) return;
  content_ = content;
  emit contentChanged();
}

void QsWindow::setBackdrop(Backdrop::Kind backdrop) {
  if (backdrop_ == backdrop) return;
  backdrop_ = backdrop;
  emit backdropChanged();
}

void QsWindow::setBackground(const QColor& color) {
  if (background_ == color) return;
  background_ = color;
  emit backgroundChanged();
}

QObject* QsWindow::host() const { return host_.data(); }

void QsWindow::setMonitorHandle(qlonglong handle) {
  if (monitorHandle_ == handle) return;
  monitorHandle_ = handle;
  emit monitorHandleChanged();
}

QPointF QsWindow::itemPosition(QQuickItem* item) const {
  return item ? item->mapToItem(contentItem(), QPointF(0, 0)) : QPointF();
}

QRectF QsWindow::itemRect(QQuickItem* item) const {
  return item ? item->mapRectToItem(contentItem(), QRectF(0, 0, item->width(), item->height()))
              : QRectF();
}

QPointF QsWindow::mapFromItem(QQuickItem* item, qreal x, qreal y) const {
  return item ? item->mapToItem(contentItem(), QPointF(x, y)) : QPointF();
}

QScreen* QsWindow::targetScreen() const {
  if (screen_ && screen_->qscreen()) return screen_->qscreen();
  if (ScreenList* list = ScreenList::of(qmlEngine(this))) {
    if (ShellScreen* primary = list->primary(); primary && primary->qscreen()) {
      return primary->qscreen();
    }
  }
  return QGuiApplication::primaryScreen();
}

void QsWindow::setPresentable(bool presentable) {
  presentable_ = presentable;
  updateVisibility();
}

void QsWindow::retire() {
  // host_ stays: ~QsWindow still calls release() to drop the entry.
  if (host_) host_->retire(this);
  setPresentable(false);
}

void QsWindow::updateFlags() {
  if (!window_) return;
  Qt::WindowFlags flags = windowFlags();
  // The mask poller owns this flag while the window is click-through.
  if (poller_ && poller_->passThrough()) flags |= Qt::WindowTransparentForInput;
  window_->setFlags(flags);
}

void QsWindow::stackingChanged() {
  if (host_ && window_ && window_->isVisible()) host_->windowStackingChanged(this);
}

void QsWindow::hideNow() {
  hideTimer_.stop();
  wasActive_ = false;
  if (!window_ || !window_->isVisible()) return;
  window_->hide();
  emit hidden();
}

void QsWindow::updateVisibility() {
  if (!window_) return;
  const bool ready = complete_ && presentable_ && buildError_.isEmpty();
  const bool show = ready && visible_;
  if (show) hideTimer_.stop();
  if (show == window_->isVisible()) return;
  if (!show) {
    // The user hid it: the content gets `hideDelay` to animate out. Any other reason (the shell
    // goes away, the window broke) takes it off the screen at once.
    if (ready && hideDelay_ > 0) {
      if (!hideTimer_.isActive()) hideTimer_.start(hideDelay_);
      return;
    }
    hideNow();
    return;
  }
  applyGeometry();
  // Before show(): the platform can activate the window while it shows.
  wasActive_ = false;
  window_->show();
  afterShow();
  if (!shownOnce_) {
    shownOnce_ = true;
    emit firstShown();
  }
  const bool activate = activateOnShow_ && takesFocus();
  activateOnShow_ = false;
  if (host_) host_->windowShown(this, activate);
  emit shown();
}

void QsWindow::componentComplete() {
  complete_ = true;
  updateFlags();
  applyGeometry();
  buildChrome();
  if (!buildContent()) {
    qWarning().noquote() << "wabar: a window could not be built:" << buildError_;
  }
  host_ = ShellHost::of(qmlEngine(this));
  emit hostChanged();
  if (host_) {
    host_->adopt(this);
    updateMaskPoller();
  } else {
    setPresentable(true);
  }
}

void QsWindow::buildChrome() {
  QQmlEngine* engine = qmlEngine(this);
  if (!engine) return;
  QQmlComponent component(engine, QUrl(QString::fromLatin1(kChromeUrl)));
  QObject* object = component.beginCreate(qmlContext(this));
  auto* chrome = qobject_cast<QQuickItem*>(object);
  if (!chrome) {
    delete object;
    qWarning().noquote() << "wabar: the window chrome is broken:"
                         << describe(component.errors(), QStringLiteral("not an item"));
    return;
  }
  component.setInitialProperties(chrome,
                                 {{QStringLiteral("shellWindow"), QVariant::fromValue(this)}});
  chrome->setParentItem(contentItem());
  chrome->setParent(contentItem());
  component.completeCreate();
}

bool QsWindow::buildContent() {
  if (!content_) return true;
  QQmlContext* context = content_->creationContext();
  if (!context) context = qmlContext(this);
  // One component is built into every window of a Panel, and a component keeps the errors of its
  // last creation, so only new errors count.
  const qsizetype before = content_->errors().size();
  QObject* object = content_->beginCreate(context);
  if (!object) {
    buildError_ =
        describe(content_->errors(), QStringLiteral("the window content could not be created"));
    return false;
  }
  if (auto* item = qobject_cast<QQuickItem*>(object)) {
    item->setParentItem(contentItem());
    fill(item, contentItem());
  }
  object->setParent(contentItem());
  const QPointer<QObject> guard(object);
  content_->completeCreate();
  if (content_->errors().size() > before) {
    buildError_ = describe(content_->errors().mid(before),
                           QStringLiteral("the window content could not be created"));
    delete guard.data();
    return false;
  }
  return true;
}

// ---- QsWindowAttached ----

QsWindowAttached::QsWindowAttached(QObject* attachee) : QObject(attachee) {
  // The attachee is an item, a window declaration, or an object declared inside an item.
  for (QObject* o = attachee; o; o = o->parent()) {
    if (auto* window = qobject_cast<QsWindow*>(o)) {
      window_ = window;
      return;
    }
    if (auto* item = qobject_cast<QQuickItem*>(o)) {
      item_ = item;
      connect(item, &QQuickItem::windowChanged, this, &QsWindowAttached::update);
      update();
      return;
    }
  }
}

void QsWindowAttached::update() {
  QsWindow* window = item_ ? QsWindow::of(item_->window()) : nullptr;
  if (window == window_) return;
  window_ = window;
  emit windowChanged();
}

QPointF QsWindowAttached::itemPosition(QQuickItem* item) const {
  return window_ ? window_->itemPosition(item) : QPointF();
}

QRectF QsWindowAttached::itemRect(QQuickItem* item) const {
  return window_ ? window_->itemRect(item) : QRectF();
}

QPointF QsWindowAttached::mapFromItem(QQuickItem* item, qreal x, qreal y) const {
  return window_ ? window_->mapFromItem(item, x, y) : QPointF();
}

// ---- PanelWindow ----

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

// ---- FloatingWindow ----

FloatingWindow::FloatingWindow(QObject* parent) : QsWindow(parent) {}

void FloatingWindow::setTitle(const QString& title) {
  if (title_ == title) return;
  title_ = title;
  if (QQuickWindow* window = backingWindow()) window->setTitle(title);
  emit titleChanged();
}

void FloatingWindow::setMinimumSize(const QSize& size) {
  if (minimumSize_ == size) return;
  minimumSize_ = size;
  if (QQuickWindow* window = backingWindow()) window->setMinimumSize(size);
  emit minimumSizeChanged();
}

void FloatingWindow::setMaximumSize(const QSize& size) {
  if (maximumSize_ == size) return;
  maximumSize_ = size;
  if (QQuickWindow* window = backingWindow(); window && size.isValid()) {
    window->setMaximumSize(size);
  }
  emit maximumSizeChanged();
}

void FloatingWindow::applyGeometry() {
  QQuickWindow* window = backingWindow();
  if (!window) return;
  const QSize size(std::max(1, implicitWidth()), std::max(1, implicitHeight()));
  if (placed_) {
    window->resize(size);
    return;
  }
  QScreen* screen = targetScreen();
  if (!screen) return;
  if (window->screen() != screen) window->setScreen(screen);
  const QRect area = screen->availableGeometry();
  window->setGeometry(QRect(area.x() + (area.width() - size.width()) / 2,
                            area.y() + (area.height() - size.height()) / 2, size.width(),
                            size.height()));
}

Qt::WindowFlags FloatingWindow::windowFlags() const { return Qt::Window; }

// ---- WlrLayershell ----

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
