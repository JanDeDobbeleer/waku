// Systray widget: pinning, ordering, state file, popup and click forwarding. The tray host and
// the Win32 window calls are fakes, the state folder is a temp dir: nothing here touches the real
// tray, real windows or the user's saved state.
#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <fstream>
#include <set>

#include "core/bar/theme_class.h"
#include "core/config/schema_validator.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/tooltip.h"
#include "core/widgets/widget_context.h"
#include "platform/systray_pipe.h"
#include "platform/systray_types.h"
#include "widgets/systray/systray_popup.h"
#include "widgets/systray/systray_service.h"
#include "widgets/systray/systray_state.h"
#include "widgets/systray/systray_widget.h"

using nlohmann::json;
using namespace yasb::widgets;
using namespace yasb::platform;

namespace {

class FakeHost : public TrayHost {
 public:
  void start() override { ++starts; }
  void stop() override { ++stops; }
  QString mode() const override { return QStringLiteral("fake"); }
  void sendModified(const TrayIconData& data) { emit iconModified(data); }
  void sendDeleted(const TrayIconData& data) { emit iconDeleted(data); }
  void askForIcons() { emit updateIcons(); }
  int starts = 0, stops = 0;
};

class FakeApi : public TrayWindowApi {
 public:
  struct Message {
    std::uint64_t hwnd;
    std::uint32_t message;
    std::uint64_t wParam;
    std::int64_t lParam;
    bool operator==(const Message& other) const {
      return hwnd == other.hwnd && message == other.message && wParam == other.wParam &&
             lParam == other.lParam;
    }
  };
  bool isWindow(std::uint64_t hwnd) override { return windows.count(hwnd) != 0; }
  void allowSetForeground(std::uint64_t hwnd) override { allowed.push_back(hwnd); }
  void sendNotifyMessage(std::uint64_t hwnd, std::uint32_t message, std::uint64_t wParam,
                         std::int64_t lParam) override {
    sent.push_back({hwnd, message, wParam, lParam});
  }
  std::pair<int, int> cursorPos() override { return cursor; }
  void broadcastTaskbarCreated() override { ++broadcasts; }

  std::set<std::uint64_t> windows;
  std::vector<std::uint64_t> allowed;
  std::vector<Message> sent;
  std::pair<int, int> cursor{300, 40};
  int broadcasts = 0;
};

// The systray block of the user's config.yaml.
json userOptions() {
  return {{"class_name", "systray"},
          {"label_collapsed", "\uE972"},
          {"label_expanded", "\uE971"},
          {"label_position", "right"},
          {"pin_click_modifier", "ctrl"},
          {"show_unpinned", false},
          {"show_battery", false},
          {"show_volume", false},
          {"icon_size", 16},
          {"use_hook", true},
          {"show_in_popup", true},
          {"icons_per_row", 5},
          {"popup",
           {{"blur", true},
            {"round_corners", true},
            {"round_corners_type", "normal"},
            {"border_color", "System"},
            {"alignment", "center"},
            {"direction", "down"},
            {"offset_top", 6},
            {"offset_left", 0}}}};
}

json inlineOptions(json patch = json::object()) {
  json options = userOptions();
  options["show_in_popup"] = false;
  options["show_unpinned"] = true;
  options.update(patch, true);
  return options;
}

TrayIconData makeIcon(std::uint64_t hwnd, std::uint32_t uid, const QString& exe,
                      std::optional<QUuid> guid = std::nullopt, const QString& tip = QString()) {
  TrayIconData data;
  data.messageType = kNimAdd;
  data.hWnd = hwnd;
  data.uID = uid;
  data.uFlags = kNifMessage | kNifIcon | kNifTip | (guid ? kNifGuid : 0u);
  data.uCallbackMessage = 0x8000;
  data.hIcon = 1;
  data.szTip = tip.isEmpty() ? exe + QStringLiteral(" tip") : tip;
  data.guid = guid;
  data.exe = exe;
  data.exePath = QStringLiteral("C:\\Apps\\") + exe + QStringLiteral(".exe");
  data.iconWidth = 16;
  data.iconHeight = 16;
  data.iconRgba = QByteArray(16 * 16 * 4, '\x40');
  return data;
}

QStringList keysIn(QLayout* layout) {
  QStringList keys;
  for (int i = 0; i < layout->count(); ++i) {
    if (auto* icon = qobject_cast<TrayIconButton*>(layout->itemAt(i)->widget()))
      keys << icon->info.exe;
  }
  return keys;
}

class SystrayWidgetTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(stateDir_.isValid()); }

  std::unique_ptr<SystrayWidget> make(const json& raw, bool connectRefresh = true) {
    const auto validated = yasb::config::SchemaValidator::embedded().validateWidgetOptions(
        "yasb.systray.SystrayWidget", yasb::config::OJson::parse(raw.dump()));
    EXPECT_TRUE(validated.ok());
    const json options = json::parse(validated.value.dump());
    WidgetContext context{"systray",
                          "yasb.systray.SystrayWidget",
                          options,
                          {},
                          {},
                          eventBus_,
                          globalState_,
                          pollingWorkers_,
                          options.get<yasb::config::options::SystrayWidgetConfig>()};
    SystrayDeps deps;
    deps.host = &host_;
    deps.windowApi = &api_;
    deps.connectRefresh = connectRefresh;
    deps.stateDir = stateDir_.path();
    deps.screenId = QStringLiteral("TESTSCREEN");
    auto widget = std::make_unique<SystrayWidget>(context, deps);
    widget->resize(200, 32);
    widget->show();
    widget->loadState();  // the widget schedules this for the event loop; do it now
    return widget;
  }

  QString stateFile() const {
    return QDir(stateDir_.path()).filePath("systray_state_TESTSCREEN.json");
  }

  void writeState(const QByteArray& text) {
    QFile file(stateFile());
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(text);
  }

  QByteArray readState() {
    QFile file(stateFile());
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll();
  }

  QTemporaryDir stateDir_;
  FakeHost host_;
  FakeApi api_;
  EventBus eventBus_;
  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
};

// --- construction ----------------------------------------------------------------------------

TEST_F(SystrayWidgetTest, UserConfigBuildsTheSameHierarchyAsPython) {
  auto widget = make(userOptions());
  EXPECT_EQ(widget->widgetFrame()->property("class").toString(), "widget systray");
  EXPECT_EQ(widget->labelContainer()->property("class").toString(), "widget-container");
  EXPECT_EQ(widget->visibilityButton()->property("class").toString(), "unpinned-visibility-btn");
  EXPECT_EQ(widget->pinnedWidget()->property("class").toString(), "pinned-container");
  EXPECT_FALSE(widget->pinnedWidget()->property("forceshow").toBool());
  EXPECT_EQ(widget->pinnedWidget()->minimumWidth(), 16);
  ASSERT_NE(widget->popup(), nullptr);
  EXPECT_EQ(widget->unpinnedWidget(), nullptr);
  EXPECT_EQ(widget->popup()->gridWidget()->property("class").toString(), "systray-popup");
  EXPECT_EQ(widget->popup()->gridWidget()->gridCols(), 5);

  // Popup mode: collapsed glyph, unchecked, only the pinned container and the button in the bar,
  // the button after the pinned container (label_position: right).
  EXPECT_EQ(widget->visibilityButton()->text(), QString::fromUtf8("\xEE\xA5\xB2"));
  EXPECT_FALSE(widget->visibilityButton()->isChecked());
  EXPECT_TRUE(widget->visibilityButton()->isVisible());
  QLayout* layout = widget->labelContainer()->layout();
  ASSERT_EQ(layout->count(), 2);
  EXPECT_EQ(layout->itemAt(0)->widget(), widget->pinnedWidget());
  EXPECT_EQ(layout->itemAt(1)->widget(), widget->visibilityButton());
  EXPECT_EQ(host_.starts, 1);
}

TEST_F(SystrayWidgetTest, InlineModeAndLabelPositionLeft) {
  auto widget = make(inlineOptions({{"label_position", "left"}, {"show_unpinned", false}}));
  ASSERT_NE(widget->unpinnedWidget(), nullptr);
  EXPECT_EQ(widget->popup(), nullptr);
  EXPECT_EQ(widget->unpinnedWidget()->property("class").toString(), "unpinned-container");
  QLayout* layout = widget->labelContainer()->layout();
  ASSERT_EQ(layout->count(), 3);
  EXPECT_EQ(layout->itemAt(0)->widget(), widget->visibilityButton());
  EXPECT_EQ(layout->itemAt(1)->widget(), widget->unpinnedWidget());
  EXPECT_EQ(layout->itemAt(2)->widget(), widget->pinnedWidget());
  // show_unpinned false: collapsed glyph and the unpinned container hidden.
  EXPECT_FALSE(widget->visibilityButton()->isChecked());
  EXPECT_EQ(widget->visibilityButton()->text(), QString::fromUtf8("\xEE\xA5\xB2"));
  EXPECT_TRUE(widget->unpinnedWidget()->isHidden());

  // The button toggles the container and its glyph.
  widget->visibilityButton()->click();
  EXPECT_TRUE(widget->visibilityButton()->isChecked());
  EXPECT_FALSE(widget->unpinnedWidget()->isHidden());
  EXPECT_EQ(widget->visibilityButton()->text(), QString::fromUtf8("\xEE\xA5\xB1"));
  widget->visibilityButton()->click();
  EXPECT_TRUE(widget->unpinnedWidget()->isHidden());
}

TEST_F(SystrayWidgetTest, InlineWithoutButtonShowsTheUnpinnedContainer) {
  auto widget = make(inlineOptions({{"show_unpinned", false}, {"show_unpinned_button", false}}));
  EXPECT_FALSE(widget->visibilityButton()->isVisible());
  EXPECT_FALSE(widget->unpinnedWidget()->isHidden());
}

TEST_F(SystrayWidgetTest, SharedHostIsRefreshedOnlyByTheFirstWidget) {
  auto first = make(userOptions(), true);
  auto second = make(userOptions(), false);
  host_.askForIcons();
  QCoreApplication::processEvents();
  EXPECT_TRUE(first->refreshTimerActive());
  EXPECT_FALSE(second->refreshTimerActive());
  QTest::qWait(300);  // the 200 ms debounce elapses: one TaskbarCreated broadcast
  EXPECT_EQ(api_.broadcasts, 1);
}

TEST_F(SystrayWidgetTest, PinModifierMapping) {
  for (const auto& [name, modifier] : {std::pair{"ctrl", Qt::ControlModifier},
                                       {"shift", Qt::ShiftModifier},
                                       {"alt", Qt::AltModifier},
                                       {"CTRL", Qt::ControlModifier},
                                       {"hyper", Qt::AltModifier}}) {
    QFile::remove(stateFile());  // every round starts with nothing saved
    auto widget = make(inlineOptions({{"pin_click_modifier", name}}));
    widget->onIconModified(makeIcon(1, 1, "App"));
    auto* icon = widget->icons().front().data();
    QTest::mouseClick(icon, Qt::LeftButton, modifier);
    EXPECT_TRUE(icon->isPinned) << name;
    EXPECT_TRUE(api_.sent.empty()) << name;
  }
}

// --- icons -----------------------------------------------------------------------------------

TEST_F(SystrayWidgetTest, NewIconsStartUnpinnedWithTipAndImage) {
  auto widget = make(inlineOptions());
  widget->onIconModified(makeIcon(0x10, 1, "Discord", std::nullopt, "Discord - 3 unread"));
  ASSERT_EQ(widget->icons().size(), 1u);
  TrayIconButton* icon = widget->icons().front();
  EXPECT_EQ(icon->property("class").toString(), "button");
  EXPECT_EQ(icon->iconSize(), QSize(16, 16));
  EXPECT_FALSE(icon->isPinned);
  EXPECT_EQ(widget->unpinnedLayout()->indexOf(icon), 0);
  EXPECT_FALSE(icon->isHidden());
  // The icon image is 32x32 whatever the source size (Python resizes everything else).
  EXPECT_EQ(icon->iconImage().size(), QSize(32, 32));
  EXPECT_FALSE(icon->icon().isNull());
  auto* tooltip = icon->findChild<TooltipEventFilter*>();
  ASSERT_NE(tooltip, nullptr);
  EXPECT_EQ(tooltip->text(), "Discord - 3 unread");

  // Without a tip the tooltip falls back to the executable name.
  TrayIconData noTip = makeIcon(0x11, 1, "Steam");
  noTip.uFlags &= ~kNifTip;
  noTip.szTip.clear();
  widget->onIconModified(noTip);
  EXPECT_EQ(widget->icons().back()->findChild<TooltipEventFilter*>()->text(), "Steam");
}

TEST_F(SystrayWidgetTest, TooltipsCanBeDisabled) {
  auto widget = make(inlineOptions({{"tooltip", false}}));
  widget->onIconModified(makeIcon(1, 1, "App"));
  EXPECT_EQ(widget->icons().front()->findChild<TooltipEventFilter*>(), nullptr);
}

TEST_F(SystrayWidgetTest, ModifyUpdatesTheExistingIconByGuidThenWindowAndId) {
  auto widget = make(inlineOptions());
  const QUuid guid = QUuid::createUuid();
  widget->onIconModified(makeIcon(0x10, 1, "App", guid, "first"));
  widget->onIconModified(makeIcon(0x99, 5, "App", guid, "second"));  // same guid, new window
  ASSERT_EQ(widget->icons().size(), 1u);
  EXPECT_EQ(widget->icons().front()->info.szTip, "second");
  EXPECT_EQ(widget->icons().front()->info.hWnd, 0x99u);

  widget->onIconModified(makeIcon(0x20, 2, "Other"));
  TrayIconData again = makeIcon(0x20, 2, "Other", std::nullopt, "renamed");
  widget->onIconModified(again);
  ASSERT_EQ(widget->icons().size(), 2u);
  EXPECT_EQ(widget->icons().back()->info.szTip, "renamed");
}

TEST_F(SystrayWidgetTest, ModifyOnlyCopiesFieldsNamedByItsFlags) {
  auto widget = make(inlineOptions());
  widget->onIconModified(makeIcon(0x10, 1, "App", std::nullopt, "kept tip"));
  TrayIconData onlyState;
  onlyState.messageType = kNimModify;
  onlyState.hWnd = 0x10;
  onlyState.uID = 1;
  onlyState.uFlags = kNifState;
  onlyState.dwState = 0;
  onlyState.exe = "App";
  onlyState.exePath = "C:\\Apps\\App.exe";
  widget->onIconModified(onlyState);
  TrayIconButton* icon = widget->icons().front();
  EXPECT_EQ(icon->info.szTip, "kept tip");
  EXPECT_EQ(icon->info.uCallbackMessage, 0x8000u);
  EXPECT_EQ(icon->info.hIcon, 1u);
  // The image is a "direct" attribute: a modify without an icon clears it, as in Python...
  EXPECT_TRUE(icon->iconImage().isNull());
  // ...while the QIcon the button already shows stays.
  EXPECT_FALSE(icon->icon().isNull());
}

TEST_F(SystrayWidgetTest, WindowsOwnIconsAreFilteredUnlessEnabled) {
  struct Case {
    QUuid guid;
    const char* option;
  };
  const Case cases[] = {{kTrayBatteryIconGuid, "show_battery"},
                        {kTrayVolumeIconGuid, "show_volume"},
                        {kTrayNetworkIconGuid, "show_network"}};
  for (const Case& c : cases) {
    auto hidden = make(inlineOptions());
    hidden->onIconModified(makeIcon(1, 1, "Shell", c.guid));
    EXPECT_TRUE(hidden->icons().empty()) << c.option;

    auto shown = make(inlineOptions({{c.option, true}}));
    shown->onIconModified(makeIcon(1, 1, "Shell", c.guid));
    EXPECT_EQ(shown->icons().size(), 1u) << c.option;
  }
}

TEST_F(SystrayWidgetTest, HideIconsMatchesTheExecutableNameCaseInsensitively) {
  auto widget = make(inlineOptions({{"hide_icons", json::array({"Discord", "STEAM"})}}));
  widget->onIconModified(makeIcon(1, 1, "discord"));
  widget->onIconModified(makeIcon(2, 1, "Steam"));
  widget->onIconModified(makeIcon(3, 1, "Slack"));
  ASSERT_EQ(widget->icons().size(), 1u);
  EXPECT_EQ(widget->icons().front()->info.exe, "Slack");
}

TEST_F(SystrayWidgetTest, HiddenStateHidesTheIconAndShowsItAgain) {
  auto widget = make(inlineOptions());
  TrayIconData hidden = makeIcon(1, 1, "App");
  hidden.uFlags |= kNifState;
  hidden.dwState = kNisHidden;
  widget->onIconModified(hidden);
  EXPECT_TRUE(widget->icons().front()->isHidden());

  hidden.dwState = 0;
  widget->onIconModified(hidden);
  EXPECT_FALSE(widget->icons().front()->isHidden());
}

TEST_F(SystrayWidgetTest, DeleteRemovesTheIcon) {
  auto widget = make(inlineOptions());
  widget->onIconModified(makeIcon(1, 1, "A"));
  widget->onIconModified(makeIcon(2, 2, "B"));
  TrayIconData gone;
  gone.hWnd = 1;
  gone.uID = 1;
  widget->onIconDeleted(gone);
  ASSERT_EQ(widget->icons().size(), 1u);
  EXPECT_EQ(widget->icons().front()->info.exe, "B");
  gone.hWnd = 77;  // unknown icon: ignored
  widget->onIconDeleted(gone);
  EXPECT_EQ(widget->icons().size(), 1u);
}

TEST_F(SystrayWidgetTest, IconsWhoseWindowIsGoneAreDropped) {
  auto widget = make(inlineOptions());
  widget->onIconModified(makeIcon(1, 1, "Alive"));
  widget->onIconModified(makeIcon(2, 1, "Dead"));
  api_.windows = {1};
  widget->checkIcons();
  ASSERT_EQ(widget->icons().size(), 1u);
  EXPECT_EQ(widget->icons().front()->info.exe, "Alive");
}

// --- pinning and state -----------------------------------------------------------------------

TEST_F(SystrayWidgetTest, SavedStatePlacesAndOrdersNewIcons) {
  writeState(
      "{\r\n"
      "  \"C:\\\\Apps\\\\Pinned.exe\": {\r\n    \"is_pinned\": true,\r\n    \"index\": 0\r\n  "
      "},\r\n"
      "  \"C:\\\\Apps\\\\Second.exe\": {\r\n    \"is_pinned\": false,\r\n    \"index\": 1\r\n  "
      "},\r\n"
      "  \"C:\\\\Apps\\\\First.exe\": {\r\n    \"is_pinned\": false,\r\n    \"index\": 0\r\n  }\r\n"
      "}");
  auto widget = make(inlineOptions());
  // Pinned.exe, First.exe and Second.exe arrive in a different order than saved.
  widget->onIconModified(makeIcon(1, 1, "Second"));
  widget->onIconModified(makeIcon(2, 1, "Pinned"));
  widget->onIconModified(makeIcon(3, 1, "First"));
  widget->onIconModified(makeIcon(4, 1, "Fresh"));
  EXPECT_EQ(keysIn(widget->pinnedLayout()), QStringList{"Pinned"});
  EXPECT_EQ(keysIn(widget->unpinnedLayout()), (QStringList{"Second", "First", "Fresh"}));
  widget->sortIcons();
  EXPECT_EQ(keysIn(widget->unpinnedLayout()), (QStringList{"First", "Second", "Fresh"}));
  EXPECT_TRUE(widget->icons().at(1)->isPinned);
}

TEST_F(SystrayWidgetTest, StateIsLookedUpByGuidBeforeExePath) {
  const QUuid guid = QUuid::createUuid();
  IconStateMap state;
  state.set(guid.toString(QUuid::WithoutBraces), IconState{true, 0});
  state.set("C:\\Apps\\App.exe", IconState{false, 1});
  writeState(QByteArray());  // an unreadable file: the widget starts from nothing
  auto widget = make(inlineOptions());
  widget->setCurrentState(state);
  widget->onIconModified(makeIcon(1, 1, "App", guid));
  EXPECT_TRUE(widget->icons().front()->isPinned);
  widget->onIconModified(makeIcon(2, 1, "App"));  // no guid: the exe path entry applies
  EXPECT_FALSE(widget->icons().back()->isPinned);
}

TEST_F(SystrayWidgetTest, ModifierClickPinsAndUnpinsAndPersists) {
  auto widget = make(inlineOptions());
  widget->onIconModified(makeIcon(1, 1, "A"));
  widget->onIconModified(makeIcon(2, 1, "B"));
  TrayIconButton* b = widget->icons().back();

  QTest::mouseClick(b, Qt::LeftButton, Qt::ControlModifier);
  EXPECT_TRUE(b->isPinned);
  EXPECT_EQ(keysIn(widget->pinnedLayout()), QStringList{"B"});
  EXPECT_EQ(keysIn(widget->unpinnedLayout()), QStringList{"A"});
  EXPECT_TRUE(api_.sent.empty());  // a pinning click is not forwarded to the app
  EXPECT_TRUE(b->isVisible());
  EXPECT_TRUE(widget->pinnedWidget()->isVisible());
  EXPECT_EQ(
      readState(),
      "{\r\n"
      "  \"C:\\\\Apps\\\\B.exe\": {\r\n    \"is_pinned\": true,\r\n    \"index\": 0\r\n  },\r\n"
      "  \"C:\\\\Apps\\\\A.exe\": {\r\n    \"is_pinned\": false,\r\n    \"index\": 0\r\n  }\r\n"
      "}");

  QTest::mouseClick(b, Qt::LeftButton, Qt::ControlModifier);
  EXPECT_FALSE(b->isPinned);
  EXPECT_EQ(keysIn(widget->unpinnedLayout()), (QStringList{"A", "B"}));
  // With nothing left pinned the pinned container hides.
  EXPECT_TRUE(widget->pinnedWidget()->isHidden());
}

TEST_F(SystrayWidgetTest, PinningWorksFromThePopupGrid) {
  auto widget = make(userOptions());
  widget->onIconModified(makeIcon(1, 1, "A"));
  widget->onIconModified(makeIcon(2, 1, "B"));
  EXPECT_EQ(keysIn(widget->unpinnedLayout()), (QStringList{"A", "B"}));
  QTest::mouseClick(widget->icons().front().data(), Qt::LeftButton, Qt::ControlModifier);
  EXPECT_EQ(keysIn(widget->pinnedLayout()), QStringList{"A"});
  EXPECT_EQ(keysIn(widget->unpinnedLayout()), QStringList{"B"});
}

TEST_F(SystrayWidgetTest, SaveMergesIntoWhatTheFileAlreadyHolds) {
  writeState(
      "{\r\n  \"other-screen-key\": {\r\n    \"is_pinned\": true,\r\n    \"index\": 4\r\n  },\r\n"
      "  \"C:\\\\Apps\\\\A.exe\": {\r\n    \"is_pinned\": true,\r\n    \"index\": 9\r\n  }\r\n}");
  auto widget = make(inlineOptions());
  // Loading renumbers the saved order (other 0, A 1); A arrives saved as pinned, so it is pinned.
  widget->onIconModified(makeIcon(1, 1, "A"));
  widget->saveState();
  EXPECT_EQ(
      readState(),
      "{\r\n"
      "  \"other-screen-key\": {\r\n    \"is_pinned\": true,\r\n    \"index\": 0\r\n  },\r\n"
      "  \"C:\\\\Apps\\\\A.exe\": {\r\n    \"is_pinned\": true,\r\n    \"index\": 1\r\n  }\r\n"
      "}");
}

TEST_F(SystrayWidgetTest, AFileThatIsNotJsonIsIgnored) {
  writeState("not json at all");
  auto widget = make(inlineOptions());
  EXPECT_TRUE(widget->currentState().empty());
  widget->onIconModified(makeIcon(1, 1, "A"));
  EXPECT_FALSE(widget->icons().front()->isPinned);
}

TEST_F(SystrayWidgetTest, StateFileNameFollowsTheScreenIdentity) {
  auto widget = make(inlineOptions());
  EXPECT_EQ(QFileInfo(widget->stateFilePath()).fileName(), "systray_state_TESTSCREEN.json");
  EXPECT_EQ(cleanScreenId("DEL", "\\\\.\\DISPLAY1 (HDMI)", "ab-12"), "DELDISPLAY1HDMIAB12");
  EXPECT_EQ(cleanScreenId("", "offscreen", ""), "OFFSCREEN");
}

TEST(SystrayStateStore, WritesPythonJsonDumpsFormat) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  SystrayStateStore store(dir.path(), "S1");
  IconStateMap state;
  state.set("C:\\Users\\jande\\caf\u00E9.exe", IconState{true, 0});
  state.set("7820ae75-23e3-4229-82c1-e41cb67d5b9c", IconState{false, 1});
  ASSERT_TRUE(store.save(state));
  QFile file(store.path());
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  // json.dumps(indent=2) in text mode on Windows: ASCII only (\u escapes), CRLF, no final newline.
  EXPECT_EQ(file.readAll(),
            "{\r\n"
            "  \"C:\\\\Users\\\\jande\\\\caf\\u00e9.exe\": {\r\n    \"is_pinned\": true,\r\n"
            "    \"index\": 0\r\n  },\r\n"
            "  \"7820ae75-23e3-4229-82c1-e41cb67d5b9c\": {\r\n    \"is_pinned\": false,\r\n"
            "    \"index\": 1\r\n  }\r\n"
            "}");
  const IconStateMap loaded = store.load();
  ASSERT_EQ(loaded.size(), 2u);
  EXPECT_EQ(loaded.entries().front().first, "C:\\Users\\jande\\caf\u00E9.exe");
  EXPECT_TRUE(loaded.entries().front().second.isPinned);
}

TEST(SystrayStateStore, LoadSkipsMalformedEntriesAndReadsLooseTypes) {
  QTemporaryDir dir;
  SystrayStateStore store(dir.path(), "S2");
  QFile file(store.path());
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"({"a": {"is_pinned": 1, "index": "3"}, "b": {"is_pinned": false}, "c": 5, )"
             R"("d": {"is_pinned": 0, "index": 2}})");
  file.close();
  const IconStateMap loaded = store.load();
  ASSERT_EQ(loaded.size(), 2u);
  EXPECT_EQ(loaded.find("a")->index, 3);
  EXPECT_TRUE(loaded.find("a")->isPinned);
  EXPECT_FALSE(loaded.find("d")->isPinned);
}

// update_current_state, worked by hand from the Python source.
TEST(SystrayUpdateState, OnScreenKeysTakeTheSavedSlots) {
  IconStateMap state;
  state.set("a", {true, 0});
  state.set("b", {true, 1});
  state.set("c", {false, 0});
  // b and a swapped on screen; d is new; c vanished from the layouts.
  updateCurrentState(state, {"b", "a"}, {"d"});
  EXPECT_EQ(*state.find("b"), (IconState{true, 0}));
  EXPECT_EQ(*state.find("a"), (IconState{true, 1}));
  // c is unpinned and not on screen: it keeps its slot, the new d goes behind it.
  EXPECT_EQ(*state.find("c"), (IconState{false, 0}));
  EXPECT_EQ(*state.find("d"), (IconState{false, 1}));
}

TEST(SystrayUpdateState, AMovedIconLeavesItsOldContainer) {
  IconStateMap state;
  state.set("a", {false, 0});
  state.set("b", {false, 1});
  updateCurrentState(state, {"a"}, {"b"});
  EXPECT_EQ(*state.find("a"), (IconState{true, 0}));
  EXPECT_EQ(*state.find("b"), (IconState{false, 0}));
}

TEST(SystrayUpdateState, EmptyLayoutsOnlyRenumber) {
  IconStateMap state;
  state.set("x", {false, 7});
  state.set("y", {false, 3});
  state.set("z", {true, 9});
  updateCurrentState(state, {}, {});
  EXPECT_EQ(*state.find("y"), (IconState{false, 0}));
  EXPECT_EQ(*state.find("x"), (IconState{false, 1}));
  EXPECT_EQ(*state.find("z"), (IconState{true, 0}));
}

TEST(SystrayUpdateState, UpdateIconDataHonoursFlags) {
  TrayIconData old = makeIcon(0x10, 1, "App", std::nullopt, "old tip");
  old.uVersion = 3;
  TrayIconData incoming;
  incoming.messageType = kNimModify;
  incoming.uFlags = kNifTip | kNifInfo;
  incoming.szTip = "new tip";
  incoming.szInfo = "body";
  incoming.szInfoTitle = "title";
  incoming.dwInfoFlags = 2;
  incoming.uVersion = 9;             // out of range: ignored
  incoming.uCallbackMessage = 1234;  // no NIF_MESSAGE: ignored
  incoming.exe = "App";
  incoming.exePath = old.exePath;
  updateIconData(old, incoming);
  EXPECT_EQ(old.szTip, "new tip");
  EXPECT_EQ(old.szInfo, "body");
  EXPECT_EQ(old.dwInfoFlags, 2u);
  EXPECT_EQ(old.uVersion, 3u);
  EXPECT_EQ(old.uCallbackMessage, 0x8000u);
  EXPECT_EQ(old.hWnd, 0x10u);  // incoming window 0 keeps the old one
  EXPECT_EQ(old.uID, 1u);
  incoming.uVersion = 4;
  updateIconData(old, incoming);
  EXPECT_EQ(old.uVersion, 4u);
}

// --- click forwarding ------------------------------------------------------------------------

constexpr std::uint32_t kLDown = 0x201, kLUp = 0x202, kLDbl = 0x203, kRDown = 0x204, kRUp = 0x205,
                        kMDown = 0x207, kMUp = 0x208;

TEST_F(SystrayWidgetTest, LeftClickSendsDownAndUpToTheOwner) {
  auto widget = make(inlineOptions());
  api_.windows = {0x10};
  widget->onIconModified(makeIcon(0x10, 7, "App"));
  TrayIconButton* icon = widget->icons().front();
  QTest::mouseClick(icon, Qt::LeftButton);
  // Legacy callback style (version 0): wParam = uID, lParam = LOWORD message.
  const std::vector<FakeApi::Message> expected = {{0x10, 0x8000, 7, kLDown},
                                                  {0x10, 0x8000, 7, kLUp}};
  EXPECT_EQ(api_.sent, expected);
  EXPECT_EQ(api_.allowed, (std::vector<std::uint64_t>{0x10, 0x10}));
}

TEST_F(SystrayWidgetTest, RightAndMiddleClicks) {
  auto widget = make(inlineOptions());
  api_.windows = {0x10};
  widget->onIconModified(makeIcon(0x10, 7, "App"));
  TrayIconButton* icon = widget->icons().front();
  QTest::mouseClick(icon, Qt::RightButton);
  QTest::mouseClick(icon, Qt::MiddleButton);
  const std::vector<FakeApi::Message> expected = {{0x10, 0x8000, 7, kRDown},
                                                  {0x10, 0x8000, 7, kRUp},
                                                  {0x10, 0x8000, 7, kMDown},
                                                  {0x10, 0x8000, 7, kMUp}};
  EXPECT_EQ(api_.sent, expected);
}

TEST_F(SystrayWidgetTest, Version3AddsSelectAndContextMenuNotifications) {
  auto widget = make(inlineOptions());
  api_.windows = {0x10};
  TrayIconData data = makeIcon(0x10, 7, "App");
  data.uVersion = 3;
  widget->onIconModified(data);
  TrayIconButton* icon = widget->icons().front();
  QTest::mouseClick(icon, Qt::LeftButton);
  QTest::mouseClick(icon, Qt::RightButton);
  const std::vector<FakeApi::Message> expected = {
      {0x10, 0x8000, 7, kLDown}, {0x10, 0x8000, 7, kLUp}, {0x10, 0x8000, 7, kNinSelect},
      {0x10, 0x8000, 7, kRDown}, {0x10, 0x8000, 7, kRUp}, {0x10, 0x8000, 7, kNinContextMenu}};
  EXPECT_EQ(api_.sent, expected);
}

TEST_F(SystrayWidgetTest, Version4PacksCursorAndIdentifier) {
  auto widget = make(inlineOptions());
  api_.windows = {0x10};
  api_.cursor = {300, 40};
  TrayIconData data = makeIcon(0x10, 7, "App");
  data.uVersion = 4;
  widget->onIconModified(data);
  QTest::mouseClick(widget->icons().front().data(), Qt::LeftButton);
  const std::uint64_t cursor = static_cast<std::uint32_t>(packI32(300, 40));
  ASSERT_EQ(api_.sent.size(), 3u);
  EXPECT_EQ(api_.sent[0], (FakeApi::Message{0x10, 0x8000, cursor, packI32(kLDown, 7)}));
  EXPECT_EQ(api_.sent[1], (FakeApi::Message{0x10, 0x8000, cursor, packI32(kLUp, 7)}));
  EXPECT_EQ(api_.sent[2], (FakeApi::Message{0x10, 0x8000, cursor, packI32(kNinSelect, 7)}));
}

TEST_F(SystrayWidgetTest, ClicksNeverReachDeadWindowsOrIconsWithoutACallback) {
  auto widget = make(inlineOptions());
  widget->onIconModified(makeIcon(0x10, 7, "Dead"));  // no such window
  QTest::mouseClick(widget->icons().front().data(), Qt::LeftButton);
  EXPECT_TRUE(api_.sent.empty());
  EXPECT_TRUE(api_.allowed.empty());

  api_.windows = {0x20};
  TrayIconData silent = makeIcon(0x20, 1, "Silent");
  silent.uFlags &= ~kNifMessage;
  silent.uCallbackMessage = 0;
  widget->onIconModified(silent);
  QTest::mouseClick(widget->icons().back().data(), Qt::LeftButton);
  EXPECT_TRUE(api_.sent.empty());
  EXPECT_EQ(api_.allowed.size(), 2u);  // permission is still granted, the message is not sent
}

TEST_F(SystrayWidgetTest, DoubleClickSendsTheDoubleClickMessage) {
  auto widget = make(inlineOptions());
  api_.windows = {0x10};
  widget->onIconModified(makeIcon(0x10, 7, "App"));
  TrayIconButton* icon = widget->icons().front();
  icon->sendAction(kLDbl);
  const std::vector<FakeApi::Message> expected = {{0x10, 0x8000, 7, kLDbl}};
  EXPECT_EQ(api_.sent, expected);
}

TEST_F(SystrayWidgetTest, ReleaseAfterADragSizedMoveDoesNotClick) {
  auto widget = make(inlineOptions());
  api_.windows = {0x10};
  widget->onIconModified(makeIcon(0x10, 7, "App"));
  TrayIconButton* icon = widget->icons().front();
  icon->resize(20, 20);
  QTest::mousePress(icon, Qt::LeftButton, {}, QPoint(2, 2));
  QTest::mouseRelease(icon, Qt::LeftButton, {}, QPoint(18, 18));
  EXPECT_TRUE(api_.sent.empty());
}

// --- containers, drag and drop, popup --------------------------------------------------------

TEST_F(SystrayWidgetTest, DroppingReordersInsideTheBar) {
  auto widget = make(inlineOptions());
  for (int i = 0; i < 3; ++i) widget->onIconModified(makeIcon(i + 1, 1, QString("I%1").arg(i)));
  TrayDropWidget* unpinned = widget->unpinnedWidget();
  unpinned->resize(120, 24);
  for (auto& icon : widget->icons()) icon->setFixedSize(30, 24);
  unpinned->layout()->activate();
  ASSERT_EQ(keysIn(unpinned->layout()), (QStringList{"I0", "I1", "I2"}));
  const auto beyondLast = [&] {
    const auto icons = unpinned->allIcons();
    return QPoint(icons.back()->geometry().right() + 10, 10);
  };

  QSignalSpy moved(widget->icons().front().data(), &TrayIconButton::iconMoved);
  QSignalSpy ended(unpinned, &TrayDropWidget::dragEnded);
  // Drop I0 to the right of I2: it lands last.
  EXPECT_TRUE(unpinned->dropIcon(widget->icons().front(), beyondLast()));
  EXPECT_EQ(keysIn(unpinned->layout()), (QStringList{"I1", "I2", "I0"}));
  EXPECT_EQ(moved.count(), 1);
  EXPECT_EQ(ended.count(), 1);

  // Dropping next to the icon's own slot changes nothing and signals no move.
  unpinned->layout()->activate();
  moved.clear();
  EXPECT_TRUE(unpinned->dropIcon(widget->icons().front(), beyondLast()));
  EXPECT_EQ(keysIn(unpinned->layout()), (QStringList{"I1", "I2", "I0"}));
  EXPECT_EQ(moved.count(), 0);
}

TEST_F(SystrayWidgetTest, DroppingOnThePinnedBarPinsTheIcon) {
  auto widget = make(inlineOptions());
  widget->onIconModified(makeIcon(1, 1, "A"));
  widget->onIconModified(makeIcon(2, 1, "B"));
  TrayIconButton* b = widget->icons().back();
  widget->pinnedWidget()->show();
  EXPECT_TRUE(widget->pinnedWidget()->dropIcon(b, QPoint(0, 0)));
  EXPECT_TRUE(b->isPinned);
  EXPECT_EQ(keysIn(widget->pinnedLayout()), QStringList{"B"});
  EXPECT_EQ(keysIn(widget->unpinnedLayout()), QStringList{"A"});
  EXPECT_EQ(QFile::exists(stateFile()), true);
}

TEST_F(SystrayWidgetTest, DroppingIntoThePopupGridInsertsAfterTheHoveredHalf) {
  auto widget = make(userOptions());
  for (int i = 0; i < 3; ++i) widget->onIconModified(makeIcon(i + 1, 1, QString("I%1").arg(i)));
  for (auto& icon : widget->icons()) icon->setFixedSize(30, 24);
  widget->popup()->open(QStringLiteral("x"));
  TrayDropWidget* grid = widget->popup()->gridWidget();
  grid->layout()->activate();
  QCoreApplication::processEvents();
  ASSERT_EQ(keysIn(grid->layout()), (QStringList{"I0", "I1", "I2"}));
  // The pointer on I2's right half puts the dragged icon after it.
  const QPoint onI2 = widget->icons().at(2)->geometry().center() + QPoint(5, 0);
  grid->dropIcon(widget->icons().front(), onI2);
  EXPECT_EQ(keysIn(grid->layout()), (QStringList{"I1", "I2", "I0"}));
}

TEST_F(SystrayWidgetTest, PopupOpensAtTheButtonAndFollowsItsState) {
  auto widget = make(userOptions());
  widget->onIconModified(makeIcon(1, 1, "A"));
  SystrayPopup* popup = widget->popup();
  QPushButton* button = widget->visibilityButton();

  button->click();
  EXPECT_TRUE(popup->isOpen());
  EXPECT_TRUE(button->isChecked());
  EXPECT_EQ(button->text(), QString::fromUtf8("\xEE\xA5\xB1"));
  EXPECT_EQ(popup->windowFlags() & Qt::WindowType_Mask, Qt::Tool);
  EXPECT_TRUE(popup->windowFlags() & Qt::WindowStaysOnTopHint);
  EXPECT_TRUE(popup->testAttribute(Qt::WA_TranslucentBackground));

  QSignalSpy closed(popup, &SystrayPopup::closed);
  popup->hideAnimated();
  // The button follows at once, the window after the fade.
  EXPECT_FALSE(button->isChecked());
  EXPECT_EQ(button->text(), QString::fromUtf8("\xEE\xA5\xB2"));
  EXPECT_TRUE(popup->isClosing());
  ASSERT_TRUE(closed.wait(2000));
  EXPECT_FALSE(popup->isVisible());
}

TEST_F(SystrayWidgetTest, DraggingFromTheBarOpensThePopupAndMarksDropTargets) {
  auto widget = make(userOptions());
  emit widget->pinnedWidget()->dragStarted(24);
  EXPECT_TRUE(widget->popup()->isOpen());
  // An empty pinned container shows during the drag, sized for one icon, flagged as a target.
  EXPECT_TRUE(widget->pinnedWidget()->isVisible());
  EXPECT_TRUE(widget->pinnedWidget()->property("forceshow").toBool());
  EXPECT_EQ(widget->pinnedWidget()->minimumWidth(), 24);
  EXPECT_TRUE(widget->pinnedWidget()->property("class").toString().contains("drop-target"));
  EXPECT_TRUE(widget->popup()->gridWidget()->property("class").toString().contains("drop-target"));

  emit widget->pinnedWidget()->dragEnded();
  EXPECT_EQ(widget->pinnedWidget()->minimumWidth(), 16);
  EXPECT_FALSE(widget->pinnedWidget()->property("class").toString().contains("drop-target"));
  EXPECT_FALSE(widget->popup()->gridWidget()->property("class").toString().contains("drop-target"));
  EXPECT_TRUE(widget->pinnedWidget()->isHidden());
}

TEST_F(SystrayWidgetTest, PinnedContainerShowsOnlyWhileItHoldsVisibleIcons) {
  auto widget = make(inlineOptions());
  widget->onIconModified(makeIcon(1, 1, "A"));
  widget->updatePinnedWidgetVisibility();
  EXPECT_TRUE(widget->pinnedWidget()->isHidden());
  QTest::mouseClick(widget->icons().front().data(), Qt::LeftButton, Qt::ControlModifier);
  EXPECT_FALSE(widget->pinnedWidget()->isHidden());
  widget->updatePinnedWidgetVisibility(true);
  EXPECT_FALSE(widget->pinnedWidget()->property("forceshow").toBool());  // not empty
}

TEST_F(SystrayWidgetTest, RefreshBroadcastsTaskbarCreated) {
  auto widget = make(userOptions());
  widget->refreshSystray();
  EXPECT_EQ(api_.broadcasts, 1);
}

TEST(SystrayPopupPosition, MatchesPythonSetPosition) {
  const QRect screen(0, 0, 1920, 1080);
  const QPoint button(1800, 4);
  const QSize buttonSize(24, 32);
  const QPoint bar(0, 0);
  // down + center: under the bar, centred on the button.
  EXPECT_EQ(computeSystrayPopupPosition(button, buttonSize, bar, 40, QSize(100, 60), screen,
                                        "center", "down", 0, 6),
            QPoint(1800 + (24 - 100) / 2, 46));
  // left: the popup's left edge on the button's; the right screen edge clamps it.
  EXPECT_EQ(computeSystrayPopupPosition(button, buttonSize, bar, 40, QSize(300, 60), screen, "left",
                                        "down", 0, 6),
            QPoint(1919 - 300, 46));
  // right: the popup's right edge on the button's.
  EXPECT_EQ(computeSystrayPopupPosition(button, buttonSize, bar, 40, QSize(100, 60), screen,
                                        "right", "down", -3, 6),
            QPoint(1800 + 24 - 100 - 3, 46));
  // up: above the bar, clamped to the top.
  EXPECT_EQ(computeSystrayPopupPosition(QPoint(10, 1040), buttonSize, QPoint(0, 1040), 40,
                                        QSize(100, 60), screen, "left", "up", 0, 6),
            QPoint(10, 1040 - 60 - 6));
  EXPECT_EQ(computeSystrayPopupPosition(button, buttonSize, bar, 40, QSize(100, 60), screen, "left",
                                        "up", 0, 6),
            QPoint(1800, 0));
  // An odd negative width difference floors like Python's //.
  EXPECT_EQ(computeSystrayPopupPosition(button, buttonSize, bar, 40, QSize(25, 60), screen,
                                        "center", "down", 0, 0)
                .x(),
            1799);
  // No screen: no clamping.
  EXPECT_EQ(computeSystrayPopupPosition(button, buttonSize, bar, 40, QSize(300, 60), std::nullopt,
                                        "left", "down", 0, 6),
            QPoint(1800, 46));
}

// --- host selection --------------------------------------------------------------------------

struct CountingHost : public TrayHost {
  explicit CountingHost(QString name) : name_(std::move(name)) {}
  void start() override {}
  void stop() override {}
  QString mode() const override { return name_; }
  QString name_;
};

struct FactoryProbe {
  int hooksMade = 0, legacyMade = 0;
  bool hookStarts = true;
  QString dll = "C:\\fake\\hook.dll";
  QString reason;
  TrayHostFactories factories() {
    TrayHostFactories f;
    f.findHookDll = [this](const QString&, QString* why) {
      if (why) *why = reason;
      return dll;
    };
    f.makeHook = [this](const QString&) -> std::unique_ptr<TrayHost> {
      ++hooksMade;
      if (!hookStarts) return nullptr;
      return std::make_unique<CountingHost>("hook");
    };
    f.makeLegacy = [this]() -> std::unique_ptr<TrayHost> {
      ++legacyMade;
      return std::make_unique<CountingHost>("legacy");
    };
    return f;
  }
};

TEST(SystrayHostChoice, HookWhenUsable) {
  FactoryProbe probe;
  auto host = createTrayHost(true, "app", probe.factories());
  EXPECT_EQ(host->mode(), "hook");
  EXPECT_EQ(probe.legacyMade, 0);
}

TEST(SystrayHostChoice, UseHookFalseNeverTouchesTheHook) {
  FactoryProbe probe;
  auto host = createTrayHost(false, "app", probe.factories());
  EXPECT_EQ(host->mode(), "legacy");
  EXPECT_EQ(probe.hooksMade, 0);
}

TEST(SystrayHostChoice, MissingDllFallsBackToLegacy) {
  FactoryProbe probe;
  probe.dll.clear();
  probe.reason = "YASBNativeTrayHook.dll not found next to the executable";
  auto host = createTrayHost(true, "app", probe.factories());
  EXPECT_EQ(host->mode(), "legacy");
  EXPECT_EQ(probe.hooksMade, 0);
  EXPECT_EQ(probe.legacyMade, 1);
}

TEST(SystrayHostChoice, EmulationAndFailedHookFallBackToo) {
  FactoryProbe emulated;
  emulated.dll.clear();
  emulated.reason = "running under emulation";
  EXPECT_EQ(createTrayHost(true, "app", emulated.factories())->mode(), "legacy");

  FactoryProbe broken;
  broken.hookStarts = false;
  auto host = createTrayHost(true, "app", broken.factories());
  EXPECT_EQ(host->mode(), "legacy");
  EXPECT_EQ(broken.hooksMade, 1);
}

// A widget built without injected deps (the production path) on the offscreen platform, which is
// what the whole test suite and --screenshot run on, must never reach for the real tray.
TEST_F(SystrayWidgetTest, ProductionPathCreatesNoHostOnTheOffscreenPlatform) {
  ASSERT_EQ(QGuiApplication::platformName(), QStringLiteral("offscreen"));
  const auto validated = yasb::config::SchemaValidator::embedded().validateWidgetOptions(
      "yasb.systray.SystrayWidget", yasb::config::OJson::parse(userOptions().dump()));
  ASSERT_TRUE(validated.ok());
  const json options = json::parse(validated.value.dump());
  WidgetContext context{"systray",
                        "yasb.systray.SystrayWidget",
                        options,
                        {},
                        {},
                        eventBus_,
                        globalState_,
                        pollingWorkers_,
                        options.get<yasb::config::options::SystrayWidgetConfig>()};
  SystrayWidget first(context);
  SystrayWidget second(context);
  auto* service = pollingWorkers_.service<SystrayService>("systray", [] {
    ADD_FAILURE() << "the widget should have registered the shared service";
    return nullptr;
  });
  ASSERT_NE(service, nullptr);
  EXPECT_EQ(service->host(), nullptr);
  EXPECT_NE(service->windowApi(), nullptr);
  // One shared service: the first widget answers the refresh request, the second does not.
  EXPECT_FALSE(service->claimRefreshSignal());
}

TEST(SystrayHostChoice, ServiceHandsTheRefreshSignalToOneWidget) {
  FactoryProbe probe;
  SystrayService service(true, "app", probe.factories());
  EXPECT_TRUE(service.claimRefreshSignal());
  EXPECT_FALSE(service.claimRefreshSignal());
  ASSERT_NE(service.windowApi(), nullptr);
  service.stop();
}

// --- parity vectors (tools/parity/vec_systray.py) ----------------------------------------------

json loadVectors() {
  std::ifstream in(std::string(YASB_VECTORS_DIR) + "/labels/systray.json", std::ios::binary);
  EXPECT_TRUE(in.good());
  return json::parse(in);
}

std::uint32_t flagsFrom(const json& names) {
  std::uint32_t flags = 0;
  for (const auto& name : names) {
    const std::string n = name.get<std::string>();
    if (n == "message") flags |= kNifMessage;
    if (n == "icon") flags |= kNifIcon;
    if (n == "tip") flags |= kNifTip;
    if (n == "state") flags |= kNifState;
    if (n == "info") flags |= kNifInfo;
  }
  return flags;
}

QString qstr(const json& value) { return QString::fromStdString(value.get<std::string>()); }

TrayIconData iconFromSpec(const json& spec) {
  TrayIconData data;
  data.hWnd = spec.at("hwnd").get<std::uint64_t>();
  data.uID = spec.value("uid", 1u);
  data.uFlags = flagsFrom(spec.value("flags", json::array({"message", "icon", "tip"})));
  if (spec.contains("guid")) {
    data.guid = QUuid(qstr(spec.at("guid")));
    data.uFlags |= kNifGuid;
  }
  data.dwState = spec.value("state", 0u);
  data.szTip = qstr(spec.value("tip", json("")));
  data.uVersion = spec.value("version", 0u);
  data.exe = qstr(spec.at("exe"));
  data.exePath = QStringLiteral("C:\\Apps\\") + data.exe + QStringLiteral(".exe");
  if (data.uFlags & kNifMessage) data.uCallbackMessage = 0x8000;
  if (data.uFlags & kNifIcon) {
    data.hIcon = 1;
    data.iconWidth = 32;
    data.iconHeight = 32;
    data.iconRgba = QByteArray(32 * 32 * 4, '\x40');
  }
  return data;
}

std::string iconKey(const TrayIconButton* icon) {
  return (icon->info.guid ? icon->info.guid->toString(QUuid::WithoutBraces) : icon->info.exePath)
      .toStdString();
}

json iconSnapshot(const TrayIconButton* icon) {
  const auto* tooltip = icon->findChild<TooltipEventFilter*>();
  return {{"class", icon->property("class").toString().toStdString()},
          {"exe", icon->info.exe.toStdString()},
          {"has_icon", !icon->icon().isNull()},
          {"hidden", icon->isHidden()},
          {"key", iconKey(icon)},
          {"pinned", icon->isPinned},
          {"tooltip", tooltip ? json(tooltip->text().toStdString()) : json(nullptr)}};
}

json iconsSnapshot(QLayout* layout) {
  json result = json::array();
  for (int i = 0; i < layout->count(); ++i) {
    if (auto* icon = qobject_cast<TrayIconButton*>(layout->itemAt(i)->widget()))
      result.push_back(iconSnapshot(icon));
  }
  return result;
}

json widgetSnapshot(SystrayWidget& widget, bool popup) {
  QLayout* layout = widget.labelContainer()->layout();
  json order = json::array();
  for (int i = 0; i < layout->count(); ++i) {
    QWidget* item = layout->itemAt(i)->widget();
    order.push_back(item == widget.pinnedWidget()       ? "pinned"
                    : item == widget.visibilityButton() ? "button"
                                                        : "unpinned");
  }
  json state = json::array();
  for (const auto& [key, value] : widget.currentState().entries()) {
    state.push_back(
        json::array({key.toStdString(), {{"is_pinned", value.isPinned}, {"index", value.index}}}));
  }
  QFile file(widget.stateFilePath());
  json stateFile = nullptr;
  if (file.open(QIODevice::ReadOnly)) stateFile = QString::fromUtf8(file.readAll()).toStdString();

  QPushButton* button = widget.visibilityButton();
  json unpinnedContainer = nullptr;
  if (!popup) {
    unpinnedContainer = {
        {"class", widget.unpinnedWidget()->property("class").toString().toStdString()},
        {"hidden", widget.unpinnedWidget()->isHidden()}};
  }
  return {{"widget_hidden", widget.isHidden()},
          {"frame_class", widget.widgetFrame()->property("class").toString().toStdString()},
          {"container_class", widget.labelContainer()->property("class").toString().toStdString()},
          {"container_order", order},
          {"button",
           {{"text", button->text().toStdString()},
            {"checked", button->isChecked()},
            {"hidden", button->isHidden()},
            {"class", button->property("class").toString().toStdString()}}},
          {"pinned_container",
           {{"class", widget.pinnedWidget()->property("class").toString().toStdString()},
            {"hidden", widget.pinnedWidget()->isHidden()},
            {"forceshow", widget.pinnedWidget()->property("forceshow").toBool()},
            {"min_width", widget.pinnedWidget()->minimumWidth()}}},
          {"unpinned_container", unpinnedContainer},
          {"popup_open", popup ? json(widget.popup()->isOpen()) : json(nullptr)},
          {"pinned", iconsSnapshot(widget.pinnedLayout())},
          {"unpinned", iconsSnapshot(widget.unpinnedLayout())},
          {"icon_count", widget.icons().size()},
          {"state", state},
          {"state_file", stateFile},
          {"state_file_name", QFileInfo(widget.stateFilePath()).fileName().toStdString()}};
}

void applyStep(SystrayWidget& widget, const json& step) {
  if (step.contains("icon")) {
    widget.onIconModified(iconFromSpec(step.at("icon")));
  } else if (step.contains("delete")) {
    const json& spec = step.at("delete");
    TrayIconData data;
    data.hWnd = spec.at("hwnd").get<std::uint64_t>();
    data.uID = spec.value("uid", 1u);
    if (spec.contains("guid")) data.guid = QUuid(qstr(spec.at("guid")));
    widget.onIconDeleted(data);
  } else if (step.contains("pin")) {
    const QString exe = qstr(step.at("pin"));
    for (const auto& icon : widget.icons()) {
      if (icon && icon->info.exe == exe) {
        emit icon->pinnedChanged(icon);
        return;
      }
    }
    ADD_FAILURE() << "no icon " << exe.toStdString();
  } else if (step.contains("sort")) {
    widget.sortIcons();
  } else if (step.contains("save")) {
    widget.saveState();
  } else if (step.contains("load")) {
    widget.loadState();
  } else if (step.contains("update_pinned_visibility")) {
    widget.updatePinnedWidgetVisibility(step.at("update_pinned_visibility").get<bool>());
  } else if (step.contains("drag_started")) {
    const json& spec = step.at("drag_started");
    const int width = spec.at("width").get<int>();
    if (spec.at("pinned").get<bool>())
      emit widget.pinnedWidget()->dragStarted(width);
    else if (widget.popup())
      emit widget.popup()->gridWidget()->dragStarted(width);
    else
      emit widget.unpinnedWidget()->dragStarted(width);
  } else if (step.contains("drag_ended")) {
    emit widget.pinnedWidget()->dragEnded();
  } else if (step.contains("toggle_button")) {
    widget.visibilityButton()->click();
  } else if (step.contains("hide_popup")) {
    widget.popup()->hide();
  } else {
    ADD_FAILURE() << "unknown step " << step.dump();
  }
}

TEST_F(SystrayWidgetTest, MatchesPythonLabelVectors) {
  const json vectors = loadVectors();
  ASSERT_TRUE(vectors.is_array());
  ASSERT_FALSE(vectors.empty());
  for (const json& scenario : vectors) {
    const std::string name = scenario.at("scenario").get<std::string>();
    SCOPED_TRACE("scenario " + name);
    // The file is named after the screen identity, which offscreen is empty (as for Python).
    const QString stateName = QDir(stateDir_.path()).filePath("systray_state_.json");
    QFile::remove(stateName);
    if (!scenario.at("initial_data").is_null()) {
      QFile seed(stateName);
      ASSERT_TRUE(seed.open(QIODevice::WriteOnly));
      // Python wrote it with json.dumps(indent=2) in text mode; the pair list keeps the order.
      nlohmann::ordered_json seeded = nlohmann::ordered_json::object();
      for (const json& pair : scenario.at("initial_data")) {
        seeded[pair.at(0).get<std::string>()] =
            nlohmann::ordered_json{{"is_pinned", pair.at(1).at("is_pinned").get<bool>()},
                                   {"index", pair.at(1).at("index").get<int>()}};
      }
      QByteArray text = QByteArray::fromStdString(seeded.dump(2, ' ', true));
      text.replace("\n", "\r\n");
      seed.write(text);
    }

    const json& raw = scenario.at("options");
    const auto validated = yasb::config::SchemaValidator::embedded().validateWidgetOptions(
        "yasb.systray.SystrayWidget", yasb::config::OJson::parse(raw.dump()));
    ASSERT_TRUE(validated.ok());
    const json options = json::parse(validated.value.dump());
    // The typed options match what pydantic normalized.
    EXPECT_EQ(options, scenario.at("normalized_options"));

    WidgetContext context{"systray",
                          "yasb.systray.SystrayWidget",
                          options,
                          {},
                          {},
                          eventBus_,
                          globalState_,
                          pollingWorkers_,
                          options.get<yasb::config::options::SystrayWidgetConfig>()};
    FakeHost host;
    SystrayDeps deps;
    deps.host = &host;
    deps.windowApi = &api_;
    deps.stateDir = stateDir_.path();  // the screen identity is the real one: it names the file
    auto widget = std::make_unique<SystrayWidget>(context, deps);
    widget->loadState();
    widget->resize(200, 32);
    widget->show();

    const bool popup = options.at("show_in_popup").get<bool>();
    EXPECT_EQ(widgetSnapshot(*widget, popup), scenario.at("initial")) << "initial";
    int index = 0;
    for (const json& step : scenario.at("steps")) {
      SCOPED_TRACE("step " + std::to_string(index++) + " " + step.at("step").dump());
      applyStep(*widget, step.at("step"));
      EXPECT_EQ(widgetSnapshot(*widget, popup), step.at("snapshot"));
    }
    if (popup) widget->popup()->hide();
  }
}

}  // namespace
