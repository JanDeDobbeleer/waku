// RunLog: the --log-file diagnostics file. Every test logs into a temporary folder; none touches
// the user's real log or config.
#include <gtest/gtest.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QStringList>
#include <QTemporaryDir>
#include <vector>

#include "host/run_log.h"

namespace {

using waku::host::RunLog;
using waku::host::RunLogOptions;

std::vector<QJsonObject> readRecords(const QString& path) {
  std::vector<QJsonObject> records;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return records;
  for (const QByteArray& line : file.readAll().split('\n')) {
    if (line.isEmpty()) continue;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    EXPECT_EQ(error.error, QJsonParseError::NoError) << line.toStdString();
    records.push_back(doc.object());
  }
  return records;
}

QJsonObject findEvent(const std::vector<QJsonObject>& records, const QString& name) {
  for (const QJsonObject& r : records) {
    if (r.value(QStringLiteral("ev")).toString() == name) return r;
  }
  return {};
}

// A RunLog that is stopped at the end of the test even when an assertion fails.
struct Guard {
  ~Guard() { RunLog::stop(0); }
};

}  // namespace

TEST(RunLog, NotStartedIsInactiveAndEventsAreDropped) {
  EXPECT_FALSE(RunLog::active());
  RunLog::event(QStringLiteral("nothing"));
  RunLog::stop(0);
  EXPECT_FALSE(RunLog::active());
}

TEST(RunLog, WritesTheHeaderMessagesEventsAndTheStop) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  RunLogOptions options;
  options.path = dir.filePath(QStringLiteral("logs/waku.log"));
  {
    Guard guard;
    ASSERT_TRUE(RunLog::start(options, {QStringLiteral("--no-tray"), QStringLiteral("--x")},
                              QStringLiteral("C:/cfg")));
    EXPECT_TRUE(RunLog::active());
    qWarning().noquote() << "hello warning";
    RunLog::event(QStringLiteral("probe"), {{QStringLiteral("a"), 1}, {QStringLiteral("seq"), 99}});
    RunLog::stop(3);
    EXPECT_FALSE(RunLog::active());
  }
  const std::vector<QJsonObject> records = readRecords(options.path);
  ASSERT_GE(records.size(), 4u);

  const QJsonObject start = records.front();
  EXPECT_EQ(start.value(QStringLiteral("ev")).toString(), QStringLiteral("run.start"));
  EXPECT_EQ(start.value(QStringLiteral("configHome")).toString(), QStringLiteral("C:/cfg"));
  EXPECT_EQ(start.value(QStringLiteral("argv")).toArray().size(), 2);
  EXPECT_TRUE(start.contains(QStringLiteral("qt")));
  EXPECT_TRUE(start.contains(QStringLiteral("os")));
  EXPECT_GE(start.value(QStringLiteral("screens")).toArray().size(), 1);

  const QString run = start.value(QStringLiteral("run")).toString();
  double lastSeq = 0;
  for (const QJsonObject& r : records) {
    EXPECT_EQ(r.value(QStringLiteral("run")).toString(), run);
    EXPECT_GT(r.value(QStringLiteral("seq")).toDouble(), lastSeq);
    lastSeq = r.value(QStringLiteral("seq")).toDouble();
    EXPECT_TRUE(r.contains(QStringLiteral("t")));
    EXPECT_TRUE(r.contains(QStringLiteral("up")));
  }

  bool sawWarning = false;
  for (const QJsonObject& r : records) {
    if (r.value(QStringLiteral("k")).toString() == QLatin1String("log") &&
        r.value(QStringLiteral("msg")).toString() == QLatin1String("hello warning")) {
      EXPECT_EQ(r.value(QStringLiteral("lvl")).toString(), QStringLiteral("warning"));
      sawWarning = true;
    }
  }
  EXPECT_TRUE(sawWarning);

  const QJsonObject probe = findEvent(records, QStringLiteral("probe"));
  EXPECT_EQ(probe.value(QStringLiteral("a")).toInt(), 1);
  // The log's own keys win over a field of the same name.
  EXPECT_NE(probe.value(QStringLiteral("seq")).toInt(), 99);

  const QJsonObject stop = records.back();
  EXPECT_EQ(stop.value(QStringLiteral("ev")).toString(), QStringLiteral("run.stop"));
  EXPECT_EQ(stop.value(QStringLiteral("exit")).toInt(), 3);
}

TEST(RunLog, MessagesStillReachThePreviousHandler) {
  static int seen = 0;
  seen = 0;
  const QtMessageHandler before =
      qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString&) { ++seen; });
  QTemporaryDir dir;
  RunLogOptions options;
  options.path = dir.filePath(QStringLiteral("waku.log"));
  {
    Guard guard;
    ASSERT_TRUE(RunLog::start(options, {}, QString()));
    qInfo() << "forwarded";
    EXPECT_EQ(seen, 1);
  }
  // stop() put the previous handler back.
  qInfo() << "after";
  EXPECT_EQ(seen, 2);
  qInstallMessageHandler(before);
}

TEST(RunLog, RotatesWhenTheFileGrowsPastTheLimit) {
  QTemporaryDir dir;
  RunLogOptions options;
  options.path = dir.filePath(QStringLiteral("waku.log"));
  options.maxBytes = 2000;
  options.keep = 2;
  {
    Guard guard;
    ASSERT_TRUE(RunLog::start(options, {}, QString()));
    for (int i = 0; i < 200; ++i) qInfo() << "line number" << i << "with some padding to grow";
  }
  EXPECT_TRUE(QFile::exists(options.path));
  EXPECT_TRUE(QFile::exists(options.path + QStringLiteral(".1")));
  EXPECT_TRUE(QFile::exists(options.path + QStringLiteral(".2")));
  EXPECT_FALSE(QFile::exists(options.path + QStringLiteral(".3")));
  EXPECT_LE(QFileInfo(options.path).size(), options.maxBytes);
  // Every file holds whole JSON lines.
  EXPECT_FALSE(readRecords(options.path).empty());
  EXPECT_FALSE(readRecords(options.path + QStringLiteral(".1")).empty());
}

TEST(RunLog, ARunThatNeverStoppedIsReportedByTheNextOne) {
  QTemporaryDir dir;
  RunLogOptions options;
  options.path = dir.filePath(QStringLiteral("waku.log"));
  QFile previous(options.path);
  ASSERT_TRUE(previous.open(QIODevice::WriteOnly));
  previous.write(
      "{\"k\":\"ev\",\"ev\":\"run.start\",\"run\":\"deadbeef\",\"seq\":1}\n"
      "{\"k\":\"log\",\"lvl\":\"warning\",\"msg\":\"last "
      "words\",\"run\":\"deadbeef\",\"seq\":2}\n");
  previous.close();
  {
    Guard guard;
    ASSERT_TRUE(RunLog::start(options, {}, QString()));
  }
  const QJsonObject unclean =
      findEvent(readRecords(options.path), QStringLiteral("run.previous_unclean"));
  EXPECT_EQ(unclean.value(QStringLiteral("previousRun")).toString(), QStringLiteral("deadbeef"));
}

TEST(RunLog, ACleanPreviousRunIsNotReported) {
  QTemporaryDir dir;
  RunLogOptions options;
  options.path = dir.filePath(QStringLiteral("waku.log"));
  {
    Guard guard;
    ASSERT_TRUE(RunLog::start(options, {}, QString()));
  }
  {
    Guard guard;
    ASSERT_TRUE(RunLog::start(options, {}, QString()));
  }
  EXPECT_TRUE(
      findEvent(readRecords(options.path), QStringLiteral("run.previous_unclean")).isEmpty());
}

TEST(RunLog, AnUnwritablePathFailsAndLeavesTheLogOff) {
  QTemporaryDir dir;
  QFile blocker(dir.filePath(QStringLiteral("blocker")));
  ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
  blocker.close();
  RunLogOptions options;
  options.path = dir.filePath(QStringLiteral("blocker/waku.log"));  // a file where a folder must be
  EXPECT_FALSE(RunLog::start(options, {}, QString()));
  EXPECT_FALSE(RunLog::active());
}

TEST(RunLog, ASecondStartIsRefused) {
  QTemporaryDir dir;
  RunLogOptions options;
  options.path = dir.filePath(QStringLiteral("waku.log"));
  Guard guard;
  ASSERT_TRUE(RunLog::start(options, {}, QString()));
  EXPECT_FALSE(RunLog::start(options, {}, QString()));
}

TEST(RunLog, ScreenSignalsBecomeScreenChangedEventsWithASnapshot) {
  ASSERT_NE(qGuiApp, nullptr);
  QTemporaryDir dir;
  RunLogOptions options;
  options.path = dir.filePath(QStringLiteral("waku.log"));
  QScreen* screen = QGuiApplication::primaryScreen();
  ASSERT_NE(screen, nullptr);
  {
    Guard guard;
    ASSERT_TRUE(RunLog::start(options, {}, QString()));
    RunLog::watchScreens();
    emit qGuiApp->screenAdded(screen);
    emit screen->logicalDotsPerInchChanged(screen->logicalDotsPerInch());
  }
  const std::vector<QJsonObject> records = readRecords(options.path);
  QStringList reasons;
  for (const QJsonObject& r : records) {
    if (r.value(QStringLiteral("ev")).toString() != QLatin1String("screen.changed")) continue;
    reasons << r.value(QStringLiteral("why")).toString();
    const QJsonArray screens = r.value(QStringLiteral("screens")).toArray();
    ASSERT_GE(screens.size(), 1);
    const QJsonObject first = screens.first().toObject();
    EXPECT_TRUE(first.contains(QStringLiteral("dpr")));
    EXPECT_TRUE(first.contains(QStringLiteral("logicalDpi")));
    EXPECT_TRUE(first.contains(QStringLiteral("geometry")));
  }
  EXPECT_TRUE(reasons.contains(QStringLiteral("screenAdded")));
  EXPECT_TRUE(reasons.contains(QStringLiteral("logicalDotsPerInchChanged")));
}
