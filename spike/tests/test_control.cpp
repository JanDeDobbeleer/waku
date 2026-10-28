#include <gtest/gtest.h>

#include <QLocalSocket>
#include <QUuid>
#include <atomic>
#include <memory>
#include <thread>

#include "spike/bar/control_handler.h"
#include "spike/control/control.h"
#include "spike/tests/bar_fixtures.h"

using namespace tidy;

namespace {

// A pipe name no other test or running bar uses.
QString uniqueName() {
  return QStringLiteral("tidy-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

// The blocking client runs on its own thread, because the server needs this thread's event loop.
struct Asked {
  std::atomic<bool> done{false};
  ControlReply reply;
  std::thread thread;
  ~Asked() {
    if (thread.joinable()) thread.join();
  }
};

std::unique_ptr<Asked> ask(const QString& name, const QString& command, int timeoutMs = 3000) {
  auto asked = std::make_unique<Asked>();
  Asked* raw = asked.get();
  asked->thread = std::thread([raw, name, command, timeoutMs] {
    raw->reply = sendControlCommand(name, command, timeoutMs);
    raw->done = true;
  });
  return asked;
}

ControlReply askAndWait(const QString& name, const QString& command) {
  auto asked = ask(name, command);
  EXPECT_TRUE(QTest::qWaitFor([&] { return asked->done.load(); }, 5000));
  asked->thread.join();
  return asked->reply;
}

}  // namespace

// ---- names and config home ----

TEST(ControlNames, ThePipeNameIsStableAndIgnoresCaseSlashesAndTrailingSeparators) {
  const QString a = controlPipeName(QStringLiteral("C:\\Users\\me\\.config\\tidy"));
  EXPECT_TRUE(a.startsWith(QStringLiteral("tidy-")));
  EXPECT_EQ(a, controlPipeName(QStringLiteral("c:/users/ME/.config/tidy/")));
  EXPECT_EQ(a, controlPipeName(QStringLiteral("C:\\Users\\me\\.config\\tidy\\")));
}

TEST(ControlNames, DifferentConfigHomesGetDifferentPipes) {
  EXPECT_NE(controlPipeName(QStringLiteral("C:\\a")), controlPipeName(QStringLiteral("C:\\b")));
}

TEST(ControlNames, TheConfigHomeIsTheOptionThenTheEnvironmentThenTheFallback) {
  qunsetenv("TIDY_CONFIG_HOME");
  EXPECT_EQ(resolveConfigHome(QString(), QStringLiteral("fallback")), QStringLiteral("fallback"));
  qputenv("TIDY_CONFIG_HOME", "from-env");
  EXPECT_EQ(resolveConfigHome(QString(), QStringLiteral("fallback")), QStringLiteral("from-env"));
  EXPECT_EQ(resolveConfigHome(QStringLiteral("from-option"), QStringLiteral("fallback")),
            QStringLiteral("from-option"));
  qunsetenv("TIDY_CONFIG_HOME");
}

// ---- the transport ----

TEST(ControlChannel, AClientGetsTheAnswerOfTheHandler) {
  const QString name = uniqueName();
  ControlServer server(name,
                       [](const QString& command) { return QStringLiteral("got:") + command; });
  ASSERT_TRUE(server.listen());
  const ControlReply reply = askAndWait(name, QStringLiteral("ping"));
  EXPECT_EQ(reply.status, ControlReply::Ok);
  EXPECT_EQ(reply.text, QStringLiteral("got:ping"));
}

TEST(ControlChannel, SeveralCommandsInARowAreAnsweredEach) {
  const QString name = uniqueName();
  int calls = 0;
  ControlServer server(name, [&calls](const QString& command) {
    return QStringLiteral("%1:%2").arg(++calls).arg(command);
  });
  ASSERT_TRUE(server.listen());
  EXPECT_EQ(askAndWait(name, QStringLiteral("a")).text, QStringLiteral("1:a"));
  EXPECT_EQ(askAndWait(name, QStringLiteral("b")).text, QStringLiteral("2:b"));
  EXPECT_EQ(askAndWait(name, QStringLiteral("c")).text, QStringLiteral("3:c"));
}

TEST(ControlChannel, NothingListeningMeansNotRunning) {
  const ControlReply reply = sendControlCommand(uniqueName(), QStringLiteral("reload"), 500);
  EXPECT_EQ(reply.status, ControlReply::NotRunning);
}

TEST(ControlChannel, ASecondServerOnTheSameNameIsRefused) {
  const QString name = uniqueName();
  ControlServer first(name, [](const QString&) { return QStringLiteral("one"); });
  ASSERT_TRUE(first.listen());
  ControlServer second(name, [](const QString&) { return QStringLiteral("two"); });
  EXPECT_FALSE(second.listen());
  EXPECT_EQ(askAndWait(name, QStringLiteral("x")).text, QStringLiteral("one"));
}

TEST(ControlChannel, AThrowingHandlerIsAnErrorAnswerNotACrash) {
  const QString name = uniqueName();
  ControlServer server(name, [](const QString&) -> QString { throw std::runtime_error("boom"); });
  ASSERT_TRUE(server.listen());
  const ControlReply reply = askAndWait(name, QStringLiteral("x"));
  EXPECT_EQ(reply.status, ControlReply::Ok);
  EXPECT_EQ(reply.text, QStringLiteral("error: boom"));
  // The server still works afterwards.
  EXPECT_EQ(askAndWait(name, QStringLiteral("y")).text, QStringLiteral("error: boom"));
}

TEST(ControlChannel, AnOverlongLineIsRefused) {
  const QString name = uniqueName();
  ControlServer server(name, [](const QString&) { return QStringLiteral("never"); });
  ASSERT_TRUE(server.listen());
  const ControlReply reply =
      askAndWait(name, QString(ControlServer::kMaxCommandBytes * 3, QLatin1Char('x')));
  EXPECT_EQ(reply.text, QStringLiteral("error: command too long"));
}

TEST(ControlChannel, AClientThatNeverFinishesItsLineIsDropped) {
  const QString name = uniqueName();
  ControlServer server(name, [](const QString&) { return QStringLiteral("never"); }, nullptr, 200);
  ASSERT_TRUE(server.listen());
  QLocalSocket slow;
  slow.connectToServer(name);
  ASSERT_TRUE(slow.waitForConnected(1000));
  slow.write("rel");  // no newline
  slow.flush();
  EXPECT_TRUE(
      QTest::qWaitFor([&] { return slow.state() == QLocalSocket::UnconnectedState; }, 3000));
}

// ---- the handler of a running bar ----

TEST(ControlHandlerTest, ReloadAppliesAChangedConfigAndAnswersOk) {
  Harness h;
  h.start(barConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  bool quit = false;
  const ControlHandler handler = makeControlHandler(*h.host, false, [&quit] { quit = true; });

  write(h.barFile(), barConfig(QStringLiteral("after")));
  EXPECT_EQ(handler(QStringLiteral("reload")), QStringLiteral("ok"));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("after"));
  EXPECT_FALSE(quit);
}

TEST(ControlHandlerTest, AFailedReloadAnswersTheErrorAndKeepsTheBars) {
  Harness h;
  h.start(barConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  QQuickWindow* before = h.window();
  const ControlHandler handler = makeControlHandler(*h.host, false);

  write(h.barFile(), QStringLiteral("not qml {{{"));
  const QString answer = handler(QStringLiteral("reload"));
  EXPECT_TRUE(answer.startsWith(QStringLiteral("error: bar.qml:"))) << qPrintable(answer);
  EXPECT_EQ(h.window(), before);
}

TEST(ControlHandlerTest, StatusReportsBarsModeAndError) {
  Harness h;
  h.start(barConfig());
  ASSERT_TRUE(h.host->reload());
  const ControlHandler handler = makeControlHandler(*h.host, false);
  EXPECT_EQ(handler(QStringLiteral("status")),
            QStringLiteral("running; bars=1; safe=no; error=none"));

  write(h.barFile(), QStringLiteral("not qml {{{"));
  h.host->reload();
  const QString broken = handler(QStringLiteral("status"));
  EXPECT_TRUE(broken.startsWith(QStringLiteral("running; bars=1; safe=no; error=bar.qml:")))
      << qPrintable(broken);

  const ControlHandler safeHandler = makeControlHandler(*h.host, true);
  EXPECT_TRUE(safeHandler(QStringLiteral("status")).contains(QStringLiteral("safe=yes")));
}

TEST(ControlHandlerTest, QuitAnswersFirstThenRunsTheQuitFunction) {
  Harness h;
  h.start(barConfig());
  ASSERT_TRUE(h.host->reload());
  bool quit = false;
  const ControlHandler handler = makeControlHandler(*h.host, false, [&quit] { quit = true; });
  EXPECT_EQ(handler(QStringLiteral("quit")), QStringLiteral("ok"));
  EXPECT_FALSE(quit);  // not yet: the answer is written first
  EXPECT_TRUE(QTest::qWaitFor([&] { return quit; }, 2000));
}

TEST(ControlHandlerTest, AnUnknownCommandIsAnError) {
  Harness h;
  h.start(barConfig());
  const ControlHandler handler = makeControlHandler(*h.host, false);
  const QString answer = handler(QStringLiteral("explode"));
  EXPECT_TRUE(answer.startsWith(QStringLiteral("error: unknown command 'explode'")))
      << qPrintable(answer);
}

TEST(ControlHandlerTest, TheWholePathFromAClientToAReload) {
  Harness h;
  h.start(barConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  const QString name = uniqueName();
  ControlServer server(name, makeControlHandler(*h.host, false));
  ASSERT_TRUE(server.listen());

  write(h.barFile(), barConfig(QStringLiteral("after")));
  const ControlReply reply = askAndWait(name, QStringLiteral("reload"));
  EXPECT_EQ(reply.status, ControlReply::Ok);
  EXPECT_EQ(reply.text, QStringLiteral("ok"));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("after"));
}
