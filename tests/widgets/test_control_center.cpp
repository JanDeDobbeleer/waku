#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QUuid>
#include <QtTest/QTest>
#include <algorithm>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

#include "core/config/loader.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/widget_builder.h"
#include "core/widgets/widget_context.h"
#include "platform/media_session.h"
#include "widgets/control_center/control_center_widget.h"

using nlohmann::json;
using namespace yasb::widgets;
using namespace yasb::platform;

namespace {

// --- fakes: no audio device, monitor, registry value or process is ever touched -----------------

// Same state shape as tools/parity/vec_control_center.py.
struct FakeWorld {
  json state;
  std::vector<std::string> calls;

  static QUuid uuidFor(const std::string& id) {
    static const QUuid ns(QStringLiteral("{6ba7b810-9dad-11d1-80b4-00c04fd430c8}"));
    return QUuid::createUuidV5(ns, QByteArray::fromStdString(id));
  }
  std::string idOf(const QUuid& guid) const {
    for (const json& plan : state["plans"]) {
      if (uuidFor(plan[0]) == guid) return plan[0];
    }
    for (const auto& [name, id] : modes()) {
      if (uuidFor(id) == guid) return id;
    }
    return {};
  }
  static std::vector<std::pair<std::string, std::string>> modes() {
    return {{"Best power efficiency", "mode-eff"},
            {"Balanced", "mode-bal"},
            {"Best performance", "mode-perf"}};
  }
};

// Recursive object merge; "values" (the brightness map) is replaced, as in the exporter.
json mergeState(const json& base, const json& patch) {
  json merged = base;
  for (const auto& [key, value] : patch.items()) {
    if (value.is_object() && merged.contains(key) && merged[key].is_object() && key != "values") {
      merged[key] = mergeState(merged[key], value);
    } else {
      merged[key] = value;
    }
  }
  return merged;
}

class FakeAudio final : public AudioEndpoints {
 public:
  FakeAudio(FakeWorld& world, std::string key) : world_(world), key_(std::move(key)) {}
  bool available() override { return s()["present"].get<bool>(); }
  std::optional<int> volumePercent() override {
    if (!available()) return std::nullopt;
    return s()["volume"].get<int>();
  }
  bool setVolumePercent(int percent) override {
    s()["volume"] = percent;
    world_.calls.push_back("volume:" + key_ + ":" + std::to_string(percent));
    return true;
  }
  std::optional<bool> muted() override {
    if (!available()) return std::nullopt;
    return s()["muted"].get<bool>();
  }
  bool setMuted(bool muted) override {
    s()["muted"] = muted;
    world_.calls.push_back("mute:" + key_ + ":" + (muted ? "on" : "off"));
    return true;
  }
  std::vector<AudioDevice> devices() override {
    std::vector<AudioDevice> result;
    for (const json& device : s()["devices"])
      result.push_back({QString::fromStdString(device[0]), QString::fromStdString(device[1])});
    return result;
  }
  QString defaultDeviceId() override { return QString::fromStdString(s()["default"]); }
  bool setDefaultDevice(const QString& id) override {
    s()["default"] = id.toStdString();
    world_.calls.push_back("default:" + key_ + ":" + id.toStdString());
    return true;
  }

 private:
  json& s() { return world_.state[key_]; }
  FakeWorld& world_;
  std::string key_;
};

class FakeBrightness final : public BrightnessControl {
 public:
  explicit FakeBrightness(FakeWorld& world) : world_(world) {}
  std::vector<BrightnessMonitor> monitors() override {
    std::vector<BrightnessMonitor> result;
    for (const json& monitor : world_.state["brightness"]["monitors"])
      result.push_back({monitor[0].get<qulonglong>(), QString::fromStdString(monitor[1])});
    return result;
  }
  QString monitorSubtitle(qulonglong id, int index) override {
    QString connection;
    for (const json& monitor : world_.state["brightness"]["monitors"]) {
      if (monitor[0].get<qulonglong>() == id) connection = QString::fromStdString(monitor[2]);
    }
    return connection.isEmpty() ? QStringLiteral("Monitor %1").arg(index + 1)
                                : QStringLiteral("Monitor %1 · %2").arg(index + 1).arg(connection);
  }
  std::optional<int> brightness(qulonglong id) override {
    const json& values = world_.state["brightness"]["values"];
    const auto it = values.find(std::to_string(id));
    if (it == values.end() || it->is_null()) return std::nullopt;
    return it->get<int>();
  }
  void setBrightness(qulonglong id, int percent) override {
    world_.state["brightness"]["values"][std::to_string(id)] = percent;
    world_.calls.push_back("brightness:" + std::to_string(id) + ":" + std::to_string(percent));
  }
  void refreshNow() override { ++refreshes; }
  int refreshes = 0;

 private:
  FakeWorld& world_;
};

class FakeSource final : public MediaSessionSource {
 public:
  explicit FakeSource(FakeWorld& world) : world_(world) {}
  void start(MediaSessionSink*) override {}
  void stop() override {}
  void command(const QString&, MediaCommand command, double) override {
    switch (command) {
      case MediaCommand::PlayPause:
        world_.calls.push_back("media:play_pause");
        break;
      case MediaCommand::Next:
        world_.calls.push_back("media:next");
        break;
      case MediaCommand::Previous:
        world_.calls.push_back("media:prev");
        break;
      default:
        break;
    }
  }

 private:
  FakeWorld& world_;
};

ControlCenterSystem makeFakeSystem(FakeWorld& world, FakeAudio* out, FakeAudio* in,
                                   FakeBrightness* brightness, MediaSessionService* media) {
  ControlCenterSystem system;
  system.output = out;
  system.input = in;
  system.brightness = brightness;
  system.media = media;
  system.dndStatus = [&world] { return QString::fromStdString(world.state["dnd"]); };
  system.setDndStatus = [&world](const QString& mode) {
    world.state["dnd"] = mode.toStdString();
    world.calls.push_back("dnd:" + mode.toStdString());
  };
  system.themeMode = [&world] { return QString::fromStdString(world.state["theme"]); };
  system.toggleTheme = [&world]() -> std::optional<QString> {
    world.state["theme"] = world.state["theme"] == "dark" ? "light" : "dark";
    world.calls.push_back("theme:" + world.state["theme"].get<std::string>());
    return QString::fromStdString(world.state["theme"]);
  };
  system.toggleTouchKeyboard = [&world] { world.calls.push_back("touch_keyboard"); };
  system.startScreenshot = [&world] { world.calls.push_back("screenshot"); };
  system.openLink = [&world](const QString& uri) {
    world.calls.push_back("open:" + uri.toStdString());
  };
  system.runCommand = [&world](const QString& file, const QString& parameters) {
    world.calls.push_back("open:" + file.toStdString() +
                          (parameters.isEmpty() ? "" : ":" + parameters.toStdString()));
  };
  for (auto [name, member] : std::initializer_list<std::pair<const char*, std::function<void()>*>>{
           {"lock", &system.lock},
           {"signout", &system.signOut},
           {"sleep", &system.sleep},
           {"hibernate", &system.hibernate},
           {"restart", &system.restart},
           {"shutdown", &system.shutdown}}) {
    *member = [&world, name = std::string(name)] { world.calls.push_back("power:" + name); };
  }
  system.powerPlans = [&world](std::optional<QUuid>* active) {
    std::vector<PowerPlan> plans;
    for (const json& plan : world.state["plans"])
      plans.push_back({FakeWorld::uuidFor(plan[0]), QString::fromStdString(plan[1])});
    if (active) {
      const json& id = world.state["active_plan"];
      *active = id.is_null() ? std::nullopt : std::optional<QUuid>(FakeWorld::uuidFor(id));
    }
    return plans;
  };
  system.setPowerPlan = [&world](const QUuid& guid) -> unsigned long {
    const std::string id = world.idOf(guid);
    world.state["active_plan"] = id;
    world.calls.push_back("plan:" + id);
    return 0;
  };
  system.powerModes = [] {
    std::vector<PowerMode> modes;
    for (const auto& [name, id] : FakeWorld::modes())
      modes.push_back({QString::fromStdString(name), FakeWorld::uuidFor(id)});
    return modes;
  };
  system.powerModeSupported = [&world] { return world.state["mode_supported"].get<bool>(); };
  system.activePowerMode = [&world]() -> std::optional<PowerMode> {
    const json& name = world.state["active_mode"];
    if (name.is_null()) return std::nullopt;
    for (const auto& [modeName, id] : FakeWorld::modes()) {
      if (modeName == name.get<std::string>())
        return PowerMode{QString::fromStdString(modeName), FakeWorld::uuidFor(id)};
    }
    return std::nullopt;
  };
  system.setPowerMode = [&world](const QUuid& guid) {
    const std::string id = world.idOf(guid);
    for (const auto& [name, modeId] : FakeWorld::modes()) {
      if (modeId == id) world.state["active_mode"] = name;
    }
    world.calls.push_back("mode:" + id);
    return true;
  };
  system.windows10 = world.state["win10"].get<bool>();
  system.userName = [] { return QStringLiteral("Vector User"); };
  system.avatarPath = [] { return QString(); };
  system.monitorOf = [](QWidget*) -> qulonglong { return 111; };
  return system;
}

MediaSessionData mediaData(const json& spec, double now) {
  MediaSessionData data;
  data.appId = QStringLiteral("Fake.Player.App");
  MediaProperties properties;
  properties.title = QString::fromStdString(spec.value("title", "Song"));
  properties.artist = QString::fromStdString(spec.value("artist", "Artist"));
  data.properties = properties;
  data.timeline = MediaTimeline{0, now, 0};
  if (spec.value("playback_ready", true)) {
    MediaPlayback playback;
    playback.status = spec.value("playing", true) ? 4 : 5;
    playback.prevEnabled = spec.value("prev_enabled", true);
    playback.playEnabled = spec.value("play_enabled", true);
    playback.nextEnabled = spec.value("next_enabled", true);
    data.playback = playback;
  }
  return data;
}

yasb::config::options::ControlCenterConfig controlCenterOptions(const json& options) {
  const json config = {
      {"bars", {{"main", {{"widgets", {{"left", {"cc"}}}}}}}},
      {"widgets",
       {{"cc", {{"type", "yasb.control_center.ControlCenterWidget"}, {"options", options}}}}}};
  const auto loaded = yasb::config::loadConfigText(config.dump());
  EXPECT_TRUE(loaded.ok()) << loaded.message;
  auto typed = loaded.widgetOptions<yasb::config::options::ControlCenterConfig>("cc");
  EXPECT_TRUE(typed.has_value());
  return typed.value_or(yasb::config::options::ControlCenterConfig{});
}

json loadVectors() {
  std::ifstream in(std::string(YASB_VECTORS_DIR) + "/labels/control_center.json", std::ios::binary);
  return json::parse(in);
}

json loadFirstVectorState() { return loadVectors().at(0).at("initial_data"); }

class ControlCenterTest : public testing::Test {
 public:
  void TearDown() override { reset(); }

  void reset() {
    widget_.reset();
    media_.reset();
    brightness_.reset();
    input_.reset();
    output_.reset();
  }

  // `state` is the complete starting state; "open" is ignored here.
  ControlCenterWidget& build(const json& options, const json& state) {
    reset();
    world_.state = state;
    world_.state.erase("open");
    world_.calls.clear();
    output_ = std::make_unique<FakeAudio>(world_, "output");
    input_ = std::make_unique<FakeAudio>(world_, "input");
    brightness_ = std::make_unique<FakeBrightness>(world_);
    media_ = std::make_unique<MediaSessionService>(std::make_unique<FakeSource>(world_), nullptr,
                                                   [this] { return now_; });
    media_->start();
    typed_ = controlCenterOptions(options);
    TypedWidgetOptions typedOptions = typed_;
    WidgetContext context{"cc",
                          "yasb.control_center.ControlCenterWidget",
                          options,
                          {},
                          {},
                          eventBus_,
                          globalState_,
                          pollingWorkers_,
                          std::move(typedOptions)};
    widget_.reset(new ControlCenterWidget(
        context,
        makeFakeSystem(world_, output_.get(), input_.get(), brightness_.get(), media_.get())));
    return *widget_;
  }

  // The complete starting state of the first vector scenario (no overrides).
  static json defaultState() {
    json state = loadFirstVectorState();
    state.erase("open");
    return state;
  }

  json userOptions() const {
    return {{"label", "<span>X</span>"},
            {"popup", {{"blur", false}}},
            {"sections", {{"media", {{"show", true}}}}}};
  }

  FakeWorld world_;
  double now_ = 1000;
  yasb::config::options::ControlCenterConfig typed_;
  std::unique_ptr<FakeAudio> output_;
  std::unique_ptr<FakeAudio> input_;
  std::unique_ptr<FakeBrightness> brightness_;
  std::unique_ptr<MediaSessionService> media_;
  std::unique_ptr<ControlCenterWidget> widget_;
  EventBus eventBus_;
  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
};

// --- snapshots (same shape as vec_control_center.py) --------------------------------------------

std::string cls(const QWidget* widget) {
  return widget->property("class").toString().toStdString();
}

json labelState(const QLabel* label) {
  return {
      {"class", cls(label)}, {"hidden", label->isHidden()}, {"text", label->text().toStdString()}};
}

json labelStates(const std::vector<QLabel*>& labels) {
  json result = json::array();
  for (const QLabel* label : labels) result.push_back(labelState(label));
  return result;
}

// [text, class]; the full text for elided labels (Python ElidedLabel.text()).
json textClass(const QLabel* label) {
  if (!label) return nullptr;
  const auto* elided = qobject_cast<const ElidedLabel*>(label);
  return json::array({(elided ? elided->fullText() : label->text()).toStdString(), cls(label)});
}

json menuItems(const QMenu* menu) {
  if (!menu || !menu->isVisible()) return nullptr;
  json items = json::array();
  for (const QAction* action : menu->actions()) {
    if (action->isCheckable()) {
      items.push_back(json::array({action->text().toStdString(), action->isChecked()}));
    } else {
      items.push_back(action->text().toStdString());
    }
  }
  return items;
}

json systemControlsJson(SystemControlsSection& section) {
  return {{"class", cls(&section)},
          {"profile", cls(section.profileButton())},
          {"settings", json::array({section.settingsButton()->text().toStdString(),
                                    cls(section.settingsButton())})},
          {"power",
           json::array({section.powerButton()->text().toStdString(), cls(section.powerButton())})},
          {"settings_menu", menuItems(section.settingsMenu())},
          {"profile_menu", menuItems(section.profileMenu())},
          {"power_menu", menuItems(section.powerMenu())}};
}

json quickActionsJson(QuickActionsSection& section,
                      const yasb::config::options::QuickActionsSectionConfig& config) {
  json buttons = json::array();
  for (const auto& action : config.actions) {
    ClickFrame* button = section.button(QString::fromStdString(action.id));
    QLabel* icon = nullptr;
    QLabel* title = nullptr;
    for (QLabel* label : button->findChildren<QLabel*>()) {
      const QStringList tokens = label->property("class").toString().split(u' ');
      if (tokens.contains(QStringLiteral("icon"))) icon = label;
      if (tokens.contains(QStringLiteral("title"))) title = label;
    }
    buttons.push_back({{"id", action.id},
                       {"class", cls(button)},
                       {"enabled", button->isEnabled()},
                       {"icon", textClass(icon)},
                       {"title", textClass(title)}});
  }
  return {{"class", cls(&section)}, {"buttons", buttons}};
}

json slidersJson(SlidersSection& section) {
  json rows = json::object();
  struct Row {
    const char* key;
    QSlider* slider;
    QLabel* value;
    QPushButton* source;
  };
  const Row all[] = {
      {"volume", section.volumeSlider(), section.volumeValue(), section.volumeSource()},
      {"microphone", section.microphoneSlider(), section.microphoneValue(),
       section.microphoneSource()},
      {"brightness", section.brightnessSlider(), section.brightnessValue(),
       section.brightnessSource()}};
  for (const Row& row : all) {
    if (!row.slider) continue;
    rows[row.key] = {
        {"class", cls(row.slider->parentWidget())},
        {"value", row.slider->value()},
        {"enabled", row.slider->isEnabled()},
        {"text", row.value->text().toStdString()},
        {"source", row.source ? json(row.source->text().toStdString()) : json(nullptr)},
        {"menu", menuItems(section.sourceMenu(QString::fromLatin1(row.key)))}};
  }
  return {{"class", cls(&section)}, {"rows", rows}};
}

json powerJson(PowerSection& section) {
  const auto* modeLabel = section.modeLabel();
  return {{"class", cls(&section)},
          {"plan_class", cls(section.planButton())},
          {"plan", section.planLabel()->fullText().toStdString()},
          {"mode_class", section.modeButton() ? json(cls(section.modeButton())) : json(nullptr)},
          {"mode", modeLabel ? json(modeLabel->fullText().toStdString()) : json(nullptr)},
          {"plan_menu", menuItems(section.menu(QStringLiteral("plan")))},
          {"mode_menu", menuItems(section.menu(QStringLiteral("mode")))}};
}

json mediaJson(MediaSection& section) {
  return {{"class", cls(&section)},
          {"hidden", section.isHidden()},
          {"title", qobject_cast<ElidedLabel*>(section.title())->fullText().toStdString()},
          {"artist", qobject_cast<ElidedLabel*>(section.artist())->fullText().toStdString()},
          {"thumbnail_has_pixmap", !section.thumbnail()->pixmap().isNull()},
          {"prev", textClass(section.prevButton())},
          {"play", textClass(section.playButton())},
          {"next", textClass(section.nextButton())}};
}

json menuJson(ControlCenterWidget& widget,
              const yasb::config::options::ControlCenterConfig& config) {
  MenuPopup* dialog = widget.dialog();
  if (!dialog || !dialog->isVisible()) return nullptr;
  json order = json::array();
  json sections = json::object();
  QLayout* layout = dialog->layout();
  for (int i = 0; i < layout->count(); ++i) {
    QWidget* child = layout->itemAt(i)->widget();
    if (child == widget.systemControls()) {
      order.push_back("system_controls");
      sections["system_controls"] = systemControlsJson(*widget.systemControls());
    } else if (child == widget.quickActions()) {
      order.push_back("quick_actions");
      sections["quick_actions"] =
          quickActionsJson(*widget.quickActions(), config.sections.quick_actions);
    } else if (child == widget.sliders()) {
      order.push_back("sliders");
      sections["sliders"] = slidersJson(*widget.sliders());
    } else if (child == widget.power()) {
      order.push_back("power");
      sections["power"] = powerJson(*widget.power());
    } else if (child == widget.media()) {
      order.push_back("media");
      sections["media"] = mediaJson(*widget.media());
    }
  }
  return {{"class", cls(dialog)}, {"order", order}, {"sections", sections}};
}

json snapshot(ControlCenterWidget& widget,
              const yasb::config::options::ControlCenterConfig& config) {
  json order = json::array();
  QLayout* layout = widget.labelContainer()->layout();
  for (int i = 0; i < layout->count(); ++i) {
    const auto* label = qobject_cast<QLabel*>(layout->itemAt(i)->widget());
    const auto has = [&](const std::vector<QLabel*>& v) {
      return std::find(v.begin(), v.end(), label) != v.end();
    };
    order.push_back(has(widget.labels()) ? "primary" : has(widget.labelsAlt()) ? "alt" : "other");
  }
  return {
      {"alt", labelStates(widget.labelsAlt())},   {"container_class", cls(widget.labelContainer())},
      {"frame_class", cls(widget.widgetFrame())}, {"layout_order", order},
      {"primary", labelStates(widget.labels())},  {"widget_hidden", widget.isHidden()},
      {"menu", menuJson(widget, config)}};
}

QAction* findAction(QMenu* menu, const std::string& text) {
  if (!menu) return nullptr;
  for (QAction* action : menu->actions()) {
    if (action->text().toStdString() == text) return action;
  }
  return nullptr;
}

// Triggers the source-menu entry at the position of `id` in the state's device or monitor list.
void chooseSource(ControlCenterWidget& widget, const FakeWorld& world, const std::string& key,
                  const json& id) {
  QMenu* menu = widget.sliders()->sourceMenu(QString::fromStdString(key));
  ASSERT_NE(menu, nullptr);
  const std::string stateKey = key == "volume" ? "output" : "input";
  const json& list = key == "brightness" ? world.state["brightness"]["monitors"]
                                         : world.state[stateKey]["devices"];
  int index = 0;
  for (const json& entry : list) {
    if (entry[0] == id) {
      // The Python vectors call the handler directly, so the menu's check marks stay as built;
      // triggering a checkable action would also toggle it. Restore them to match.
      std::vector<bool> checked;
      for (const QAction* action : menu->actions()) checked.push_back(action->isChecked());
      menu->actions().at(index)->trigger();
      for (int i = 0; i < static_cast<int>(checked.size()); ++i)
        menu->actions().at(i)->setChecked(checked[static_cast<size_t>(i)]);
      return;
    }
    ++index;
  }
  FAIL() << "unknown source " << id;
}

void applyStep(ControlCenterTest& test, ControlCenterWidget& widget, const json& step) {
  FakeWorld& world = test.world_;
  world.calls.clear();
  if (step.contains("state")) {
    world.state = mergeState(world.state, step["state"]);
    widget.refreshPopupState();
  }
  if (step.value("open", false)) widget.showMenu();
  if (step.value("close", false)) widget.dialog()->hideNow();
  if (step.value("toggle_menu", false)) widget.toggleMenu();
  if (step.contains("action") && widget.quickActions()) {
    const std::string id = step["action"];
    for (const auto& action : test.typed_.sections.quick_actions.actions) {
      if (action.id == id) widget.quickActions()->runAction(action);
    }
  }
  if (step.contains("slider")) {
    const std::string key = step["slider"][0];
    QSlider* slider = key == "volume"       ? widget.sliders()->volumeSlider()
                      : key == "microphone" ? widget.sliders()->microphoneSlider()
                                            : widget.sliders()->brightnessSlider();
    slider->setValue(step["slider"][1].get<int>());
  }
  if (step.contains("source_menu")) {
    const std::string key = step["source_menu"];
    if (key == "volume") widget.sliders()->showVolumeSourceMenu();
    if (key == "microphone") widget.sliders()->showMicrophoneSourceMenu();
    if (key == "brightness") widget.sliders()->showBrightnessSourceMenu();
  }
  if (step.contains("source")) chooseSource(widget, world, step["source"][0], step["source"][1]);
  if (step.contains("system_menu")) {
    const std::string key = step["system_menu"];
    if (key == "settings") widget.systemControls()->showSettingsMenu();
    if (key == "profile") widget.systemControls()->showProfileMenu();
    if (key == "power") widget.systemControls()->showPowerMenu();
  }
  if (step.contains("system_action")) {
    const std::string key = step["system_action"][0];
    QMenu* menu = key == "settings"  ? widget.systemControls()->settingsMenu()
                  : key == "profile" ? widget.systemControls()->profileMenu()
                                     : widget.systemControls()->powerMenu();
    QAction* action = findAction(menu, step["system_action"][1]);
    ASSERT_NE(action, nullptr);
    action->trigger();
  }
  if (step.contains("plan_menu")) {
    if (step["plan_menu"] == "plan") widget.power()->showPlanMenu();
    if (step["plan_menu"] == "mode") widget.power()->showModeMenu();
  }
  if (step.contains("power_action")) {
    QAction* action =
        findAction(widget.power()->menu(QString::fromStdString(step["power_action"][0])),
                   step["power_action"][1]);
    ASSERT_NE(action, nullptr);
    action->trigger();
  }
  if (step.contains("media")) {
    test.now_ += 1;
    // A fresh session object each time, as the Python fake builds one per step.
    test.media_->applySessions({}, QString(), true);
    if (!step["media"].is_null()) {
      test.media_->applySessions({mediaData(step["media"], test.now_)},
                                 QStringLiteral("Fake.Player.App"), true);
    }
  }
  if (step.value("media_hide", false)) widget.media()->hideNow();
  if (step.contains("media_click")) {
    const std::string key = step["media_click"];
    QLabel* button = key == "play"   ? widget.media()->playButton()
                     : key == "prev" ? widget.media()->prevButton()
                                     : widget.media()->nextButton();
    QTest::mouseClick(button, Qt::LeftButton);
  }
}

}  // namespace

TEST_F(ControlCenterTest, ReplaysPythonVectors) {
  const json vectors = loadVectors();
  ASSERT_FALSE(vectors.empty());
  for (const json& scenario : vectors) {
    const std::string name = scenario["scenario"];
    SCOPED_TRACE(name);
    ControlCenterWidget& widget = build(scenario["options"], scenario["initial_data"]);
    widget.show();
    if (scenario["initial_data"].value("open", false)) widget.showMenu();
    EXPECT_EQ(snapshot(widget, typed_), scenario["initial"]) << "initial";
    int index = 0;
    for (const json& step : scenario["steps"]) {
      SCOPED_TRACE("step " + std::to_string(index++) + " " + step["step"].dump());
      applyStep(*this, widget, step["step"]);
      EXPECT_EQ(snapshot(widget, typed_), step["snapshot"]);
      EXPECT_EQ(json(world_.calls), step["commands"]);
    }
    if (widget.dialog()) widget.dialog()->hideNow();
  }
}

TEST_F(ControlCenterTest, VolumeSliderFollowsTheSystemBothWays) {
  ControlCenterWidget& widget = build(userOptions(), defaultState());
  widget.show();
  widget.showMenu();
  QSlider* slider = widget.sliders()->volumeSlider();
  EXPECT_EQ(slider->value(), 40);

  // Slider -> system.
  slider->setValue(65);
  EXPECT_EQ(world_.state["output"]["volume"], 65);
  EXPECT_EQ(widget.sliders()->volumeValue()->text(), "65%");

  // System -> slider, without echoing the change back.
  world_.calls.clear();
  world_.state["output"]["volume"] = 20;
  emit output_->volumeChanged();
  EXPECT_EQ(slider->value(), 20);
  EXPECT_EQ(widget.sliders()->volumeValue()->text(), "20%");
  EXPECT_TRUE(world_.calls.empty());
}

TEST_F(ControlCenterTest, DoNotDisturbStateSurvivesAReopen) {
  ControlCenterWidget& widget = build(userOptions(), defaultState());
  widget.show();
  widget.showMenu();
  emit widget.quickActions()->button(QStringLiteral("toggle_dnd"))->clicked();
  EXPECT_EQ(world_.state["dnd"], "priority");
  EXPECT_EQ(cls(widget.quickActions()->button(QStringLiteral("toggle_dnd"))),
            "button toggle_dnd active");

  widget.dialog()->hideNow();
  widget.showMenu();
  EXPECT_EQ(cls(widget.quickActions()->button(QStringLiteral("toggle_dnd"))),
            "button toggle_dnd active");
}

TEST_F(ControlCenterTest, SwitchingTheOutputDeviceMovesTheSlider) {
  ControlCenterWidget& widget = build(userOptions(), defaultState());
  widget.show();
  widget.showMenu();
  widget.sliders()->showVolumeSourceMenu();
  QMenu* menu = widget.sliders()->sourceMenu(QStringLiteral("volume"));
  ASSERT_NE(menu, nullptr);
  // The new default device has its own level; the device-changed notification refreshes the row.
  world_.state["output"]["volume"] = 85;
  menu->actions().at(1)->trigger();
  EXPECT_EQ(world_.state["output"]["default"], "out-2");
  EXPECT_EQ(widget.sliders()->volumeSlider()->value(), 85);
}

TEST_F(ControlCenterTest, InternalPanelOnlyShowsNoBrightnessSourceMenu) {
  ControlCenterWidget& widget = build(userOptions(), defaultState());
  widget.show();
  widget.showMenu();
  widget.sliders()->showBrightnessSourceMenu();
  EXPECT_EQ(widget.sliders()->sourceMenu(QStringLiteral("brightness")), nullptr);
  EXPECT_EQ(widget.sliders()->brightnessSlider()->value(), 55);
}

TEST_F(ControlCenterTest, UnsupportedPowerModeShowsAutomaticAndDisablesTheButton) {
  json state = defaultState();
  state["mode_supported"] = false;
  ControlCenterWidget& widget = build(userOptions(), state);
  widget.show();
  widget.showMenu();
  EXPECT_FALSE(widget.power()->modeEnabled());
  EXPECT_EQ(widget.power()->modeLabel()->fullText(), "Automatic");
  EXPECT_EQ(cls(widget.power()->modeButton()), "mode-name disabled");
  widget.power()->showModeMenu();
  EXPECT_EQ(widget.power()->menu(QStringLiteral("mode")), nullptr);
}

TEST_F(ControlCenterTest, ScreenshotActionHidesThePopupAndStartsTheInterimCapture) {
  ControlCenterWidget& widget = build(userOptions(), defaultState());
  widget.show();
  widget.showMenu();
  emit widget.quickActions()->button(QStringLiteral("screenshot"))->clicked();
  EXPECT_FALSE(widget.dialog()->isVisible());
  EXPECT_EQ(world_.calls, std::vector<std::string>{"screenshot"});
}

TEST_F(ControlCenterTest, ToggleMenuCallbackOpensAndFadesTheRegisteredPopup) {
  ControlCenterWidget& widget = build(userOptions(), defaultState());
  widget.show();
  EXPECT_TRUE(widget.runCallback(QStringLiteral("toggle_menu")));
  ASSERT_NE(widget.dialog(), nullptr);
  EXPECT_TRUE(widget.dialog()->isVisible());
  EXPECT_EQ(cls(widget.dialog()), "control-center-menu");
  // Opening refreshes the sections against the live state.
  EXPECT_GT(brightness_->refreshes, 0);
}

TEST_F(ControlCenterTest, WidgetBuilderMapsTheTypeToTypedOptions) {
  const json config = {{"bars", {{"main", {{"widgets", {{"left", {"cc"}}}}}}}},
                       {"widgets",
                        {{"cc",
                          {{"type", "yasb.control_center.ControlCenterWidget"},
                           {"options", {{"tooltip", true}, {"label", "<span>Q</span>"}}}}}}}};
  const auto loaded = yasb::config::loadConfigText(config.dump());
  ASSERT_TRUE(loaded.ok()) << loaded.message;
  WidgetRegistry registry;
  FakeWorld world;
  world.state = defaultState();
  FakeAudio out(world, "output");
  FakeAudio in(world, "input");
  FakeBrightness brightness(world);
  registry.add("yasb.control_center.ControlCenterWidget", [&](const WidgetContext& context) {
    return new ControlCenterWidget(context, makeFakeSystem(world, &out, &in, &brightness, nullptr));
  });
  WidgetBuilder builder(loaded, registry, eventBus_, globalState_, pollingWorkers_);
  std::unique_ptr<BaseWidget> built(builder.buildWidget("cc", QString(), QString()));
  ASSERT_NE(built, nullptr);
  EXPECT_FALSE(builder.hasErrors());
  auto* widget = dynamic_cast<ControlCenterWidget*>(built.get());
  ASSERT_NE(widget, nullptr);
  EXPECT_EQ(widget->labels().at(0)->text(), "Q");
  EXPECT_EQ(widget->widgetFrame()->property("class").toString(), "widget control-center-widget ");
}
