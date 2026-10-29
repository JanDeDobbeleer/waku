#include "spike/services/komorebi_client.h"

#include <QDebug>
#include <QHash>

#include "widgets/komorebi/komorebi_state.h"

namespace wabar::services {

namespace kmb = widgets::komorebi;

namespace {

QString stringOf(const kmb::Json* node, std::string_view key) {
  const kmb::Json* value = kmb::child(node, key);
  return value && value->is_string() ? QString::fromStdString(value->get<std::string>())
                                     : QString();
}

// komorebi's layout name -> snake_case layout key (the `layout_icons` names of Python YASB).
QString layoutKey(const QString& name) {
  static const QHash<QString, QString> keys = {
      {QStringLiteral("BSP"), QStringLiteral("bsp")},
      {QStringLiteral("Columns"), QStringLiteral("columns")},
      {QStringLiteral("Rows"), QStringLiteral("rows")},
      {QStringLiteral("Grid"), QStringLiteral("grid")},
      {QStringLiteral("Scrolling"), QStringLiteral("scrolling")},
      {QStringLiteral("VerticalStack"), QStringLiteral("vertical_stack")},
      {QStringLiteral("HorizontalStack"), QStringLiteral("horizontal_stack")},
      {QStringLiteral("UltrawideVerticalStack"), QStringLiteral("ultrawide_vertical_stack")},
      {QStringLiteral("RightMainVerticalStack"), QStringLiteral("right_main_vertical_stack")}};
  return keys.value(name, QStringLiteral("custom"));
}

// What the workspace shows, in the order Python YASB checks: paused, floating (tiling off),
// maximized, monocle, else the tiling layout.
QString layoutOf(const kmb::Json& state, const kmb::Json* workspace) {
  if (kmb::truthy(kmb::child(&state, "is_paused"))) return QStringLiteral("paused");
  if (!kmb::truthy(kmb::child(workspace, "tile"))) return QStringLiteral("floating");
  if (kmb::truthy(kmb::child(workspace, "maximized_window"))) return QStringLiteral("maximized");
  if (kmb::truthy(kmb::child(workspace, "monocle_container"))) return QStringLiteral("monocle");
  return layoutKey(stringOf(kmb::child(workspace, "layout"), "Default"));
}

QVariantList toVariant(const kmb::Json& state) {
  QVariantList list;
  const kmb::Json* screens = kmb::elements(&state, "monitors");
  if (!screens || !screens->is_array()) return list;
  const std::optional<long long> focusedMonitor =
      kmb::integer(kmb::child(kmb::child(&state, "monitors"), "focused"));
  int monitorIndex = 0;
  for (const kmb::Json& screen : *screens) {
    const std::optional<int> focused = kmb::focusedWorkspaceIndex(&screen);
    QVariantList workspaces;
    const kmb::Json* shown = focused ? kmb::workspaceAt(&screen, *focused) : nullptr;
    for (int i = 0; i < kmb::workspaceCount(&screen); ++i) {
      const kmb::Json* workspace = kmb::workspaceAt(&screen, i);
      workspaces << QVariantMap{
          {QStringLiteral("index"), i},
          {QStringLiteral("name"), stringOf(workspace, "name")},
          {QStringLiteral("focused"), focused == i},
          {QStringLiteral("hasWindows"), kmb::hasWindows(workspace)},
          {QStringLiteral("windowCount"), kmb::windowCount(workspace, false)}};
    }
    list << QVariantMap{
        {QStringLiteral("index"), monitorIndex},
        {QStringLiteral("focused"), focusedMonitor == monitorIndex++},
        {QStringLiteral("id"),
         static_cast<qlonglong>(kmb::integer(kmb::child(&screen, "id")).value_or(0))},
        {QStringLiteral("name"), stringOf(&screen, "name")},
        {QStringLiteral("workspace"), focused.value_or(-1)},
        {QStringLiteral("layout"), shown ? layoutOf(state, shown) : QString()},
        {QStringLiteral("workspaces"), workspaces}};
  }
  return list;
}

}  // namespace

Komorebi::Komorebi(QObject* parent) : QObject(parent), service_(bus_) { subscribe(); }

Komorebi::Komorebi(platform::KomorebiBackend backend, kmb::KomorebiService::CommandRunner runner,
                   QString pipeName, platform::KomorebiPipeListener::Timing timing, QObject* parent)
    : QObject(parent),
      service_(bus_, std::move(backend), std::move(runner), std::move(pipeName), timing) {
  subscribe();
}

void Komorebi::subscribe() {
  connect(&bus_, &widgets::EventBus::event, this, &Komorebi::onEvent);
  emit bus_.event(kmb::kSubscribeEvent, {});
}

Komorebi::~Komorebi() { service_.stop(); }

void Komorebi::onEvent(const QString& name, const QVariantMap& payload) {
  if (name != kmb::kStateEvent) return;
  try {
    const QString kind = payload.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("disconnect")) {
      connected_ = false;
      monitors_.clear();
    } else if (kind == QStringLiteral("connect") || kind == QStringLiteral("update")) {
      const kmb::JsonPtr state = kmb::jsonFromPayload(payload, QStringLiteral("state"));
      if (!state) return;
      connected_ = true;
      monitors_ = toVariant(*state);
    } else {
      return;
    }
    emit changed();
  } catch (const std::exception& e) {
    qWarning().noquote() << "komorebi: bad state event:" << e.what();
  }
}

void Komorebi::focusWorkspace(int monitorIndex, int workspaceIndex) {
  kmb::sendCommand(bus_, {QStringLiteral("focus-monitor-workspace"), QString::number(monitorIndex),
                          QString::number(workspaceIndex)});
}

void Komorebi::setLayout(int monitorIndex, QString layout) {
  const QVariantMap monitor = monitorAt(monitorIndex);
  const int workspace = monitor.value(QStringLiteral("workspace"), -1).toInt();
  if (workspace < 0) return;
  kmb::sendCommand(bus_, {QStringLiteral("workspace-layout"), QString::number(monitorIndex),
                          QString::number(workspace), layout.replace('_', '-')});
}

void Komorebi::cycleLayout(int monitorIndex, QStringList layouts, int step) {
  const QVariantMap monitor = monitorAt(monitorIndex);
  if (monitor.isEmpty()) return;
  const QString current = monitor.value(QStringLiteral("layout")).toString();
  // Like Python YASB: while paused, floating, maximized or in monocle, the click undoes that
  // state instead of changing the layout.
  static const QHash<QString, QString> blocking = {
      {QStringLiteral("paused"), QStringLiteral("pause")},
      {QStringLiteral("floating"), QStringLiteral("tiling")},
      {QStringLiteral("maximized"), QStringLiteral("maximize")},
      {QStringLiteral("monocle"), QStringLiteral("monocle")}};
  const auto toggle = blocking.constFind(current);
  if (toggle != blocking.cend()) {
    kmb::sendCommandChain(
        bus_, {{QStringLiteral("focus-monitor-at-cursor")}, {QStringLiteral("toggle-") + *toggle}});
    return;
  }
  if (layouts.isEmpty()) return;
  for (QString& layout : layouts) layout.replace('-', '_');
  // A layout outside the list (or a custom one) counts as bsp, as in Python YASB.
  const qsizetype at =
      layouts.indexOf(current == QStringLiteral("custom") ? QStringLiteral("bsp") : current);
  const qsizetype count = layouts.size();
  const qsizetype next =
      at < 0 ? (step > 0 ? 0 : count - 1) : (((at + step) % count) + count) % count;
  setLayout(monitorIndex, layouts.at(next));
}

QVariantMap Komorebi::monitorAt(int monitorIndex) const {
  for (const QVariant& monitor : monitors_) {
    const QVariantMap map = monitor.toMap();
    if (map.value(QStringLiteral("index")).toInt() == monitorIndex) return map;
  }
  return {};
}

}  // namespace wabar::services
