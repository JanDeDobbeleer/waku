#include <gtest/gtest.h>

#include <QApplication>
#include <QCalendarWidget>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <fstream>
#include <sstream>

#include "core/config/schema_validator.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/widget_context.h"
#include "widgets/clock/clock_state.h"
#include "widgets/clock/clock_widget.h"
#include "widgets/clock/py_strftime.h"

using nlohmann::json;
using namespace yasb::widgets;

namespace {

QDateTime utcAt(int year, int month, int day, int hour, int minute, int second) {
  return QDateTime(QDate(year, month, day), QTime(hour, minute, second), QTimeZone::UTC);
}

// Port of the strftime cases the Python build was exercised with (see labels/clock.json for the
// replayed vectors). Wall time is 2026-03-05 03:09:05.000012 at +05:30.
StrftimeInput kolkata() {
  const QDateTime when(QDate(2026, 3, 5), QTime(3, 9, 5), QTimeZone(19800));
  return {when, 12, QStringLiteral("IST")};
}

std::optional<QString> strftime(const QString& format, const QString& locale = QString()) {
  return PyStrftime(locale).format(format, kolkata());
}

json clockOptions(const std::string& label = "{%H:%M}",
                  const std::string& labelAlt = "{%H:%M:%S}") {
  return {{"label", label},
          {"label_alt", labelAlt},
          {"class_name", ""},
          {"update_interval", 1000},
          {"locale", ""},
          {"tooltip", true},
          {"timezones", json::array()},
          {"icons", json::object()},
          {"alarm_icons", {{"enabled", "ON"}, {"disabled", "OFF"}, {"snooze", "ZZ"}}},
          {"calendar",
           {{"blur", false},
            {"round_corners", false},
            {"round_corners_type", "normal"},
            {"border_color", "System"},
            {"alignment", "center"},
            {"direction", "down"},
            {"offset_top", 6},
            {"offset_left", 0},
            {"show_holidays", false},
            {"holiday_color", "#FF6464"},
            {"show_week_numbers", true},
            {"show_years", true},
            {"extended", true}}},
          {"keybindings", json::array()},
          {"callbacks",
           {{"on_left", "toggle_calendar"},
            {"on_middle", "toggle_label"},
            {"on_right", "context_menu"}}}};
}

// Options go through the embedded schema first, as the loader does, so defaults are filled in.
WidgetContext contextFor(const json& raw, EventBus& bus, yasb::bar::GlobalState& state,
                         PollingWorkerRegistry& polling) {
  auto validated = yasb::config::SchemaValidator::embedded().validateWidgetOptions(
      "yasb.clock.ClockWidget", yasb::config::OJson::parse(raw.dump()));
  if (!validated.ok()) throw std::runtime_error("invalid clock options: " + raw.dump());
  json options = json::parse(validated.value.dump());
  TypedWidgetOptions typed = options.get<yasb::config::options::ClockConfig>();
  return {"clock", "yasb.clock.ClockWidget", std::move(options), {}, {}, bus, state,
          polling, std::move(typed)};
}

// Wall clock the test controls: local wall time unless the options name a time zone.
struct FakeClock {
  QDateTime wall = QDateTime(QDate(2026, 10, 28), QTime(23, 59, 5), QTimeZone::UTC);
  int microsecond = 123456;
  bool local = true;
  ClockInstant operator()() const {
    ClockInstant instant;
    instant.microsecond = microsecond;
    instant.utc = local ? QDateTime(wall.date(), wall.time(), QTimeZone::LocalTime).toUTC()
                        : QDateTime(wall.date(), wall.time(), QTimeZone::UTC);
    return instant;
  }
};

json readJson(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return json::parse(in, nullptr, false);
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

json dumpLabels(const std::vector<QLabel*>& labels) {
  json arr = json::array();
  for (QLabel* l : labels) {
    arr.push_back({{"class", l->property("class").toString().toStdString()},
                   {"hidden", l->isHidden()},
                   {"text", l->text().toStdString()}});
  }
  return arr;
}

json snapshot(ClockWidget& w) {
  json order = json::array();
  QLayout* layout = w.labelContainer()->layout();
  for (int i = 0; i < layout->count(); ++i) {
    const auto* label = qobject_cast<QLabel*>(layout->itemAt(i)->widget());
    const auto in = [&](const std::vector<QLabel*>& set) {
      return std::find(set.begin(), set.end(), label) != set.end();
    };
    order.push_back(in(w.labels()) ? "primary" : in(w.labelsAlt()) ? "alt" : "other");
  }
  return {{"alt", dumpLabels(w.labelsAlt())},
          {"container_class", w.labelContainer()->property("class").toString().toStdString()},
          {"frame_class", w.widgetFrame()->property("class").toString().toStdString()},
          {"layout_order", order},
          {"primary", dumpLabels(w.labels())},
          {"widget_hidden", w.isHidden()}};
}

void setWall(FakeClock& clock, const std::string& iso) {
  const QString text = QString::fromStdString(iso);
  const QString whole = text.section(QLatin1Char('.'), 0, 0);
  const QString fraction = text.section(QLatin1Char('.'), 1, 1);
  clock.wall = QDateTime::fromString(whole, Qt::ISODate);
  clock.microsecond = fraction.isEmpty() ? 0 : fraction.leftJustified(6, QLatin1Char('0')).toInt();
}

}  // namespace

class ClockTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    alarmsFile_ = std::filesystem::path(dir_.path().toStdWString()) / "alarms.json";
  }
  ClockSharedState& makeState(ClockSharedState::NowFn now = {}) {
    return ClockSharedState::resetInstance(alarmsFile_, std::move(now));
  }
  void TearDown() override { ClockSharedState::resetInstance(alarmsFile_ / "gone.json"); }

  QTemporaryDir dir_;
  std::filesystem::path alarmsFile_;
  EventBus eventBus_;
  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
};

// ---- strftime -------------------------------------------------------------------------------

TEST(ClockStrftime, MatchesCPythonOnWindows) {
  EXPECT_EQ(strftime("%a, %d %b %H:%M"), "Thu, 05 Mar 03:09");
  EXPECT_EQ(strftime("%f"), "000012");
  EXPECT_EQ(strftime("%z|%:z"), "+0530|+05:30");
  EXPECT_EQ(strftime("%Z"), "IST");
  EXPECT_EQ(strftime("%%z|%%f"), "%z|%f");
  EXPECT_EQ(strftime("%%%z"), "%+0530");
  EXPECT_EQ(strftime("x%fy%Zz"), "x000012yISTz");
  EXPECT_EQ(strftime("%#d %#H %#I %#j"), "5 3 3 64");
  EXPECT_EQ(strftime("%D %F %T %R %C %h %u %V %G %g"),
            "03/05/26 2026-03-05 03:09:05 03:09 20 Mar 4 10 2026 26");
  EXPECT_EQ(strftime("%Ey %Od %Ec"), "26 05 Thu Mar  5 03:09:05 2026");
  EXPECT_EQ(strftime(""), "");
  EXPECT_EQ(strftime("no directives"), "no directives");
}

TEST(ClockStrftime, InvalidDirectivesAreRejectedLikePythonsValueError) {
  for (const char* bad :
       {"%-d",  "%",  "a%", "%E",  "%O", "%Q", "%k", "%l", "%s",  "%P",  "%:", "%:%",
        "%::z", "%#", "%1", "% d", "%+", "%v", "%L", "%N", "%#f", "%-z", "%Ef"}) {
    EXPECT_FALSE(strftime(bad).has_value()) << bad;
  }
}

TEST(ClockStrftime, ArbitraryJunkNeverAbortsTheProcess) {
  const PyStrftime formatter;
  const StrftimeInput input = kolkata();
  for (int c = 1; c < 0x250; ++c) {
    const QString format = QStringLiteral("%") + QChar(c);
    (void)formatter.format(format, input);
    (void)formatter.format(QStringLiteral("%E") + QChar(c), input);
    (void)formatter.format(QStringLiteral("%O") + QChar(c), input);
    (void)formatter.format(QStringLiteral("%#") + QChar(c), input);
  }
  SUCCEED();
}

TEST(ClockStrftime, LocaleNamesDayAndMonthAndFallsBackToC) {
  EXPECT_EQ(strftime("%A %B", "nl-BE"), "donderdag maart");
  EXPECT_EQ(strftime("%A %B", "zz-nope"), "Thursday March");
  EXPECT_EQ(strftime("%A %B", ""), "Thursday March");
}

TEST(ClockStrftime, UtcOffsetFormatsSecondsAndMicroseconds) {
  EXPECT_EQ(formatUtcOffset(0, 0, false), "+0000");
  EXPECT_EQ(formatUtcOffset(-12615, 5, true), "-03:30:15.000005");
  EXPECT_EQ(formatUtcOffset(19800, 0, true), "+05:30");
}

// ---- alarms.json ----------------------------------------------------------------------------

TEST_F(ClockTest, AlarmsFileWrittenByPythonRoundTripsByteForByte) {
  // json.dumps(indent=2, ensure_ascii=False) + int-list collapsing, CRLF from a text-mode write.
  const std::string python =
      "[\r\n  {\r\n    \"time\": \"07:00\",\r\n    \"days\": [0,1,2],\r\n    \"enabled\": true,\r\n"
      "    \"title\": \"Wake up \xC3\xA9\"\r\n  },\r\n  {\r\n    \"time\": \"22:30\",\r\n"
      "    \"days\": [6],\r\n    \"enabled\": false,\r\n    \"title\": \"Bed\",\r\n"
      "    \"extra\": {\r\n      \"k\": [\r\n        \"a\"\r\n      ]\r\n    }\r\n  }\r\n]";
  writeFile(alarmsFile_, python);
  ClockSharedState& state = makeState();
  ASSERT_EQ(state.alarms().size(), 2u);
  EXPECT_EQ(alarmTime(state.alarms()[0]), "07:00");
  EXPECT_EQ(alarmDays(state.alarms()[0]), (std::vector<int>{0, 1, 2}));
  EXPECT_FALSE(alarmEnabled(state.alarms()[1]));
  EXPECT_EQ(alarmTitle(state.alarms()[0]), "Wake up \xC3\xA9");
  EXPECT_TRUE(state.save());
  EXPECT_EQ(readFile(alarmsFile_), python);
}

TEST_F(ClockTest, EmptyAndMissingAlarmsFile) {
  ClockSharedState& state = makeState();
  EXPECT_TRUE(state.alarms().empty());
  EXPECT_EQ(serializeAlarms({}), "[]");
  writeFile(alarmsFile_, "not json");
  EXPECT_FALSE(state.load());
  EXPECT_TRUE(state.alarms().empty());
  writeFile(alarmsFile_, "{\"time\": \"07:00\"}");
  EXPECT_FALSE(state.load());
  EXPECT_TRUE(state.alarms().empty());
}

TEST_F(ClockTest, AlarmEditedNativelyIsReadableAsPythonWouldWriteIt) {
  ClockSharedState& state = makeState();
  state.addAlarm({{"time", "07:30"}, {"days", {0, 4}}, {"enabled", true}, {"title", "Gym"}});
  EXPECT_EQ(readFile(alarmsFile_),
            "[\r\n  {\r\n    \"time\": \"07:30\",\r\n    \"days\": [0,4],\r\n"
            "    \"enabled\": true,\r\n    \"title\": \"Gym\"\r\n  }\r\n]");
  const Alarm first = state.alarms().front();
  Alarm edited = first;
  edited["time"] = "08:00";
  edited["enabled"] = false;
  EXPECT_TRUE(state.updateAlarm(first, edited));
  const json onDisk = readJson(alarmsFile_.string());
  ASSERT_EQ(onDisk.size(), 1u);
  EXPECT_EQ(onDisk[0]["time"], "08:00");
  EXPECT_EQ(onDisk[0]["enabled"], false);
  EXPECT_TRUE(state.removeAlarm(state.alarms().front()));
  EXPECT_EQ(readFile(alarmsFile_), "[]");
}

TEST(ClockAlarmText, MenuAndTooltipWordingMatchesPython) {
  const Alarm every = {{"time", "07:00"}, {"days", json::array()}, {"title", "x"}};
  const Alarm one = {{"time", "08:15"}, {"days", {2}}, {"enabled", false}};
  const Alarm few = {{"time", "09:00"}, {"days", {4, 0}}};
  EXPECT_EQ(formatAlarmText(every, "ON", "OFF"), "ON  07:00 (Every day)");
  EXPECT_EQ(formatAlarmText(one, "ON", "OFF"), "OFF  08:15 (Wed)");
  EXPECT_EQ(formatAlarmText(few, "ON", "OFF"), "ON  09:00 (Mon, Fri)");
  EXPECT_EQ(alarmsTooltip({}, {}), "No active alarms");
  EXPECT_EQ(alarmsTooltip({every, one, few}, {}),
            "Active Alarms\n07:00 - Every day\n09:00 - Mon, Fri");
  EXPECT_EQ(alarmsTooltip({one}, {}), "No active alarms");
  const QDateTime until(QDate(2026, 1, 1), QTime(7, 5, 9));
  EXPECT_EQ(alarmsTooltip({one}, {{every, until}}), "Snoozed alarm returns at 07:05:09");
  EXPECT_EQ(alarmsTooltip({one}, {{every, until}, {few, until}}), "2 snoozed alarms");
  EXPECT_EQ(alarmsTooltip({Alarm{{"time", "10:00"}, {"days", {2}}}}, {}),
            "Active Alarms\n10:00 - Wednesday");
}

// ---- shared state: alarms, snooze, timer ---------------------------------------------------

namespace {
struct RecordingClient : ClockClient {
  void refreshClock(bool tooltip) override {
    ++refreshes;
    tooltips += tooltip ? 1 : 0;
  }
  void startTicking() override { ++started; }
  void alarmFired(const Alarm& alarm) override { fired.push_back(alarm); }
  void timerFinished() override { ++timerDone; }
  int refreshes = 0, tooltips = 0, started = 0, timerDone = 0;
  std::vector<Alarm> fired;
};
}  // namespace

TEST_F(ClockTest, AlarmFiresOnceOnItsMinuteAndNotOnTheStartupMinute) {
  QDateTime now(QDate(2026, 10, 28), QTime(6, 59, 30));  // Wednesday
  writeFile(alarmsFile_,
            "[{\"time\":\"07:00\",\"days\":[2],\"title\":\"a\"},"
            "{\"time\":\"07:00\",\"days\":[3],\"title\":\"other day\"},"
            "{\"time\":\"06:59\",\"title\":\"startup minute\"}]");
  ClockSharedState& state = makeState([&] { return now; });
  RecordingClient client;
  state.registerClient(&client);
  EXPECT_EQ(client.started, 1);

  state.tick();
  EXPECT_TRUE(client.fired.empty());  // the 06:59 alarm shares the startup minute
  now = QDateTime(QDate(2026, 10, 28), QTime(7, 0, 0));
  state.tick();
  state.tick();
  ASSERT_EQ(client.fired.size(), 1u);
  EXPECT_EQ(alarmTitle(client.fired[0]), "a");
  now = now.addSecs(61);
  state.tick();
  EXPECT_EQ(client.fired.size(), 1u);
  state.unregisterClient(&client);
}

TEST_F(ClockTest, SnoozedAlarmReturnsAndDisablingDropsIt) {
  QDateTime now(QDate(2026, 10, 28), QTime(7, 0, 0));
  writeFile(alarmsFile_, "[{\"time\":\"07:00\",\"days\":[],\"enabled\":true,\"title\":\"a\"}]");
  ClockSharedState& state = makeState([&] { return now; });
  RecordingClient client;
  state.registerClient(&client);
  const Alarm alarm = state.alarms().front();
  state.snooze(alarm, 3);
  ASSERT_EQ(state.snoozed().size(), 1u);
  EXPECT_TRUE(state.hasEnabledAlarms());
  now = now.addSecs(179);
  state.tick();
  EXPECT_EQ(state.snoozed().size(), 1u);
  now = now.addSecs(2);
  state.tick();
  EXPECT_TRUE(state.snoozed().empty());
  ASSERT_EQ(client.fired.size(), 1u);

  state.snooze(alarm, 5);
  Alarm disabled = alarm;
  disabled["enabled"] = false;
  state.updateAlarm(alarm, disabled);
  EXPECT_TRUE(state.snoozed().empty());
  state.unregisterClient(&client);
}

TEST_F(ClockTest, TimerCountsDownFinishesOnceAndCancels) {
  ClockSharedState& state = makeState();
  RecordingClient client;
  state.registerClient(&client);
  state.startTimer(2);
  EXPECT_TRUE(state.timerActive());
  state.tick();
  EXPECT_EQ(state.timerSecondsRemaining(), 1);
  state.tick();
  EXPECT_EQ(state.timerSecondsRemaining(), 0);
  EXPECT_TRUE(state.timerActive());
  state.tick();  // the tick after reaching zero finishes it, as Python does
  EXPECT_FALSE(state.timerActive());
  EXPECT_EQ(client.timerDone, 1);
  state.tick();
  EXPECT_EQ(client.timerDone, 1);
  state.startTimer(30);
  state.cancelTimer();
  EXPECT_FALSE(state.timerActive());
  state.unregisterClient(&client);
}

TEST_F(ClockTest, OnlyTheFirstClientDrivesTheTickAndAlarmUi) {
  ClockSharedState& state = makeState();
  RecordingClient first, second;
  state.registerClient(&first);
  state.registerClient(&second);
  EXPECT_EQ(first.started, 1);
  EXPECT_EQ(second.started, 0);
  state.notifyAll();
  EXPECT_EQ(second.refreshes, 1);
  state.unregisterClient(&first);
  state.unregisterClient(&second);
}

// ---- the widget ----------------------------------------------------------------------------

TEST_F(ClockTest, UserLabelRendersAlarmIconAndRollsOverAtMidnight) {
  FakeClock clock;
  makeState();
  json options = clockOptions("{%a, %d %b %H:%M}<span>{alarm}</span>", "{%H:%M}");
  ClockWidget widget(contextFor(options, eventBus_, globalState_, pollingWorkers_),
                     &ClockSharedState::instance(), std::ref(clock));
  widget.show();
  EXPECT_EQ(widget.labels()[0]->text(), "Wed, 28 Oct 23:59");
  EXPECT_EQ(widget.labels()[0]->property("class").toString(), "label clock_23");
  EXPECT_TRUE(widget.labels()[1]->isHidden());

  clock.wall = clock.wall.addSecs(55);
  widget.runCallback("update_label");
  EXPECT_EQ(widget.labels()[0]->text(), "Thu, 29 Oct 00:00");
  EXPECT_EQ(widget.labels()[0]->property("class").toString(), "label clock_00");

  ClockSharedState::instance().addAlarm({{"time", "07:00"}, {"days", json::array()}});
  EXPECT_EQ(widget.labels()[1]->text(), "ON");
  EXPECT_EQ(widget.labels()[1]->property("class").toString(), "icon alarm");
  EXPECT_FALSE(widget.labels()[1]->isHidden());
}

TEST_F(ClockTest, AltLabelTogglesAndBack) {
  FakeClock clock;
  makeState();
  ClockWidget widget(
      contextFor(clockOptions("{%H:%M}", "{%d-%m}"), eventBus_, globalState_, pollingWorkers_),
      &ClockSharedState::instance(), std::ref(clock));
  widget.show();
  widget.runCallback("toggle_label");
  EXPECT_TRUE(widget.labels()[0]->isHidden());
  EXPECT_EQ(widget.labelsAlt()[0]->text(), "28-10");
  EXPECT_EQ(widget.labelsAlt()[0]->property("class").toString(), "label alt clock_23");
  widget.runCallback("toggle_label");
  EXPECT_FALSE(widget.labels()[0]->isHidden());
  EXPECT_EQ(widget.labels()[0]->text(), "23:59");
}

TEST_F(ClockTest, InvalidFormatsKeepTheRawTemplateAndNeverThrow) {
  FakeClock clock;
  makeState();
  for (const char* label : {"{%-d}", "{%}", "{%Q}", "{%k}", "a{%}b", "{%d %-m}"}) {
    ClockWidget widget(
        contextFor(clockOptions(label, label), eventBus_, globalState_, pollingWorkers_),
        &ClockSharedState::instance(), std::ref(clock));
    EXPECT_EQ(widget.labels()[0]->text().toStdString(), label);
  }
  ClockWidget widget(
      contextFor(clockOptions("{%d} and {%m}"), eventBus_, globalState_, pollingWorkers_),
      &ClockSharedState::instance(), std::ref(clock));
  EXPECT_EQ(widget.labels()[0]->text(), "28} and {10");
}

TEST_F(ClockTest, TimerLabelShowsRemainingTimeAndHidesWhenDone) {
  FakeClock clock;
  ClockSharedState& state = makeState();
  ClockWidget widget(contextFor(clockOptions(), eventBus_, globalState_, pollingWorkers_), &state,
                     std::ref(clock));
  widget.show();
  QLabel* timer = widget.labelContainer()->findChildren<QLabel*>().back();
  ASSERT_EQ(timer->property("class").toString(), "label timer");
  EXPECT_TRUE(timer->isHidden());
  state.startTimer(65);
  EXPECT_FALSE(timer->isHidden());
  EXPECT_EQ(timer->text(), "[01:05]");
  state.startTimer(3725);
  EXPECT_EQ(timer->text(), "[01:02:05]");
  widget.runCallback("toggle_label");
  EXPECT_EQ(timer->property("class").toString(), "label alt timer");
  state.cancelTimer();
  EXPECT_TRUE(timer->isHidden());
}

TEST_F(ClockTest, TimezonesCycleAndInvalidOnesAreIgnored) {
  FakeClock clock;
  clock.local = false;
  makeState();
  json options = clockOptions("{%H:%M %z}");
  options["timezones"] = {"Not/AZone", "UTC", "Asia/Kolkata"};
  ClockWidget widget(contextFor(options, eventBus_, globalState_, pollingWorkers_),
                     &ClockSharedState::instance(), std::ref(clock));
  ASSERT_EQ(widget.timezones().size(), 2u);
  EXPECT_EQ(widget.labels()[0]->text(), "23:59 +0000");
  widget.runCallback("next_timezone");
  EXPECT_EQ(widget.labels()[0]->text(), "05:29 +0530");
  widget.runCallback("next_timezone");
  EXPECT_EQ(widget.labels()[0]->text(), "23:59 +0000");
  widget.setTimezone(std::string("Asia/Kolkata"));
  EXPECT_EQ(widget.labels()[0]->text(), "05:29 +0530");
  EXPECT_EQ(widget.tooltipText().section(QLatin1Char('\n'), 2, 2), "Thu 05:29 (Asia/Kolkata)");
}

TEST_F(ClockTest, TooltipHasDateTimeAndZoneAndAlarmBlock) {
  FakeClock clock;
  ClockSharedState& state = makeState();
  ClockWidget widget(contextFor(clockOptions(), eventBus_, globalState_, pollingWorkers_), &state,
                     std::ref(clock));
  EXPECT_EQ(widget.tooltipText(), "Wednesday, 28 October 2026\n\nWed 23:59 (Local time)");
  state.addAlarm({{"time", "07:00"}, {"days", {0}}});
  EXPECT_EQ(
      widget.tooltipText(),
      "Wednesday, 28 October 2026\n\nWed 23:59 (Local time)\n\nActive Alarms\n07:00 - Monday");
}

TEST_F(ClockTest, CalendarPopupShowsTheConfiguredDateLabels) {
  FakeClock clock;
  makeState();
  ClockWidget widget(contextFor(clockOptions(), eventBus_, globalState_, pollingWorkers_),
                     &ClockSharedState::instance(), std::ref(clock));
  widget.show();
  widget.runCallback("toggle_calendar");
  ASSERT_NE(widget.calendarPopup(), nullptr);
  EXPECT_EQ(widget.calendarPopup()->property("class").toString(), "clock-popup calendar");
  const auto labelText = [&](const char* cssClass) {
    for (QLabel* l : widget.calendarPopup()->findChildren<QLabel*>()) {
      if (l->property("class").toString() == cssClass) return l->text();
    }
    return QString("<missing>");
  };
  EXPECT_EQ(labelText("date-label"), "28");
  EXPECT_EQ(labelText("year-label"), "2026");
  EXPECT_EQ(labelText("week-label"), "Week 44");
  EXPECT_EQ(labelText("upcoming-events-header"), "Upcoming holidays");
  auto* calendar = widget.calendarPopup()->findChild<QCalendarWidget*>();
  ASSERT_NE(calendar, nullptr);
  EXPECT_EQ(calendar->selectedDate(), QDate(2026, 10, 28));
  EXPECT_FALSE(calendar->isNavigationBarVisible());
  calendar->setSelectedDate(QDate(2026, 3, 5));
  emit calendar->clicked(QDate(2026, 3, 5));
  EXPECT_EQ(labelText("date-label"), "5");
  EXPECT_EQ(labelText("week-label"), "Week 10");
}

TEST_F(ClockTest, TimerDialogStartsTheSharedTimer) {
  FakeClock clock;
  ClockSharedState& state = makeState();
  ClockWidget widget(contextFor(clockOptions(), eventBus_, globalState_, pollingWorkers_), &state,
                     std::ref(clock));
  widget.show();
  widget.runCallback("toggle_timer");
  ASSERT_NE(widget.timerPopup(), nullptr);
  auto spins = widget.timerPopup()->findChildren<QSpinBox*>();
  ASSERT_EQ(spins.size(), 2);
  EXPECT_EQ(spins[0]->value(), 5);
  EXPECT_EQ(spins[0]->text(), "05");
  QPushButton* ten = nullptr;
  QPushButton* start = nullptr;
  for (auto* b : widget.timerPopup()->findChildren<QPushButton*>()) {
    if (b->text() == "10 min") ten = b;
    if (b->text() == "Start") start = b;
  }
  ASSERT_TRUE(ten && start);
  ten->click();
  EXPECT_EQ(spins[0]->value(), 10);
  EXPECT_TRUE(ten->isChecked());
  spins[1]->setValue(30);
  EXPECT_FALSE(ten->isChecked());  // typing a custom value clears the quick pick
  start->click();
  EXPECT_TRUE(state.timerActive());
  EXPECT_EQ(state.timerSecondsRemaining(), 630);
}

TEST_F(ClockTest, AlarmDialogSavesToTheSharedFileAndEditsInPlace) {
  FakeClock clock;
  ClockSharedState& state = makeState();
  ClockWidget widget(contextFor(clockOptions(), eventBus_, globalState_, pollingWorkers_), &state,
                     std::ref(clock));
  widget.show();
  widget.runCallback("toggle_alarm");
  ASSERT_NE(widget.alarmPopup(), nullptr);
  QPushButton* save = nullptr;
  for (auto* b : widget.alarmPopup()->findChildren<QPushButton*>()) {
    if (b->text() == "Save") save = b;
  }
  auto* title = widget.alarmPopup()->findChild<QLineEdit*>();
  ASSERT_TRUE(save && title);
  EXPECT_FALSE(save->isEnabled());  // a title is required
  title->setText("Stand up");
  EXPECT_TRUE(save->isEnabled());
  auto spins = widget.alarmPopup()->findChildren<QSpinBox*>();
  ASSERT_EQ(spins.size(), 2);
  spins[0]->setValue(7);
  spins[1]->setValue(5);
  save->click();
  ASSERT_EQ(state.alarms().size(), 1u);
  EXPECT_EQ(alarmTime(state.alarms()[0]), "07:05");
  EXPECT_EQ(alarmTitle(state.alarms()[0]), "Stand up");
  EXPECT_EQ(alarmDays(state.alarms()[0]), (std::vector<int>{2}));  // today is a Wednesday
  EXPECT_TRUE(alarmEnabled(state.alarms()[0]));
  EXPECT_EQ(readJson(alarmsFile_.string())[0]["title"], "Stand up");

  const Alarm saved = state.alarms()[0];
  widget.showAlarmDialog(&saved);
  QPushButton* toggle = nullptr;
  for (auto* b : widget.alarmPopup()->findChildren<QPushButton*>()) {
    if (b->text() == "Enabled") toggle = b;
  }
  ASSERT_NE(toggle, nullptr);
  toggle->click();
  EXPECT_EQ(toggle->text(), "Disabled");
  EXPECT_EQ(toggle->property("class").toString(), "button is-alarm-disabled");
  for (auto* b : widget.alarmPopup()->findChildren<QPushButton*>()) {
    if (b->text() == "Save") b->click();
  }
  ASSERT_EQ(state.alarms().size(), 1u);
  EXPECT_FALSE(alarmEnabled(state.alarms()[0]));
  EXPECT_FALSE(state.hasEnabledAlarms());
}

TEST_F(ClockTest, ContextMenuAndActiveAlarmBuildWithoutCrashing) {
  FakeClock clock;
  ClockSharedState& state = makeState();
  json options = clockOptions();
  options["timezones"] = {"UTC", "Asia/Kolkata"};
  ClockWidget widget(contextFor(options, eventBus_, globalState_, pollingWorkers_), &state,
                     std::ref(clock));
  widget.show();
  state.addAlarm({{"time", "07:00"}, {"days", json::array()}, {"title", "Up"}});
  widget.runCallback("context_menu");
  widget.alarmFired(state.alarms().front());
  ASSERT_NE(widget.activeAlarmPopup(), nullptr);
  QPushButton* snooze = nullptr;
  for (auto* b : widget.activeAlarmPopup()->findChildren<QPushButton*>()) {
    if (b->text() == "Snooze 3 min") snooze = b;
  }
  ASSERT_NE(snooze, nullptr);
  snooze->click();
  EXPECT_EQ(state.snoozed().size(), 1u);
  EXPECT_EQ(widget.labels().size(), 1u);
}

// ---- vectors -------------------------------------------------------------------------------

TEST_F(ClockTest, ReplaysPythonLabelVectors) {
  const json cases = readJson(std::string(YASB_VECTORS_DIR) + "/labels/clock.json");
  ASSERT_TRUE(cases.is_array());
  ASSERT_GT(cases.size(), 0u);
  for (const json& c : cases) {
    SCOPED_TRACE(c.at("scenario").get<std::string>());
    ASSERT_FALSE(c.contains("construct_error"));
    ClockSharedState& state = makeState();
    FakeClock clock;
    const json& options = c.at("options");
    clock.local = options.at("timezones").empty() ||
                  !std::any_of(options.at("timezones").begin(), options.at("timezones").end(),
                               [](const json& tz) {
                                 return QTimeZone::isTimeZoneIdAvailable(
                                     QByteArray::fromStdString(tz.get<std::string>()));
                               });
    setWall(clock, "2026-10-28T23:59:05.123456");
    ClockWidget widget(contextFor(options, eventBus_, globalState_, pollingWorkers_), &state,
                       std::ref(clock));
    widget.show();
    EXPECT_EQ(snapshot(widget), c.at("initial")) << snapshot(widget).dump(1);
    size_t index = 0;
    for (const json& s : c.at("steps")) {
      const json& step = s.at("step");
      SCOPED_TRACE("step " + std::to_string(index++) + " " + step.dump());
      if (step.contains("time")) setWall(clock, step.at("time").get<std::string>());
      if (step.value("toggle", false)) {
        widget.runCallback("toggle_label");
      } else if (step.value("next_timezone", false)) {
        widget.runCallback("next_timezone");
      } else {
        if (step.contains("alarms")) {
          std::vector<Alarm> alarms;
          for (const json& a : step.at("alarms")) alarms.push_back(Alarm::parse(a.dump()));
          state.replaceAlarms(std::move(alarms));
        }
        if (step.contains("snoozed")) {
          state.clearSnoozed();
          for (const json& a : step.at("snoozed")) state.snooze(Alarm::parse(a.dump()), 600);
        }
        if (step.contains("timer")) {
          if (step.at("timer").is_null()) {
            state.cancelTimer();
          } else {
            state.startTimer(step.at("timer").get<int>());
          }
        }
        widget.runCallback("update_label");
      }
      EXPECT_EQ(snapshot(widget), s.at("snapshot")) << snapshot(widget).dump(1);
    }
  }
}
