#include <windows.h>
#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/bar/theme_class.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"
#include "platform/komorebi.h"
#include "widgets/komorebi/active_layout_widget.h"
#include "widgets/komorebi/komorebi_service.h"
#include "widgets/komorebi/komorebi_state.h"
#include "widgets/komorebi/komorebi_widgets.h"
#include "widgets/komorebi/workspaces_widget.h"

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
  for (const WorkspaceButton* button : widget.buttons()) {
    buttons.push_back({{"class", button->property("class").toString().toStdString()},
                       {"hidden", button->isHidden()},
                       {"index", button->workspaceIndex()},
                       {"text", button->text().toStdString()}});
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
        widget.buttons().at(step.at("click").get<size_t>())->click();
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

// --- state helpers ------------------------------------------------------------------------

TEST(KomorebiState, ReadsAreNullSafeOnMalformedState) {
  const json empty = json::object();
  EXPECT_EQ(komo::screenById(empty, 1), nullptr);
  const json odd = {{"monitors", 5}};
  EXPECT_EQ(komo::screenById(odd, 1), nullptr);
  EXPECT_EQ(komo::workspaceCount(nullptr), 0);
  EXPECT_EQ(komo::workspaceAt(nullptr, 0), nullptr);
  EXPECT_FALSE(komo::focusedWorkspaceIndex(&empty).has_value());
  EXPECT_FALSE(komo::hasWindows(&empty));
  EXPECT_EQ(komo::windowCount(&empty, false), 0);

  const json screen = {{"workspaces", {{"elements", {json::object()}}, {"focused", 3}}}};
  EXPECT_FALSE(komo::focusedWorkspaceIndex(&screen).has_value());
}

TEST(KomorebiState, WindowCountsFollowPythonRules) {
  const json workspace = {
      {"containers",
       {{"elements", {{{"windows", {{"elements", {1, 2}}}}}, {{"windows", {{"elements", {3}}}}}}}}},
      {"monocle_container", {{"windows", {{"elements", {4}}}}}},
      {"maximized_window", nullptr},
      {"floating_windows", {{"elements", {5, 6}}}}};
  EXPECT_EQ(komo::windowCount(&workspace, false), 6);
  EXPECT_EQ(komo::windowCount(&workspace, true), 4);
  EXPECT_TRUE(komo::hasWindows(&workspace));

  const json emptyWorkspace = {{"containers", {{"elements", json::array()}}},
                               {"monocle_container", nullptr},
                               {"maximized_window", nullptr},
                               {"floating_windows", {{"elements", json::array()}}}};
  EXPECT_FALSE(komo::hasWindows(&emptyWorkspace));
  const json maximized = {{"containers", {{"elements", json::array()}}},
                          {"maximized_window", {{"hwnd", 7}}},
                          {"floating_windows", {{"elements", json::array()}}}};
  EXPECT_TRUE(komo::hasWindows(&maximized));
}

TEST(KomorebiState, TruthinessMatchesPython) {
  EXPECT_FALSE(komo::truthy(nullptr));
  const json values[] = {nullptr, false, 0, "", json::array(), json::object()};
  for (const json& value : values) EXPECT_FALSE(komo::truthy(&value)) << value.dump();
  const json truthyValues[] = {true, 1, "x", json::array({1}), json::object({{"a", 1}})};
  for (const json& value : truthyValues) EXPECT_TRUE(komo::truthy(&value)) << value.dump();
}

// --- service ------------------------------------------------------------------------------

TEST(KomorebiServiceTest, ShellChainQuotesLikePython) {
  EXPECT_EQ(komo::KomorebiService::shellChain({{"focus-monitor-at-cursor"}, {"toggle-tiling"}}),
            QStringLiteral("\"komorebic.exe\" focus-monitor-at-cursor && \"komorebic.exe\" "
                           "toggle-tiling"));
  const QString name = komo::KomorebiService::uniquePipeName();
  EXPECT_TRUE(name.startsWith(QStringLiteral("yasb-native-")));
  EXPECT_NE(name, komo::KomorebiService::uniquePipeName());
}

TEST(KomorebiServiceTest, HiddenShellRunsQuotedCommandChains) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString out = QDir::toNativeSeparators(dir.filePath(QStringLiteral("chain.txt")));
  // Same shape as the toggle chains: the line starts with a quoted program and contains `&&`.
  const QString chain =
      QStringLiteral("\"cmd.exe\" /c echo first> \"%1\" && \"cmd.exe\" /c echo second>> \"%1\"")
          .arg(out);
  ASSERT_TRUE(platform::launchHiddenShell(chain));
  QFile file(out);
  ASSERT_TRUE(QTest::qWaitFor([&] { return file.exists() && QFileInfo(out).size() >= 14; }, 10000));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll().simplified(), QByteArray("first second"));
}

TEST(KomorebiServiceTest, CommandEventsReachTheRunner) {
  EventBus bus;
  std::vector<std::vector<QStringList>> runs;
  komo::KomorebiService service(
      bus, {}, [&](const std::vector<QStringList>& commands) { runs.push_back(commands); },
      QStringLiteral("yasb-native-unused"), {});
  komo::sendCommand(
      bus, {QStringLiteral("focus-monitor-workspace"), QStringLiteral("0"), QStringLiteral("2")});
  komo::sendCommandChain(
      bus, {{QStringLiteral("focus-monitor-at-cursor")}, {QStringLiteral("toggle-pause")}});
  ASSERT_EQ(runs.size(), 2u);
  EXPECT_EQ(runs[0].size(), 1u);
  EXPECT_EQ(runs[0][0].join(' '), QStringLiteral("focus-monitor-workspace 0 2"));
  EXPECT_EQ(runs[1].size(), 2u);
  EXPECT_FALSE(service.isListening());  // lazy: nothing listens until a widget subscribes
}

namespace {

const char* kStateJson =
    R"({"monitors":{"elements":[{"id":65537,"workspaces":{"elements":[{"name":"I","containers":{"elements":[]},)"
    R"("monocle_container":null,"maximized_window":null,"floating_windows":{"elements":[]},)"
    R"("layout":{"Default":"BSP"},"tile":true}],"focused":0}}],"focused":0},"is_paused":false})";

const char* kEventJson =
    R"({"event":{"type":"FocusWorkspaceNumber","content":0},"state":{"monitors":{"elements":[],"focused":0}}})";

bool writePipe(HANDLE pipe, const std::string& text) {
  DWORD written = 0;
  return WriteFile(pipe, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
         written == text.size();
}

// Plays the role of komorebi: opens the listener's pipe once it exists.
HANDLE openPipeClient(const QString& pipeName, int timeoutMs = 10000) {
  const std::wstring path = L"\\\\.\\pipe\\" + pipeName.toStdWString();
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMs) {
    HANDLE pipe = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) return pipe;
    QTest::qWait(10);
  }
  return INVALID_HANDLE_VALUE;
}

}  // namespace

TEST(KomorebiServiceTest, ListensOnItsOwnPipeReconnectsAndSurvivesMalformedJson) {
  EventBus bus;
  std::mutex lock;
  std::vector<QStringList> calls;
  platform::KomorebiBackend backend;
  backend.isKomorebiRunning = [] { return true; };
  backend.runKomorebic = [&](const QStringList& args, int) {
    std::lock_guard guard(lock);
    calls.push_back(args);
    platform::KomorebicResult result;
    result.started = true;
    result.exitCode = 0;
    if (args.value(0) == QStringLiteral("state")) result.standardOutput = kStateJson;
    return result;
  };
  const auto callCount = [&](const QString& first) {
    std::lock_guard guard(lock);
    int count = 0;
    for (const QStringList& call : calls) count += call.value(0) == first ? 1 : 0;
    return count;
  };

  std::vector<QString> kinds;
  QObject::connect(&bus, &EventBus::event, [&](const QString& name, const QVariantMap& payload) {
    if (name == komo::kStateEvent) kinds.push_back(payload.value("kind").toString());
  });
  const auto seen = [&](const QString& kind) {
    return static_cast<int>(std::count(kinds.begin(), kinds.end(), kind));
  };

  platform::KomorebiPipeListener::Timing timing;
  timing.subscribeRetryMs = 20;
  timing.stateRetryMs = 20;
  timing.reconnectMs = 50;
  const QString pipe =
      QStringLiteral("yasb-native-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  komo::KomorebiService service(bus, backend, nullptr, pipe, timing);
  EXPECT_NE(pipe, komo::KomorebiService::uniquePipeName());

  emit bus.event(komo::kSubscribeEvent, {});
  EXPECT_TRUE(service.isListening());
  HANDLE client = openPipeClient(pipe);
  ASSERT_NE(client, INVALID_HANDLE_VALUE);
  ASSERT_TRUE(QTest::qWaitFor([&] { return seen("connect") == 1; }, 10000));
  EXPECT_TRUE(service.isConnected());

  // The listener asked komorebi to publish to our pipe, and nothing else was contacted.
  {
    std::lock_guard guard(lock);
    ASSERT_GE(calls.size(), 2u);
    EXPECT_EQ(calls[0], (QStringList{"subscribe-pipe", pipe}));
    EXPECT_EQ(calls[1], QStringList{"state"});
  }

  ASSERT_TRUE(writePipe(client, "this is {not json"));
  ASSERT_TRUE(writePipe(client, R"({"event":"just a string","state":{}})"));
  ASSERT_TRUE(writePipe(client, kEventJson));
  ASSERT_TRUE(QTest::qWaitFor([&] { return seen("update") == 1; }, 10000));
  EXPECT_TRUE(service.isListening());

  // A late subscriber (a bar created after connect) is brought up to date.
  emit bus.event(komo::kSubscribeEvent, {});
  ASSERT_TRUE(QTest::qWaitFor([&] { return seen("connect") == 2; }, 10000));

  // komorebi goes away: disconnect, then the listener resubscribes and reconnects.
  CloseHandle(client);
  ASSERT_TRUE(QTest::qWaitFor([&] { return seen("disconnect") >= 1; }, 10000));
  EXPECT_FALSE(service.isConnected());
  ASSERT_TRUE(QTest::qWaitFor([&] { return callCount("subscribe-pipe") >= 2; }, 10000));
  client = openPipeClient(pipe);
  ASSERT_NE(client, INVALID_HANDLE_VALUE);
  ASSERT_TRUE(QTest::qWaitFor([&] { return seen("connect") >= 3; }, 10000));
  EXPECT_TRUE(service.isConnected());

  service.stop();
  EXPECT_FALSE(service.isListening());
  EXPECT_EQ(callCount("unsubscribe-pipe"), 1);
  {
    std::lock_guard guard(lock);
    EXPECT_EQ(calls.back(), (QStringList{"unsubscribe-pipe", pipe}));
  }
  CloseHandle(client);
}

TEST(KomorebiServiceTest, OfflineKomorebiKeepsRetryingUntilStopped) {
  EventBus bus;
  std::atomic<int> subscribeAttempts{0};
  platform::KomorebiBackend backend;
  backend.isKomorebiRunning = [] { return false; };
  backend.runKomorebic = [&](const QStringList& args, int) {
    if (args.value(0) == QStringLiteral("subscribe-pipe")) ++subscribeAttempts;
    return platform::KomorebicResult{};
  };
  platform::KomorebiPipeListener::Timing timing;
  timing.subscribeRetryMs = 10;
  komo::KomorebiService service(bus, backend, nullptr, QStringLiteral("yasb-native-test-offline"),
                                timing);
  emit bus.event(komo::kSubscribeEvent, {});
  QTest::qWait(150);
  EXPECT_TRUE(service.isListening());
  EXPECT_FALSE(service.isConnected());
  EXPECT_EQ(subscribeAttempts.load(), 0);  // komorebi.exe is not running, so nothing to subscribe
  service.stop();
  EXPECT_FALSE(service.isListening());
}
