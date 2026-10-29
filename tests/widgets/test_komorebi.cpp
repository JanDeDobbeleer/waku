#include <windows.h>
#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "core/bar/theme_class.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"
#include "platform/komorebi.h"
#include "platform/window_system.h"
#include "widgets/komorebi/active_layout_widget.h"
#include "widgets/komorebi/komorebi_service.h"
#include "widgets/komorebi/komorebi_state.h"
#include "widgets/komorebi/komorebi_widgets.h"
#include "widgets/komorebi/workspaces_widget.h"
#include "widgets/menu_support/menu_popup.h"

using nlohmann::json;
using namespace yasb::widgets;
namespace komo = yasb::widgets::komorebi;
namespace platform = yasb::platform;

namespace {

constexpr long long kMonitorId = 65537;  // MONITOR_ID in tools/parity/vec_komorebi.py

komo::JsonPtr ptr(const json& value) { return std::make_shared<const json>(value); }

json labelState(const QLabel* label) {
  return {{"class", label->property("class").toString().toStdString()},
          {"hidden", label->isHidden()},
          {"text", label->text().toStdString()}};
}

// Records the komorebic commands a widget publishes on the bus.
class CommandRecorder {
 public:
  explicit CommandRecorder(EventBus& bus) {
    QObject::connect(&bus, &EventBus::event, &receiver_,
                     [this](const QString& name, const QVariantMap& payload) {
                       if (name != komo::kCommandEvent) return;
                       for (const QVariant& entry : payload.value("commands").toList()) {
                         json args = json::array();
                         for (const QString& arg : entry.toStringList()) {
                           args.push_back(arg.toStdString());
                         }
                         commands_.push_back(args);
                       }
                     });
  }
  json take() {
    json result = commands_;
    commands_ = json::array();
    return result;
  }

 private:
  QObject receiver_;
  json commands_ = json::array();
};

json workspacesSnapshot(KomorebiWorkspacesWidget& widget) {
  json buttons = json::array();
  for (const WorkspaceButtonBase* button : widget.buttons()) {
    buttons.push_back({{"class", button->widget()->property("class").toString().toStdString()},
                       {"hidden", button->widget()->isHidden()},
                       {"index", button->workspaceIndex()},
                       {"text", button->labelText().toStdString()}});
  }
  return {{"buttons", buttons},
          {"container_class", widget.labelContainer()->property("class").toString().toStdString()},
          {"container_hidden", widget.labelContainer()->isHidden()},
          {"float_override", labelState(widget.floatOverrideLabel())},
          {"frame_class", widget.widgetFrame()->property("class").toString().toStdString()},
          {"layer",
           widget.workspaceLayerLabel() ? labelState(widget.workspaceLayerLabel()) : json(nullptr)},
          {"offline", labelState(widget.offlineLabel())},
          {"widget_hidden", widget.isHidden()}};
}

json layoutSnapshot(KomorebiActiveLayoutWidget& widget) {
  return {{"container_class", widget.labelContainer()->property("class").toString().toStdString()},
          {"frame_class", widget.widgetFrame()->property("class").toString().toStdString()},
          {"label", labelState(widget.label())},
          {"widget_hidden", widget.isHidden()}};
}

json loadVectors(const std::string& file) {
  std::ifstream in(std::string(YASB_VECTORS_DIR) + "/labels/" + file, std::ios::binary);
  EXPECT_TRUE(in.good()) << file;
  return json::parse(in);
}

}  // namespace

class KomorebiVectorTest : public testing::Test {
 protected:
  WidgetContext context(const char* name, const char* type, TypedWidgetOptions typed) {
    return {name,
            type,
            json::object(),
            {},
            {},
            eventBus_,
            globalState_,
            pollingWorkers_,
            std::move(typed)};
  }

  EventBus eventBus_;
  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
};

TEST_F(KomorebiVectorTest, WorkspacesReplayPythonVectors) {
  const json cases = loadVectors("komorebi_workspaces.json");
  ASSERT_GT(cases.size(), 0u);
  for (const json& c : cases) {
    SCOPED_TRACE(c.at("scenario").get<std::string>());
    EventBus bus;
    CommandRecorder recorder(bus);
    WidgetContext ctx{
        "komorebi_workspaces",
        "komorebi.workspaces.WorkspaceWidget",
        c.at("options"),
        {},
        {},
        bus,
        globalState_,
        pollingWorkers_,
        c.at("normalized_options").get<yasb::config::options::KomorebiWorkspacesConfig>()};
    KomorebiWorkspacesWidget widget(ctx, /*subscribe=*/false);
    widget.setMonitorResolver([] { return std::optional<long long>(kMonitorId); });
    widget.show();
    EXPECT_EQ(workspacesSnapshot(widget), c.at("initial")) << workspacesSnapshot(widget).dump(1);
    recorder.take();

    size_t index = 0;
    for (const json& s : c.at("steps")) {
      const json& step = s.at("step");
      SCOPED_TRACE("step " + std::to_string(index++) + " " + step.dump().substr(0, 120));
      if (step.contains("connect")) {
        widget.onConnect(ptr(step.at("connect")));
      } else if (step.contains("update")) {
        const json& data = step.at("update");
        widget.onUpdate(ptr(data.at("event")), ptr(data.at("state")));
      } else if (step.contains("disconnect")) {
        widget.onDisconnect();
      } else if (step.contains("click")) {
        widget.buttons().at(step.at("click").get<size_t>())->activate();
      } else if (step.contains("wheel")) {
        widget.scrollWorkspaces(step.at("wheel").get<int>());
      } else {
        FAIL() << "unknown step";
      }
      EXPECT_EQ(workspacesSnapshot(widget), s.at("snapshot")) << workspacesSnapshot(widget).dump(1);
      EXPECT_EQ(recorder.take(), s.at("commands"));
    }
  }
}

TEST_F(KomorebiVectorTest, ActiveLayoutReplaysPythonVectors) {
  const json cases = loadVectors("komorebi_active_layout.json");
  ASSERT_GT(cases.size(), 0u);
  for (const json& c : cases) {
    SCOPED_TRACE(c.at("scenario").get<std::string>());
    EventBus bus;
    CommandRecorder recorder(bus);
    WidgetContext ctx{"komorebi_active_layout",
                      "komorebi.active_layout.ActiveLayoutWidget",
                      c.at("options"),
                      {},
                      {},
                      bus,
                      globalState_,
                      pollingWorkers_,
                      c.at("normalized_options").get<yasb::config::options::ActiveLayoutConfig>()};
    KomorebiActiveLayoutWidget widget(ctx, /*subscribe=*/false);
    widget.setMonitorResolver([] { return std::optional<long long>(kMonitorId); });
    widget.show();
    EXPECT_EQ(layoutSnapshot(widget), c.at("initial")) << layoutSnapshot(widget).dump(1);
    recorder.take();

    size_t index = 0;
    for (const json& s : c.at("steps")) {
      const json& step = s.at("step");
      SCOPED_TRACE("step " + std::to_string(index++) + " " + step.dump().substr(0, 120));
      if (step.contains("connect")) {
        widget.onConnect(ptr(step.at("connect")));
      } else if (step.contains("update")) {
        const json& data = step.at("update");
        widget.onUpdate(ptr(data.at("event")), ptr(data.at("state")));
      } else if (step.contains("disconnect")) {
        widget.onDisconnect();
      } else if (step.contains("callback")) {
        widget.runCallback(QString::fromStdString(step.at("callback").get<std::string>()));
      } else {
        FAIL() << "unknown step";
      }
      EXPECT_EQ(layoutSnapshot(widget), s.at("snapshot")) << layoutSnapshot(widget).dump(1);
      EXPECT_EQ(recorder.take(), s.at("commands"));
    }
  }
}

TEST_F(KomorebiVectorTest, WidgetsAreRegisteredAndGoThroughTheBus) {
  WidgetRegistry registry;
  registerKomorebiWidgets(registry);
  EXPECT_TRUE(registry.contains("komorebi.workspaces.WorkspaceWidget"));
  EXPECT_TRUE(registry.contains("komorebi.active_layout.ActiveLayoutWidget"));

  // Bus events (as published by KomorebiService) reach the widget, subscribing asks for state.
  const json cases = loadVectors("komorebi_workspaces.json");
  const json& c = cases.at(0);
  int subscribes = 0;
  QObject::connect(&eventBus_, &EventBus::event, [&](const QString& name, const QVariantMap&) {
    if (name == komo::kSubscribeEvent) ++subscribes;
  });
  WidgetContext ctx =
      context("w", "komorebi.workspaces.WorkspaceWidget",
              c.at("normalized_options").get<yasb::config::options::KomorebiWorkspacesConfig>());
  KomorebiWorkspacesWidget widget(ctx);
  EXPECT_EQ(subscribes, 1);
  widget.setMonitorResolver([] { return std::optional<long long>(kMonitorId); });
  const json& step = c.at("steps").at(0).at("step");
  emit eventBus_.event(
      komo::kStateEvent,
      {{"kind", "connect"}, {"state", QVariant::fromValue(ptr(step.at("connect")))}});
  EXPECT_EQ(widget.buttons().size(), 3u);
  emit eventBus_.event(komo::kStateEvent, {{"kind", "disconnect"}});
  EXPECT_TRUE(widget.isHidden());
}

// --- app icons, pending state and the layout menu -----------------------------------------

namespace {

// A window system that answers icon and pid queries from tables; nothing touches the desktop.
class IconSystem final : public platform::WindowSystem {
 public:
  bool isWindow(platform::WindowHandle) const override { return true; }
  bool isIconic(platform::WindowHandle) const override { return false; }
  platform::WindowHandle foregroundWindow() const override { return 0; }
  std::vector<platform::WindowHandle> enumerateWindows() const override { return {}; }
  platform::WindowInfo query(platform::WindowHandle hwnd) const override {
    platform::WindowInfo info;
    info.hwnd = hwnd;
    const auto found = pids.find(hwnd);
    info.pid = found != pids.end() ? found->second : static_cast<std::uint32_t>(hwnd);
    return info;
  }
  bool isTaskbarWindow(const platform::WindowInfo&) const override { return true; }
  bool canMinimize(platform::WindowHandle) const override { return true; }
  platform::WindowHandle monitorOfWindow(platform::WindowHandle) const override { return 0; }
  QRect windowRect(platform::WindowHandle) const override { return {}; }
  std::pair<platform::WindowHandle, platform::WindowHandle> resolveBaseAndFocus(
      platform::WindowHandle hwnd) const override {
    return {hwnd, hwnd};
  }
  bool isOwnerRootActive(platform::WindowHandle) const override { return false; }
  std::optional<QString> appUniqueId(const platform::WindowInfo&) const override {
    return std::nullopt;
  }
  platform::IconBitmap windowIcon(platform::WindowHandle hwnd, int size) const override {
    platform::IconBitmap bitmap;
    if (noIcon.count(hwnd)) return bitmap;
    bitmap.width = bitmap.height = size;
    bitmap.bgra = QByteArray(size * size * 4, '\x80');
    return bitmap;
  }
  void restoreWindow(platform::WindowHandle) override {}
  void minimizeWindow(platform::WindowHandle) override {}
  void showWindow(platform::WindowHandle) override {}
  void setForeground(platform::WindowHandle) override {}
  void closeApplication(platform::WindowHandle, bool) override {}

  std::map<platform::WindowHandle, std::uint32_t> pids;
  std::set<platform::WindowHandle> noIcon;
};

json iconWindow(long long hwnd) { return {{"hwnd", hwnd}, {"title", ""}}; }

json iconWorkspace(const std::string& name, std::vector<long long> tiled,
                   std::vector<long long> floating = {}) {
  json containers = json::array();
  for (const long long hwnd : tiled) {
    containers.push_back({{"windows", {{"elements", {iconWindow(hwnd)}}, {"focused", 0}}}});
  }
  json floats = json::array();
  for (const long long hwnd : floating) floats.push_back(iconWindow(hwnd));
  return {{"name", name},
          {"containers", {{"elements", containers}, {"focused", 0}}},
          {"floating_windows", {{"elements", floats}}},
          {"monocle_container", nullptr},
          {"maximized_window", nullptr},
          {"tile", true},
          {"layout", {{"Default", "BSP"}}},
          {"layer", "Tiling"}};
}

json iconState(const std::vector<json>& workspaces, int focused = 0) {
  return {
      {"is_paused", false},
      {"float_override", false},
      {"monitors",
       {{"elements",
         {{{"id", kMonitorId}, {"workspaces", {{"elements", workspaces}, {"focused", focused}}}}}},
        {"focused", 0}}}};
}

json iconEvent(const std::string& type, json content = nullptr) {
  return {{"type", type}, {"content", content}};
}

std::vector<std::string> iconClasses(const WorkspaceIconButton* button) {
  std::vector<std::string> classes;
  for (const QLabel* label : button->iconLabels()) {
    classes.push_back(label->property("class").toString().toStdString());
  }
  return classes;
}

}  // namespace

class KomorebiIconsTest : public KomorebiVectorTest {
 protected:
  void build(const json& appIcons) {
    const json cases = loadVectors("komorebi_workspaces.json");
    json options = cases.at(0).at("normalized_options");
    options["app_icons"] = appIcons;
    ctx_ = std::make_unique<WidgetContext>(
        context("komorebi_workspaces", "komorebi.workspaces.WorkspaceWidget",
                options.get<yasb::config::options::KomorebiWorkspacesConfig>()));
    widget_ = std::make_unique<KomorebiWorkspacesWidget>(*ctx_, /*subscribe=*/false);
    system_ = std::make_shared<IconSystem>();
    widget_->setWindowSystem(system_, /*async=*/false);
    widget_->setMonitorResolver([] { return std::optional<long long>(kMonitorId); });
    widget_->show();
  }

  WorkspaceIconButton* iconButton(size_t index) {
    return dynamic_cast<WorkspaceIconButton*>(widget_->buttons().at(index));
  }

  std::unique_ptr<WidgetContext> ctx_;
  std::unique_ptr<KomorebiWorkspacesWidget> widget_;
  std::shared_ptr<IconSystem> system_;
};

TEST_F(KomorebiIconsTest, ButtonsShowOneIconPerWindow) {
  build({{"enabled_populated", true}, {"enabled_active", true}, {"size", 16}});
  widget_->onConnect(ptr(iconState(
      {iconWorkspace("I", {10, 11}), iconWorkspace("II", {12}), iconWorkspace("III", {})}, 0)));
  ASSERT_EQ(widget_->buttons().size(), 3u);
  ASSERT_NE(iconButton(0), nullptr);
  EXPECT_EQ(iconClasses(iconButton(0)), (std::vector<std::string>{"icon icon-1", "icon icon-2"}));
  EXPECT_EQ(iconClasses(iconButton(1)), (std::vector<std::string>{"icon icon-1"}));
  EXPECT_TRUE(iconClasses(iconButton(2)).empty());
  EXPECT_EQ(iconButton(0)->widget()->property("class").toString(),
            QStringLiteral("ws-btn active button-1"));
  EXPECT_EQ(iconButton(1)->widget()->property("class").toString(),
            QStringLiteral("ws-btn populated button-2"));
  EXPECT_EQ(iconButton(0)->textLabel()->property("class").toString(), QStringLiteral("label"));
  EXPECT_FALSE(iconButton(0)->textLabel()->isHidden());
  EXPECT_FALSE(iconButton(0)->iconLabels().front()->pixmap().isNull());
  EXPECT_EQ(iconButton(0)->iconLabels().front()->pixmap().size(), QSize(16, 16));
}

TEST_F(KomorebiIconsTest, MaxIconsAndHideLabel) {
  build({{"enabled_populated", true},
         {"enabled_active", true},
         {"max_icons", 1},
         {"hide_label", true}});
  widget_->onConnect(ptr(iconState({iconWorkspace("I", {10, 11}), iconWorkspace("II", {})}, 0)));
  EXPECT_EQ(iconButton(0)->iconLabels().size(), 1u);
  EXPECT_TRUE(iconButton(0)->textLabel()->isHidden());
  // No icons: the label stays visible even with hide_label.
  EXPECT_TRUE(iconButton(1)->iconLabels().empty());
  EXPECT_FALSE(iconButton(1)->textLabel()->isHidden());
}

TEST_F(KomorebiIconsTest, EnabledActiveAndPopulatedGateTheIcons) {
  build({{"enabled_populated", true}, {"enabled_active", false}});
  widget_->onConnect(ptr(iconState({iconWorkspace("I", {10}), iconWorkspace("II", {12})}, 0)));
  EXPECT_TRUE(iconClasses(iconButton(0)).empty());  // focused workspace
  EXPECT_EQ(iconClasses(iconButton(1)).size(), 1u);

  // Moving focus swaps which workspace shows icons.
  widget_->onUpdate(ptr(iconEvent("FocusWorkspaceNumber", 1)),
                    ptr(iconState({iconWorkspace("I", {10}), iconWorkspace("II", {12})}, 1)));
  EXPECT_EQ(iconClasses(iconButton(0)).size(), 1u);
  EXPECT_TRUE(iconClasses(iconButton(1)).empty());
}

TEST_F(KomorebiIconsTest, HideDuplicatesKeepsOneIconPerProcess) {
  build({{"enabled_populated", true}, {"enabled_active", true}, {"hide_duplicates", true}});
  system_->pids = {{10, 7}, {11, 7}, {12, 8}};
  widget_->onConnect(ptr(iconState({iconWorkspace("I", {10, 11, 12})}, 0)));
  EXPECT_EQ(iconClasses(iconButton(0)).size(), 2u);
}

TEST_F(KomorebiIconsTest, HideFloatingSkipsFloatingWindows) {
  build({{"enabled_populated", true}, {"enabled_active", true}, {"hide_floating", true}});
  widget_->onConnect(ptr(iconState({iconWorkspace("I", {10}, {99})}, 0)));
  EXPECT_EQ(iconClasses(iconButton(0)).size(), 1u);

  build({{"enabled_populated", true}, {"enabled_active", true}, {"hide_floating", false}});
  widget_->onConnect(ptr(iconState({iconWorkspace("I", {10}, {99})}, 0)));
  EXPECT_EQ(iconClasses(iconButton(0)).size(), 2u);
}

TEST_F(KomorebiIconsTest, WindowsWithoutAnIconAreSkipped) {
  build({{"enabled_populated", true}, {"enabled_active", true}});
  system_->noIcon = {11};
  widget_->onConnect(ptr(iconState({iconWorkspace("I", {10, 11})}, 0)));
  EXPECT_EQ(iconClasses(iconButton(0)).size(), 1u);
}

TEST_F(KomorebiIconsTest, WindowCountChangeRefreshesTheIcons) {
  build({{"enabled_populated", true}, {"enabled_active", true}});
  widget_->onConnect(ptr(iconState({iconWorkspace("I", {10}), iconWorkspace("II", {12})}, 0)));
  ASSERT_EQ(iconClasses(iconButton(1)).size(), 1u);
  widget_->onUpdate(ptr(iconEvent("MoveContainerToWorkspaceNumber", 1)),
                    ptr(iconState({iconWorkspace("I", {}), iconWorkspace("II", {12, 10})}, 0)));
  EXPECT_TRUE(iconClasses(iconButton(0)).empty());
  EXPECT_EQ(iconClasses(iconButton(1)).size(), 2u);
}

TEST_F(KomorebiIconsTest, LeftPressActivatesTheWorkspace) {
  build({{"enabled_populated", true}, {"enabled_active", true}});
  CommandRecorder recorder(eventBus_);
  widget_->onConnect(ptr(iconState({iconWorkspace("I", {10}), iconWorkspace("II", {12})}, 0)));
  recorder.take();
  QWidget* second = iconButton(1)->widget();
  QMouseEvent press(QEvent::MouseButtonPress, QPointF(2, 2), QPointF(2, 2), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(second, &press);
  EXPECT_EQ(recorder.take(), json::array({json::array({"focus-monitor-workspace", "0", "1"})}));
  QMouseEvent right(QEvent::MouseButtonPress, QPointF(2, 2), QPointF(2, 2), Qt::RightButton,
                    Qt::RightButton, Qt::NoModifier);
  QApplication::sendEvent(second, &right);
  EXPECT_EQ(recorder.take(), json::array());
}

TEST_F(KomorebiIconsTest, DisabledAppIconsKeepPlainButtons) {
  build({{"enabled_populated", false}, {"enabled_active", false}});
  widget_->onConnect(ptr(iconState({iconWorkspace("I", {10})}, 0)));
  EXPECT_NE(dynamic_cast<WorkspaceButton*>(widget_->buttons().at(0)), nullptr);
}

TEST_F(KomorebiIconsTest, StateBeforeTheWidgetHasAWindowIsReplayedOnceItDoes) {
  build({{"enabled_populated", false}, {"enabled_active", false}});
  bool inWindow = false;
  widget_->setMonitorResolver(
      [&] { return inWindow ? std::optional<long long>(kMonitorId) : std::optional<long long>(); });
  widget_->onConnect(ptr(iconState({iconWorkspace("I", {10}), iconWorkspace("II", {})}, 0)));
  EXPECT_TRUE(widget_->buttons().empty());
  EXPECT_TRUE(widget_->hasPendingState());

  inWindow = true;
  QEvent parentChange(QEvent::ParentChange);
  QApplication::sendEvent(widget_.get(), &parentChange);
  EXPECT_FALSE(widget_->hasPendingState());
  EXPECT_EQ(widget_->buttons().size(), 2u);

  // The newest state wins when several arrive before there is a window.
  inWindow = false;
  widget_->onConnect(ptr(iconState({iconWorkspace("I", {10})}, 0)));
  widget_->onUpdate(
      ptr(iconEvent("NewWorkspace")),
      ptr(iconState({iconWorkspace("I", {10}), iconWorkspace("II", {}), iconWorkspace("III", {})},
                    0)));
  inWindow = true;
  QApplication::sendEvent(widget_.get(), &parentChange);
  EXPECT_EQ(widget_->buttons().size(), 3u);
}

TEST_F(KomorebiVectorTest, ActiveLayoutReplaysStateAfterItGetsAWindow) {
  const json cases = loadVectors("komorebi_active_layout.json");
  const json& c = cases.at(0);
  WidgetContext ctx =
      context("komorebi_active_layout", "komorebi.active_layout.ActiveLayoutWidget",
              c.at("normalized_options").get<yasb::config::options::ActiveLayoutConfig>());
  KomorebiActiveLayoutWidget widget(ctx, /*subscribe=*/false);
  bool inWindow = false;
  widget.setMonitorResolver(
      [&] { return inWindow ? std::optional<long long>(kMonitorId) : std::optional<long long>(); });
  widget.onConnect(ptr(iconState({iconWorkspace("I", {10})}, 0)));
  EXPECT_TRUE(widget.label()->text().isEmpty());
  inWindow = true;
  QEvent parentChange(QEvent::ParentChange);
  QApplication::sendEvent(&widget, &parentChange);
  EXPECT_FALSE(widget.label()->text().isEmpty());
}

TEST_F(KomorebiVectorTest, LayoutMenuIsAPlainPopupThatTogglesAndIsDeletedWhenHidden) {
  const json cases = loadVectors("komorebi_active_layout.json");
  const json& c = cases.at(0);
  WidgetContext ctx =
      context("komorebi_active_layout", "komorebi.active_layout.ActiveLayoutWidget",
              c.at("normalized_options").get<yasb::config::options::ActiveLayoutConfig>());
  KomorebiActiveLayoutWidget widget(ctx, /*subscribe=*/false);
  widget.setMonitorResolver([] { return std::optional<long long>(kMonitorId); });
  widget.onConnect(ptr(iconState({iconWorkspace("I", {10})}, 0)));
  widget.show();

  widget.runCallback(QStringLiteral("toggle_layout_menu"));
  auto* menu = widget.findChild<MenuPopup*>();
  ASSERT_NE(menu, nullptr);
  EXPECT_TRUE(menu->isVisible());
  EXPECT_TRUE(menu->windowFlags().testFlag(Qt::Popup));
  EXPECT_EQ(menu->property("class").toString(), QStringLiteral("komorebi-layout-menu"));

  // Opening it again closes the open one instead of stacking another.
  widget.runCallback(QStringLiteral("toggle_layout_menu"));
  EXPECT_TRUE(menu->isClosing());
  EXPECT_EQ(widget.findChildren<MenuPopup*>().size(), 1);

  menu->hideNow();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  EXPECT_EQ(widget.findChild<MenuPopup*>(), nullptr);
}
