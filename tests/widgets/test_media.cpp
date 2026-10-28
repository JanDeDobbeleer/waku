#include <gtest/gtest.h>

#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFrame>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>

#include "core/config/loader.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"
#include "core/widgets/scrolling_label.h"
#include "platform/app_volume.h"
#include "platform/media_session.h"
#include "widgets/media/lanczos.h"
#include "widgets/media/media_widget.h"

using nlohmann::json;
using namespace yasb::widgets;
using namespace yasb::platform;

namespace {

// --- fakes
// ----------------------------------------------------------------------------------------

struct Command {
  QString appId;
  MediaCommand command;
  double value;
};

class FakeSource final : public MediaSessionSource {
 public:
  void start(MediaSessionSink* sink) override { sink_ = sink; }
  void stop() override { sink_ = nullptr; }
  void command(const QString& appId, MediaCommand command, double value) override {
    commands.push_back({appId, command, value});
  }
  MediaSessionSink* sink_ = nullptr;
  std::vector<Command> commands;
};

QByteArray pngBytes(int width, int height, QColor color) {
  QImage image(width, height, QImage::Format_RGB32);
  image.fill(color);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return bytes;
}

// Vector sessions: see tools/parity/vec_media.py (BASE_SESSION).
struct Spec {
  QString appId = QStringLiteral("Fake.Player.App");
  QString title = QStringLiteral("Song");
  QString artist = QStringLiteral("Artist");
  bool playing = true;
  bool prevEnabled = true;
  bool playEnabled = true;
  bool nextEnabled = true;
  bool timelineEnabled = false;
  double duration = 0;
  double position = 0;
  bool thumbnail = false;
};

Spec specFrom(const json& j) {
  Spec s;
  auto str = [&](const char* key, QString& out) {
    if (j.contains(key)) out = QString::fromStdString(j[key].get<std::string>());
  };
  auto flag = [&](const char* key, bool& out) {
    if (j.contains(key)) out = j[key].get<bool>();
  };
  auto num = [&](const char* key, double& out) {
    if (j.contains(key)) out = j[key].get<double>();
  };
  str("app_id", s.appId);
  str("title", s.title);
  str("artist", s.artist);
  flag("playing", s.playing);
  flag("prev_enabled", s.prevEnabled);
  flag("play_enabled", s.playEnabled);
  flag("next_enabled", s.nextEnabled);
  flag("timeline_enabled", s.timelineEnabled);
  num("duration", s.duration);
  num("position", s.position);
  flag("thumbnail", s.thumbnail);
  return s;
}

MediaSessionData dataFrom(const Spec& spec, double now) {
  MediaSessionData data;
  data.appId = spec.appId;
  data.sourceName = spec.appId == QLatin1String("Fake.Player.App") ? QStringLiteral("Fake Player")
                    : spec.appId == QLatin1String("Other.Player.App")
                        ? QStringLiteral("Other Player")
                        : QString();
  MediaProperties properties;
  properties.title = spec.title;
  properties.artist = spec.artist;
  if (spec.thumbnail) properties.thumbnail = pngBytes(40, 20, QColor(200, 80, 40));
  data.properties = properties;
  data.timeline = MediaTimeline{spec.position, now, spec.duration};
  MediaPlayback playback;
  playback.status = spec.playing ? 4 : 5;
  playback.prevEnabled = spec.prevEnabled;
  playback.playEnabled = spec.playEnabled;
  playback.nextEnabled = spec.nextEnabled;
  playback.timelineEnabled = spec.timelineEnabled;
  data.playback = playback;
  return data;
}

// Config -> typed options through the real loader so schema defaults apply.
yasb::config::options::MediaWidgetConfig mediaOptions(const json& options) {
  const json config = {
      {"bars", {{"main", {{"widgets", {{"left", {"media"}}}}}}}},
      {"widgets", {{"media", {{"type", "yasb.media.MediaWidget"}, {"options", options}}}}}};
  const auto loaded = yasb::config::loadConfigText(config.dump());
  EXPECT_TRUE(loaded.ok()) << loaded.message;
  auto typed = loaded.widgetOptions<yasb::config::options::MediaWidgetConfig>("media");
  EXPECT_TRUE(typed.has_value());
  return typed.value_or(yasb::config::options::MediaWidgetConfig{});
}

// Per-app audio session fake: state lives in a struct shared with the test.
struct FakeVolumeState {
  float volume = 0.4f;
  bool muted = false;
  bool fail = false;
  QString boundAumid;
  bool hasSession = true;
};

class FakeVolumeSession final : public AppVolumeSession {
 public:
  explicit FakeVolumeSession(std::shared_ptr<FakeVolumeState> state) : state_(std::move(state)) {}
  bool volume(float* out) override {
    if (state_->fail) return false;
    *out = state_->volume;
    return true;
  }
  bool setVolume(float level) override {
    if (state_->fail) return false;
    state_->volume = level;
    return true;
  }
  bool muted(bool* out) override {
    if (state_->fail) return false;
    *out = state_->muted;
    return true;
  }
  bool setMuted(bool muted) override {
    if (state_->fail) return false;
    state_->muted = muted;
    return true;
  }

 private:
  std::shared_ptr<FakeVolumeState> state_;
};

class FakeVolumeSource final : public AppVolumeSource {
 public:
  explicit FakeVolumeSource(std::shared_ptr<FakeVolumeState> state) : state_(std::move(state)) {}
  std::unique_ptr<AppVolumeSession> bind(const QString& aumid) override {
    state_->boundAumid = aumid;
    if (!state_->hasSession) return nullptr;
    return std::make_unique<FakeVolumeSession>(state_);
  }

 private:
  std::shared_ptr<FakeVolumeState> state_;
};

class MediaTest : public testing::Test {
 protected:
  void SetUp() override {
    auto source = std::make_unique<FakeSource>();
    source_ = source.get();
    service_ =
        std::make_unique<MediaSessionService>(std::move(source), nullptr, [this] { return now_; });
    service_->start();
  }

  void TearDown() override {
    widget_.reset();
    service_.reset();
  }

  MediaWidget& build(const json& options, std::shared_ptr<AppVolumeSource> volumeSource = nullptr) {
    TypedWidgetOptions typed = mediaOptions(options);
    WidgetContext context{
        "media",      "yasb.media.MediaWidget", options,         {}, {}, eventBus_,
        globalState_, pollingWorkers_,          std::move(typed)};
    widget_.reset(new MediaWidget(context, service_.get(), std::move(volumeSource)));
    return *widget_;
  }

  void show(const Spec& spec) {
    now_ += 1;
    service_->applySessions({dataFrom(spec, now_)}, spec.appId, true);
  }

  void clear() {
    now_ += 1;
    service_->applySessions({}, QString(), true);
  }

  double now_ = 1000;
  FakeSource* source_ = nullptr;
  std::unique_ptr<MediaSessionService> service_;
  std::unique_ptr<MediaWidget> widget_;
  EventBus eventBus_;
  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
};

// --- vector replay
// --------------------------------------------------------------------------------

json labelJson(const QLabel* label) {
  if (!label) return nullptr;
  return {{"text", label->text().toStdString()},
          {"class", label->property("class").toString().toStdString()},
          {"hidden", label->isHidden()}};
}

QLabel* findLabel(QWidget* root, const QString& classPrefix) {
  for (QLabel* label : root->findChildren<QLabel*>()) {
    if (label->property("class").toString().startsWith(classPrefix)) return label;
  }
  return nullptr;
}

json menuJson(MediaWidget& widget) {
  QWidget* dialog = widget.menu();
  if (!dialog || !dialog->isVisible()) return nullptr;
  json menu = {{"class", dialog->property("class").toString().toStdString()}};
  auto noMedia = findLabel(dialog, QStringLiteral("no-media"));
  menu["no_media"] = noMedia ? json(noMedia->text().toStdString()) : json(nullptr);
  menu["title"] = labelJson(findLabel(dialog, QStringLiteral("title")));
  menu["artist"] = labelJson(findLabel(dialog, QStringLiteral("artist")));
  menu["source"] = labelJson(findLabel(dialog, QStringLiteral("source")));
  menu["current_time"] = labelJson(findLabel(dialog, QStringLiteral("playback-time current")));
  menu["total_time"] = labelJson(findLabel(dialog, QStringLiteral("playback-time total")));
  menu["prev"] = labelJson(findLabel(dialog, QStringLiteral("btn prev")));
  menu["play"] = labelJson(findLabel(dialog, QStringLiteral("btn play")));
  menu["next"] = labelJson(findLabel(dialog, QStringLiteral("btn next")));
  QFrame* timeline = nullptr;
  for (QFrame* frame : dialog->findChildren<QFrame*>()) {
    if (frame->property("class").toString() == QLatin1String("media-timeline-container"))
      timeline = frame;
  }
  menu["timeline_hidden"] = timeline ? json(timeline->isHidden()) : json(nullptr);
  auto* slider = dialog->findChild<QSlider*>();
  menu["slider_value"] = slider ? json(slider->value()) : json(nullptr);
  QLabel* thumbnail = findLabel(dialog, QStringLiteral("thumbnail"));
  menu["thumbnail_has_pixmap"] = thumbnail ? json(!thumbnail->pixmap().isNull()) : json(nullptr);
  return menu;
}

json snapshot(MediaWidget& widget) {
  json order = json::array();
  QLayout* layout = widget.labelContainer()->layout();
  for (int i = 0; i < layout->count(); ++i) {
    QLayoutItem* item = layout->itemAt(i);
    QWidget* child = item->widget();
    if (!child) {
      order.push_back("stack");
    } else if (child == widget.prevButton()) {
      order.push_back("prev");
    } else if (child == widget.playButton()) {
      order.push_back("play");
    } else if (child == widget.nextButton()) {
      order.push_back("next");
    } else {
      order.push_back("other");
    }
  }
  return {{"widget_hidden", widget.isHidden()},
          {"frame_class", widget.widgetFrame()->property("class").toString().toStdString()},
          {"container_class", widget.labelContainer()->property("class").toString().toStdString()},
          {"label", labelJson(widget.label())},
          {"label_alt", labelJson(widget.labelAlt())},
          {"thumbnail_hidden", widget.thumbnailLabel()->isHidden()},
          {"thumbnail_has_pixmap", !widget.thumbnailLabel()->pixmap().isNull()},
          {"progress_hidden", widget.progressBar()->isHidden()},
          {"progress_value", widget.progressBar()->value()},
          {"progress_container_hidden", widget.progressBar()->parentWidget()->isHidden()},
          {"prev", labelJson(widget.prevButton())},
          {"play", labelJson(widget.playButton())},
          {"next", labelJson(widget.nextButton())},
          {"layout_order", order},
          {"menu", menuJson(widget)}};
}

}  // namespace

TEST_F(MediaTest, ReplaysPythonLabelVectors) {
  std::ifstream in(std::filesystem::path(YASB_VECTORS_DIR) / "labels" / "media.json",
                   std::ios::binary);
  const json vectors = json::parse(in);
  ASSERT_FALSE(vectors.empty());
  for (const json& scenario : vectors) {
    const std::string name = scenario["scenario"];
    SCOPED_TRACE(name);
    TearDown();  // fresh service and clock per scenario
    SetUp();
    now_ = 1000;
    MediaWidget& widget = build(scenario["options"]);
    widget.show();
    EXPECT_EQ(snapshot(widget), scenario["initial"]) << "initial";
    int index = 0;
    for (const json& step : scenario["steps"]) {
      SCOPED_TRACE("step " + std::to_string(index++) + " " + step["step"].dump());
      const json& what = step["step"];
      if (what.contains("session")) {
        if (what["session"].is_null()) {
          clear();
        } else {
          show(specFrom(what["session"]));
        }
      } else if (what.contains("toggle")) {
        widget.runCallback("toggle_label");
      } else if (what.contains("menu")) {
        widget.showMenu();
      } else if (what.contains("close_menu")) {
        if (widget.menu()) widget.menu()->hide();
      }
      EXPECT_EQ(snapshot(widget), step["snapshot"]);
    }
  }
}

// --- widget behaviour
// -----------------------------------------------------------------------------

TEST_F(MediaTest, NoSessionWithHideEmptyHidesTheWidget) {
  MediaWidget& widget = build({{"hide_empty", true}});
  EXPECT_TRUE(widget.isHidden());
  show(Spec{});
  EXPECT_FALSE(widget.isHidden());
  clear();
  EXPECT_TRUE(widget.isHidden());
  EXPECT_EQ(widget.playButton()->property("class").toString(), "btn play disabled");
}

TEST_F(MediaTest, SwitchingSessionsUpdatesTitleAndThumbnail) {
  MediaWidget& widget = build({{"label", "{title}"}, {"show_thumbnail", true}});
  widget.show();
  Spec first;
  first.title = "First";
  first.thumbnail = true;
  Spec second;
  second.appId = "Other.Player.App";
  second.title = "Second";
  now_ += 1;
  service_->applySessions({dataFrom(first, now_), dataFrom(second, now_)}, first.appId, true);
  EXPECT_EQ(widget.label()->text(), "First");
  EXPECT_FALSE(widget.thumbnailLabel()->isHidden());
  EXPECT_FALSE(widget.thumbnailLabel()->pixmap().isNull());

  service_->switchCurrentSession(+1);
  EXPECT_EQ(widget.label()->text(), "Second");
  EXPECT_TRUE(widget.thumbnailLabel()->isHidden());
}

TEST_F(MediaTest, ConfiguredCallbacksControlPlaybackAndLabel) {
  MediaWidget& widget = build({{"label", "{title}"},
                               {"label_alt", "{artist}"},
                               {"callbacks",
                                {{"on_left", "toggle_media_menu"},
                                 {"on_middle", "toggle_play_pause"},
                                 {"on_right", "toggle_label"}}}});
  widget.show();
  show(Spec{});
  EXPECT_TRUE(widget.runCallback("toggle_play_pause"));
  ASSERT_EQ(source_->commands.size(), 1u);
  EXPECT_EQ(source_->commands[0].command, MediaCommand::PlayPause);
  EXPECT_EQ(source_->commands[0].appId, "Fake.Player.App");

  EXPECT_TRUE(widget.runCallback("toggle_label"));
  EXPECT_TRUE(widget.label()->isHidden());
  EXPECT_FALSE(widget.labelAlt()->isHidden());
  EXPECT_EQ(widget.labelAlt()->text(), "Artist");
  EXPECT_TRUE(widget.runCallback("toggle_label"));
  EXPECT_FALSE(widget.label()->isHidden());
}

TEST_F(MediaTest, ButtonsSendCommandsOnlyWhenEnabled) {
  MediaWidget& widget = build(json::object());
  widget.show();
  Spec spec;
  spec.nextEnabled = false;
  show(spec);
  EXPECT_EQ(widget.nextButton()->property("class").toString(), "btn next disabled");
  EXPECT_EQ(widget.prevButton()->property("class").toString(), "btn prev ");

  for (auto* button : {widget.prevButton(), widget.nextButton(), widget.playButton()}) {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(1, 1), QPointF(1, 1), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(button, &press);
    QApplication::sendEvent(button, &release);
  }
  ASSERT_EQ(source_->commands.size(), 2u);
  EXPECT_EQ(source_->commands[0].command, MediaCommand::Previous);
  EXPECT_EQ(source_->commands[1].command, MediaCommand::PlayPause);
}

TEST_F(MediaTest, PlayButtonShowsPauseIconWhilePlaying) {
  MediaWidget& widget = build({{"icons", {{"play", "P"}, {"pause", "S"}}}});
  Spec spec;
  show(spec);
  EXPECT_EQ(widget.playButton()->text(), "S");
  spec.playing = false;
  show(spec);
  EXPECT_EQ(widget.playButton()->text(), "P");
}

// --- service
// ----------------------------------------------------------------------------------------

TEST_F(MediaTest, PositionInterpolatesOnlyWhilePlaying) {
  Spec spec;
  spec.duration = 200;
  spec.position = 10;
  spec.timelineEnabled = true;
  show(spec);
  EXPECT_TRUE(service_->isInterpolating());
  now_ += 5;
  service_->forceUpdate();
  EXPECT_DOUBLE_EQ(service_->currentSession()->currentPos, 15);

  spec.playing = false;
  show(spec);
  EXPECT_FALSE(service_->isInterpolating());
  const double paused = service_->currentSession()->currentPos;
  now_ += 30;
  service_->forceUpdate();
  EXPECT_DOUBLE_EQ(service_->currentSession()->currentPos, paused);

  clear();
  EXPECT_FALSE(service_->isInterpolating());
  EXPECT_EQ(service_->currentSession(), nullptr);
}

TEST_F(MediaTest, UnsetTimelineTimestampIsIgnoredAndRemovedCurrentFallsBack) {
  MediaSessionData unset = dataFrom(Spec{}, 0);
  unset.timeline->lastUpdated = 0;
  service_->applySessions({unset}, unset.appId, true);
  EXPECT_FALSE(service_->isInterpolating());
  EXPECT_DOUBLE_EQ(service_->currentSession()->duration, 0);

  Spec other;
  other.appId = "Other.Player.App";
  now_ += 1;
  service_->applySessions({dataFrom(Spec{}, now_), dataFrom(other, now_)}, "Other.Player.App",
                          true);
  EXPECT_EQ(service_->currentSession()->appId, "Other.Player.App");
  // The selected session ends while the system "current" is unknown: fall back to the first.
  service_->applySessions({dataFrom(Spec{}, now_)}, QString(), false);
  EXPECT_EQ(service_->currentSession()->appId, "Fake.Player.App");
}

TEST_F(MediaTest, SeekAndToggleCommandsReachTheSource) {
  Spec spec;
  spec.duration = 100;
  show(spec);
  service_->seek(42);
  service_->next();
  service_->previous();
  ASSERT_EQ(source_->commands.size(), 3u);
  EXPECT_EQ(source_->commands[0].command, MediaCommand::Seek);
  EXPECT_DOUBLE_EQ(source_->commands[0].value, 42);
  EXPECT_DOUBLE_EQ(service_->currentSession()->lastSnapshotPos, 42);
  // Shuffle and repeat need the control to be enabled by the session.
  service_->toggleShuffle();
  service_->cycleRepeat();
  EXPECT_EQ(source_->commands.size(), 3u);
}

// --- helpers -------------------------------------------------------------------------------------

TEST(MediaHelpers, CleanLabelDropsEmptyPlaceholdersAndDanglingSeparators) {
  const QMap<QString, QString> both = {{"s", " - "}, {"title", "T"}, {"artist", "A"}};
  EXPECT_EQ(cleanMediaLabel("{title}{s}{artist}", both), "{title}{s}{artist}");
  const QMap<QString, QString> noArtist = {{"s", " - "}, {"title", "T"}, {"artist", ""}};
  EXPECT_EQ(cleanMediaLabel("{title}{s}{artist}", noArtist), "{title}");
  EXPECT_EQ(cleanMediaLabel("{s}{title}{s}", noArtist), "{title}");
  EXPECT_EQ(cleanMediaLabel("x {unknown} y", both), "x  y");
}

TEST(MediaHelpers, TruncationCountsCodePointsAndFollowsPythonSlicing) {
  EXPECT_EQ(truncateMediaField("abcdef", 6), "abcdef");
  EXPECT_EQ(truncateMediaField("abcdefg", 6), "abc...");
  // Two emoji are two code points (four UTF-16 units).
  EXPECT_EQ(truncateMediaField(QString::fromUtf8("😀😀abc"), 5), QString::fromUtf8("😀😀abc"));
  EXPECT_EQ(truncateMediaField(QString::fromUtf8("😀😀abcde"), 6), QString::fromUtf8("😀😀a..."));
  // text[: max - 3] with max < 3 slices from the end.
  EXPECT_EQ(truncateMediaField("abcdef", 2), "abcde...");
}

TEST(MediaHelpers, FormatsTime) {
  EXPECT_EQ(formatMediaTime(0), "0:00");
  EXPECT_EQ(formatMediaTime(61.9), "1:01");
  EXPECT_EQ(formatMediaTime(3725), "1:02:05");
}

TEST(MediaHelpers, BarThumbnailAppliesAlphaPaddingAndCorners) {
  QImage source(40, 20, QImage::Format_RGB32);
  source.fill(Qt::red);
  ThumbnailStyle style;
  style.labelWidth = 92;
  style.paddingAdded = 8;
  style.containerHeight = 30;
  style.alpha = 80;
  style.cornerRadius = 14;
  style.symmetric = true;
  QImage image = renderBarThumbnail(source, style);
  ASSERT_FALSE(image.isNull());
  EXPECT_EQ(image.width(), 100);
  EXPECT_EQ(image.height(), 30);  // 50 tall scaled, cropped to the bar height
  EXPECT_EQ(qAlpha(image.pixel(50, 15)), 80);
  EXPECT_EQ(qAlpha(image.pixel(0, 0)), 0);
  EXPECT_EQ(qAlpha(image.pixel(99, 29)), 0);

  style.symmetric = false;
  style.controlsLeft = true;  // only the right-hand corners are rounded
  image = renderBarThumbnail(source, style);
  EXPECT_EQ(qAlpha(image.pixel(0, 0)), 80);
  EXPECT_EQ(qAlpha(image.pixel(99, 0)), 0);

  style.cornerRadius = 0;
  style.edgeFade = true;
  image = renderBarThumbnail(source, style);
  EXPECT_EQ(qAlpha(image.pixel(0, 15)), 0);
  EXPECT_EQ(qAlpha(image.pixel(50, 15)), 80);
  EXPECT_LT(qAlpha(image.pixel(5, 15)), 80);

  style.labelWidth = -8;
  EXPECT_TRUE(renderBarThumbnail(source, style).isNull());
}

TEST(MediaHelpers, PopupThumbnailIsASquareCrop) {
  QImage wide(40, 20, QImage::Format_RGB32);
  wide.fill(Qt::blue);
  const QImage square = renderPopupThumbnail(wide, 50);
  EXPECT_EQ(square.size(), QSize(50, 50));
}

TEST(MediaHelpers, LanczosResizeKeepsFlatColourAndAlpha) {
  QImage flat(37, 23, QImage::Format_RGBA8888);
  flat.fill(QColor(200, 100, 50, 128));
  for (const QSize target : {QSize(10, 6), QSize(80, 50)}) {
    const QImage out = lanczosResize(flat, target.width(), target.height());
    ASSERT_EQ(out.size(), target);
    for (int y = 0; y < out.height(); y += 3) {
      for (int x = 0; x < out.width(); x += 3) {
        const QColor c = out.pixelColor(x, y);
        EXPECT_NEAR(c.red(), 200, 1);
        EXPECT_NEAR(c.green(), 100, 1);
        EXPECT_NEAR(c.blue(), 50, 1);
        EXPECT_NEAR(c.alpha(), 128, 1);
      }
    }
  }
  EXPECT_TRUE(lanczosResize(flat, 0, 5).isNull());
  EXPECT_TRUE(lanczosResize(QImage(), 5, 5).isNull());
}

TEST(MediaHelpers, LanczosDownscaleKeepsEdges) {
  // Left half black, right half white: a 2x downscale keeps the transition centred.
  QImage half(40, 4, QImage::Format_RGBA8888);
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 40; ++x) half.setPixelColor(x, y, x < 20 ? Qt::black : Qt::white);
  const QImage out = lanczosResize(half, 20, 2);
  EXPECT_LE(out.pixelColor(2, 0).red(), 5);
  EXPECT_GE(out.pixelColor(17, 0).red(), 250);
  EXPECT_LT(out.pixelColor(9, 0).red(), 128);
  EXPECT_GT(out.pixelColor(10, 0).red(), 128);
}

TEST_F(MediaTest, VolumeSliderIsAbsentUnlessConfigured) {
  auto state = std::make_shared<FakeVolumeState>();
  MediaWidget& widget = build(json::object(), std::make_shared<FakeVolumeSource>(state));
  show(Spec{});
  widget.showMenu();
  EXPECT_EQ(widget.appVolumeSlider(), nullptr);
  EXPECT_TRUE(state->boundAumid.isEmpty());
}

TEST_F(MediaTest, VolumeSliderBindsCurrentAppAndWritesVolume) {
  auto state = std::make_shared<FakeVolumeState>();
  MediaWidget& widget = build({{"media_menu", {{"show_volume_slider", true}}},
                               {"media_menu_icons", {{"mute", "M"}, {"unmute", "U"}}}},
                              std::make_shared<FakeVolumeSource>(state));
  show(Spec{});
  widget.showMenu();
  ASSERT_NE(widget.appVolumeSlider(), nullptr);
  EXPECT_EQ(state->boundAumid, QStringLiteral("Fake.Player.App"));
  EXPECT_EQ(widget.appVolumeSlider()->property("class").toString(),
            QStringLiteral("volume-slider"));
  EXPECT_EQ(widget.appVolumeSlider()->orientation(), Qt::Vertical);
  EXPECT_EQ(widget.appVolumeSlider()->value(), 40);
  EXPECT_TRUE(widget.appVolumeSlider()->isEnabled());
  EXPECT_EQ(widget.appVolumeContainer()->property("class").toString(),
            QStringLiteral("app-volume-container"));
  EXPECT_EQ(widget.appMuteButton()->text(), QStringLiteral("M"));
  EXPECT_EQ(widget.appMuteButton()->property("class").toString(), QStringLiteral("mute-button"));

  widget.appVolumeSlider()->setValue(75);
  EXPECT_FLOAT_EQ(state->volume, 0.75f);
}

TEST_F(MediaTest, MuteButtonTogglesAndRaisingVolumeDoesNotUnmuteTheSession) {
  auto state = std::make_shared<FakeVolumeState>();
  MediaWidget& widget = build({{"media_menu", {{"show_volume_slider", true}}},
                               {"media_menu_icons", {{"mute", "M"}, {"unmute", "U"}}}},
                              std::make_shared<FakeVolumeSource>(state));
  show(Spec{});
  widget.showMenu();
  QLabel* button = widget.appMuteButton();
  ASSERT_NE(button, nullptr);

  QMouseEvent press(QEvent::MouseButtonPress, QPointF(2, 2), QPointF(2, 2), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QMouseEvent release(QEvent::MouseButtonRelease, QPointF(2, 2), QPointF(2, 2), Qt::LeftButton,
                      Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(button, &press);
  QApplication::sendEvent(button, &release);
  EXPECT_TRUE(state->muted);
  EXPECT_EQ(button->text(), QStringLiteral("U"));
  EXPECT_EQ(button->property("class").toString(), QStringLiteral("unmute-button"));

  // Python only refreshes its flag and the button after a slider write; the session stays muted.
  widget.appVolumeSlider()->setValue(60);
  EXPECT_FLOAT_EQ(state->volume, 0.6f);
  EXPECT_TRUE(state->muted);
}

TEST_F(MediaTest, VolumeSliderHidesWithoutAMatchingSession) {
  auto state = std::make_shared<FakeVolumeState>();
  state->hasSession = false;
  MediaWidget& widget = build({{"media_menu", {{"show_volume_slider", true}}}},
                              std::make_shared<FakeVolumeSource>(state));
  show(Spec{});
  widget.showMenu();
  ASSERT_NE(widget.appVolumeSlider(), nullptr);
  EXPECT_TRUE(widget.appVolumeContainer()->isHidden());
  EXPECT_FALSE(widget.appVolumeSlider()->isEnabled());
  EXPECT_FALSE(widget.appMuteButton()->isEnabled());
}

TEST_F(MediaTest, VolumeReadFailureDisablesTheSlider) {
  auto state = std::make_shared<FakeVolumeState>();
  state->fail = true;
  MediaWidget& widget = build({{"media_menu", {{"show_volume_slider", true}}}},
                              std::make_shared<FakeVolumeSource>(state));
  show(Spec{});
  widget.showMenu();
  EXPECT_FALSE(widget.appVolumeSlider()->isEnabled());
  EXPECT_FALSE(widget.appMuteButton()->isEnabled());
}

TEST_F(MediaTest, ScrollingLabelReplacesTheLabelsWhenEnabled) {
  MediaWidget& plain = build(json::object());
  EXPECT_EQ(qobject_cast<ScrollingLabel*>(plain.label()), nullptr);
  MediaWidget& widget = build({{"scrolling_label", {{"enabled", true}, {"style", "bounce"}}}});
  auto* label = qobject_cast<ScrollingLabel*>(widget.label());
  ASSERT_NE(label, nullptr);
  ASSERT_NE(qobject_cast<ScrollingLabel*>(widget.labelAlt()), nullptr);
  Spec spec;
  spec.title = QStringLiteral("A very very very long track title that never fits the bar at all");
  show(spec);
  // A scrolling label shows the untruncated text.
  EXPECT_TRUE(label->displayText().contains(spec.title));
}

TEST_F(MediaTest, ScrollingLabelPassesItsOptionsThrough) {
  MediaWidget& widget = build(
      {{"scrolling_label",
        {{"enabled", true}, {"style", "right"}, {"always_scroll", true}, {"separator", " | "}}}});
  auto* label = qobject_cast<ScrollingLabel*>(widget.label());
  ASSERT_NE(label, nullptr);
  widget.show();
  show(Spec{});
  EXPECT_TRUE(label->scrollingNeeded());
  EXPECT_EQ(label->displayText(), QStringLiteral("Song | "));
}

TEST(MediaServiceRegistry, SharesOneServiceAndStopsItOnStopAll) {
  PollingWorkerRegistry registry;
  int created = 0;
  auto make = [&] {
    ++created;
    return new MediaSessionService(std::make_unique<FakeSource>());
  };
  auto* first = registry.service<MediaSessionService>("media", make);
  auto* second = registry.service<MediaSessionService>("media", make);
  EXPECT_EQ(first, second);
  EXPECT_EQ(created, 1);
  first->start();
  registry.stopAll();
}

TEST(MediaServiceRegistry, RegistersTheWidgetType) {
  WidgetRegistry registry;
  registerMediaWidgets(registry);
  EXPECT_TRUE(registry.contains("yasb.media.MediaWidget"));
}

// Read-only check of the WinRT source against the live system sessions (never sends commands).
// Opt in with YASB_LIVE_MEDIA_SMOKE=1.
TEST(MediaLive, ReadsSystemSessionsWithoutControllingThem) {
  if (qEnvironmentVariableIsEmpty("YASB_LIVE_MEDIA_SMOKE"))
    GTEST_SKIP() << "set YASB_LIVE_MEDIA_SMOKE=1";
  MediaSessionService service(createSystemMediaSource());
  bool changed = false;
  QObject::connect(&service, &MediaSessionService::currentSessionChanged, [&] { changed = true; });
  service.start();
  QElapsedTimer timer;
  timer.start();
  while (!changed && timer.elapsed() < 10000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  EXPECT_TRUE(changed);
  for (const auto& session : service.sessions()) {
    std::cout << "session " << session.appId.toStdString() << " | "
              << session.sourceName.toStdString() << " | " << session.title.toStdString() << " | "
              << session.artist.toStdString() << " | playing=" << session.isPlaying
              << " thumb=" << session.thumbnail.size() << " dur=" << session.duration << std::endl;
  }
  service.stop();
}
