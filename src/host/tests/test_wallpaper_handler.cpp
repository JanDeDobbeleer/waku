// `wabarc wallpaper ...` through the bar's control handler: the handler that parses the command
// line (on a fake WallpaperControl) and the wabarc client.
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>

#include "control/control.h"
#include "control/wallpaper_control.h"
#include "host/qml/control_handler.h"
#include "host/testing/bar_fixtures.h"

namespace {

// ---- fakes ----

class FakeControl final : public WallpaperControl {
 public:
  QString controlSet(const QString& target, bool dryRun) override {
    calls << QStringLiteral("set|%1|%2").arg(target).arg(dryRun);
    return answer;
  }
  QString controlRandom(bool dryRun) override {
    calls << QStringLiteral("random|%1").arg(dryRun);
    return answer;
  }
  QString controlNext(bool dryRun) override {
    calls << QStringLiteral("next|%1").arg(dryRun);
    return answer;
  }
  QString controlPrevious(bool dryRun) override {
    calls << QStringLiteral("previous|%1").arg(dryRun);
    return answer;
  }
  QString controlGallery() override {
    calls << QStringLiteral("gallery");
    return answer;
  }
  QStringList calls;
  QString answer = QStringLiteral("ok: fake");
};

QString uniqueName() {
  return QStringLiteral("wabar-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

QString wabarcPath() {
  return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("wabarc.exe"));
}

}  // namespace

// ---- the handler on a fake service ----

namespace {

struct HandlerFixture {
  HandlerFixture() {
    h.start(barConfig());
    EXPECT_TRUE(h.host->reload());
  }
  ControlHandler handler(bool withService = true) {
    return makeControlHandler(*h.host, false, {}, [this, withService]() -> WallpaperControl* {
      return withService ? &fake : nullptr;
    });
  }
  Harness h;
  FakeControl fake;
};

}  // namespace

TEST(WallpaperHandler, SetPassesTheTargetAndTheDryRunFlagThroughQuotes) {
  HandlerFixture f;
  const ControlHandler handler = f.handler();
  EXPECT_EQ(handler(QStringLiteral("wallpaper set sunset")), QStringLiteral("ok: fake"));
  EXPECT_EQ(handler(QStringLiteral("wallpaper set \"C:\\My Pictures\\a b.png\"")),
            QStringLiteral("ok: fake"));
  EXPECT_EQ(handler(QStringLiteral("wallpaper set --dry-run \"a \"\"q\"\" b.png\"")),
            QStringLiteral("ok: fake"));
  EXPECT_EQ(handler(QStringLiteral("wallpaper set x.png --dry-run")), QStringLiteral("ok: fake"));
  EXPECT_EQ(
      f.fake.calls,
      (QStringList{QStringLiteral("set|sunset|0"), QStringLiteral("set|C:\\My Pictures\\a b.png|0"),
                   QStringLiteral("set|a \"q\" b.png|1"), QStringLiteral("set|x.png|1")}));
}

TEST(WallpaperHandler, TheOtherVerbsCallTheirFunction) {
  HandlerFixture f;
  const ControlHandler handler = f.handler();
  handler(QStringLiteral("wallpaper random"));
  handler(QStringLiteral("wallpaper next --dry-run"));
  handler(QStringLiteral("wallpaper previous"));
  handler(QStringLiteral("wallpaper gallery"));
  EXPECT_EQ(f.fake.calls, (QStringList{QStringLiteral("random|0"), QStringLiteral("next|1"),
                                       QStringLiteral("previous|0"), QStringLiteral("gallery")}));
}

TEST(WallpaperHandler, TheAnswerOfTheServiceIsPassedOn) {
  HandlerFixture f;
  f.fake.answer = QStringLiteral("error: no such file");
  EXPECT_EQ(f.handler()(QStringLiteral("wallpaper set x")), QStringLiteral("error: no such file"));
}

TEST(WallpaperHandler, BadUsageIsAnErrorAndCallsNothing) {
  HandlerFixture f;
  const ControlHandler handler = f.handler();
  for (const char* line :
       {"wallpaper", "wallpaper set", "wallpaper set a b", "wallpaper random extra",
        "wallpaper gallery --dry-run", "wallpaper gallery now", "wallpaper bogus",
        "wallpaper set \"unterminated"}) {
    const QString answer = handler(QString::fromLatin1(line));
    EXPECT_TRUE(answer.startsWith(QStringLiteral("error: ")))
        << line << " -> " << qPrintable(answer);
  }
  EXPECT_TRUE(f.fake.calls.isEmpty());
}

TEST(WallpaperHandler, WithoutAServiceTheAnswerIsAnError) {
  HandlerFixture f;
  EXPECT_TRUE(f.handler(false)(QStringLiteral("wallpaper next"))
                  .startsWith(QStringLiteral("error: the wallpaper service is not available")));
  const ControlHandler none = makeControlHandler(*f.h.host, false);
  EXPECT_TRUE(none(QStringLiteral("wallpaper next")).startsWith(QStringLiteral("error: ")));
  // The old commands are untouched.
  EXPECT_EQ(none(QStringLiteral("status")).left(7), QStringLiteral("running"));
}

TEST(WallpaperHandler, ThePipeCarriesTheQuotedLineFromWabarc) {
  if (!QFile::exists(wabarcPath())) GTEST_SKIP() << "wabarc.exe is not built";
  HandlerFixture f;
  QTemporaryDir home;
  ASSERT_TRUE(home.isValid());
  ControlServer server(controlPipeName(home.path()), f.handler());
  ASSERT_TRUE(server.listen());

  auto run = [&](const QStringList& args, int* exitCode, QString* out, QString* err) {
    QProcess process;
    process.start(wabarcPath(), QStringList{QStringLiteral("--config-home"), home.path()} + args);
    ASSERT_TRUE(process.waitForStarted(5000));
    ASSERT_TRUE(QTest::qWaitFor([&] { return process.state() == QProcess::NotRunning; }, 10000));
    *exitCode = process.exitCode();
    *out = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    *err = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
  };
  int code = -1;
  QString out;
  QString err;
  run({QStringLiteral("wallpaper"), QStringLiteral("set"), QStringLiteral("--dry-run"),
       QStringLiteral("C:\\My Pictures\\x \"y\".png")},
      &code, &out, &err);
  EXPECT_EQ(code, 0) << qPrintable(err);
  EXPECT_EQ(out, QStringLiteral("ok: fake"));
  ASSERT_EQ(f.fake.calls.size(), 1);
  EXPECT_EQ(f.fake.calls.first(), QStringLiteral("set|C:\\My Pictures\\x \"y\".png|1"));

  f.fake.answer = QStringLiteral("error: nope");
  run({QStringLiteral("wallpaper"), QStringLiteral("next")}, &code, &out, &err);
  EXPECT_EQ(code, 1);
  EXPECT_EQ(err, QStringLiteral("error: nope"));
}

// ---- the wabarc client without a bar ----

TEST(WallpaperCli, NoBarRunningExitsWithTwoAndBadUsageWithThree) {
  if (!QFile::exists(wabarcPath())) GTEST_SKIP() << "wabarc.exe is not built";
  QTemporaryDir home;
  ASSERT_TRUE(home.isValid());
  auto exitOf = [&](const QStringList& args) {
    QProcess process;
    process.start(wabarcPath(), QStringList{QStringLiteral("--config-home"), home.path()} + args);
    EXPECT_TRUE(process.waitForFinished(10000));
    return process.exitCode();
  };
  EXPECT_EQ(exitOf({QStringLiteral("wallpaper"), QStringLiteral("next")}), 2);
  EXPECT_EQ(exitOf({QStringLiteral("wallpaper"), QStringLiteral("set"), QStringLiteral("a.png")}),
            2);
  EXPECT_EQ(exitOf({QStringLiteral("wallpaper")}), 3);
  EXPECT_EQ(exitOf({QStringLiteral("wallpaper"), QStringLiteral("set")}), 3);
  EXPECT_EQ(exitOf({QStringLiteral("wallpaper"), QStringLiteral("bogus")}), 3);
  EXPECT_EQ(
      exitOf({QStringLiteral("wallpaper"), QStringLiteral("gallery"), QStringLiteral("--dry-run")}),
      3);
  EXPECT_EQ(exitOf({QStringLiteral("wallpaper"), QStringLiteral("next"), QStringLiteral("x")}), 3);
  EXPECT_EQ(exitOf({QStringLiteral("status"), QStringLiteral("--dry-run")}), 3);
  EXPECT_EQ(exitOf({QStringLiteral("status")}), 2);
}
