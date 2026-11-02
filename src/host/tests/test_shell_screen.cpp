// ShellScreen units. width and height are logical (device-independent) sizes, the units of window
// sizes and margins; x and y are the origin of QScreen::geometry() (native pixels in Qt 6, the
// space window positions live in); devicePixelRatio converts a size to physical pixels.
//
// The offscreen platform takes a screen layout from a JSON file (`offscreen:configfile=...`), so
// the units are checked on screens at DPR 1.5 and 2.0 with a non-zero origin. The platform is
// fixed when the process starts, so ShellScreenUnits re-runs this executable on each layout and
// ShellScreenUnitsChild (skipped in a normal run) holds the checks.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <cmath>

#include "host/qml/shell_screen.h"

namespace {

using waku::host::ScreenList;

// The layout a child process runs on, from the environment the parent sets.
struct Layout {
  double dpr = 1.0;
  int originX = 0;
  int originY = 0;
  int logicalWidth = 0;
  int logicalHeight = 0;
};

constexpr const char* kLayoutVar = "WAKU_TEST_SCREEN_LAYOUT";

QString encode(const Layout& l) {
  return QStringLiteral("%1,%2,%3,%4,%5")
      .arg(l.dpr)
      .arg(l.originX)
      .arg(l.originY)
      .arg(l.logicalWidth)
      .arg(l.logicalHeight);
}

bool decode(const QString& text, Layout* out) {
  const QStringList p = text.split(QLatin1Char(','));
  if (p.size() != 5) return false;
  out->dpr = p[0].toDouble();
  out->originX = p[1].toInt();
  out->originY = p[2].toInt();
  out->logicalWidth = p[3].toInt();
  out->logicalHeight = p[4].toInt();
  return true;
}

QByteArray configJson(const Layout& l) {
  QJsonObject screen;
  screen[QStringLiteral("name")] = QStringLiteral("TEST1");
  screen[QStringLiteral("x")] = l.originX;
  screen[QStringLiteral("y")] = l.originY;
  screen[QStringLiteral("width")] = l.logicalWidth;
  screen[QStringLiteral("height")] = l.logicalHeight;
  // The ratio comes from `dpr` alone; a logicalDpi above the base multiplies it again.
  screen[QStringLiteral("logicalDpi")] = 96;
  screen[QStringLiteral("logicalBaseDpi")] = 96;
  screen[QStringLiteral("dpr")] = l.dpr;
  QJsonObject root;
  root[QStringLiteral("screens")] = QJsonArray{screen};
  return QJsonDocument(root).toJson();
}

// Runs the child checks on `layout`; returns the child's output on failure, empty on success.
QString runChild(const Layout& layout, QTemporaryDir& dir) {
  const QString config = dir.filePath(QStringLiteral("screens.json"));
  QFile file(config);
  if (!file.open(QIODevice::WriteOnly)) return QStringLiteral("cannot write ") + config;
  file.write(configJson(layout));
  file.close();

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  // The platform string splits at ':', so a drive letter breaks the path: pass a relative one.
  env.insert(QStringLiteral("WAKU_TEST_SCREEN_CONFIG"), QStringLiteral("screens.json"));
  env.insert(QString::fromLatin1(kLayoutVar), encode(layout));
  QProcess child;
  child.setWorkingDirectory(dir.path());
  child.setProcessEnvironment(env);
  child.setProcessChannelMode(QProcess::MergedChannels);
  child.start(QCoreApplication::applicationFilePath(),
              {QStringLiteral("--gtest_filter=ShellScreenUnitsChild.*")});
  if (!child.waitForFinished(60000)) {
    child.kill();
    return QStringLiteral("child timed out");
  }
  const QString output = QString::fromLocal8Bit(child.readAll());
  const bool ok = child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0 &&
                  output.contains(QStringLiteral("[  PASSED  ] 1 test"));
  return ok ? QString() : output;
}

}  // namespace

TEST(ShellScreenUnits, LogicalSizeAtDpr1_5) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  // 3840x2560 physical pixels.
  const QString failure = runChild({1.5, 2560, 120, 2560, 1707}, dir);
  EXPECT_TRUE(failure.isEmpty()) << failure.toStdString();
}

TEST(ShellScreenUnits, LogicalSizeAtDpr2_0) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  // 3840x2160 physical pixels, to the right of a 2560-wide screen.
  const QString failure = runChild({2.0, 2560, 300, 1920, 1080}, dir);
  EXPECT_TRUE(failure.isEmpty()) << failure.toStdString();
}

TEST(ShellScreenUnitsChild, MatchesQScreenUnits) {
  Layout layout;
  if (!decode(qEnvironmentVariable(kLayoutVar), &layout)) GTEST_SKIP() << "run by ShellScreenUnits";

  ScreenList list;
  ASSERT_EQ(list.list().size(), 1);
  const waku::host::ShellScreen* screen = list.list().first();
  const QScreen* qscreen = screen->qscreen();
  ASSERT_NE(qscreen, nullptr);

  // The platform runs at the layout under test.
  ASSERT_DOUBLE_EQ(qscreen->devicePixelRatio(), layout.dpr);

  // Size: logical, the units of window sizes, and QScreen::size().
  EXPECT_EQ(screen->width(), layout.logicalWidth);
  EXPECT_EQ(screen->height(), layout.logicalHeight);
  EXPECT_EQ(screen->width(), qscreen->size().width());
  EXPECT_EQ(screen->height(), qscreen->size().height());

  // Origin: QScreen::geometry(), the space window positions use.
  EXPECT_EQ(screen->x(), qscreen->geometry().x());
  EXPECT_EQ(screen->y(), qscreen->geometry().y());
  EXPECT_EQ(screen->x(), layout.originX);
  EXPECT_EQ(screen->y(), layout.originY);

  // The ratio converts a logical size to physical pixels.
  EXPECT_DOUBLE_EQ(screen->devicePixelRatio(), layout.dpr);
  EXPECT_EQ(std::lround(screen->width() * screen->devicePixelRatio()),
            std::lround(layout.logicalWidth * layout.dpr));
  EXPECT_GT(screen->width() * screen->devicePixelRatio(), screen->width());
}
