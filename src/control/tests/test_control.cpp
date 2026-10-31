#include <gtest/gtest.h>

#include <QLocalSocket>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <atomic>
#include <memory>
#include <thread>

#include "control/control.h"

using namespace wabar::control;

namespace {

// A pipe name no other test or running bar uses.
QString uniqueName() {
  return QStringLiteral("wabar-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
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
  const QString a = controlPipeName(QStringLiteral("C:\\Users\\me\\.config\\wabar"));
  EXPECT_TRUE(a.startsWith(QStringLiteral("wabar-")));
  EXPECT_EQ(a, controlPipeName(QStringLiteral("c:/users/ME/.config/wabar/")));
  EXPECT_EQ(a, controlPipeName(QStringLiteral("C:\\Users\\me\\.config\\wabar\\")));
}

TEST(ControlNames, DifferentConfigHomesGetDifferentPipes) {
  EXPECT_NE(controlPipeName(QStringLiteral("C:\\a")), controlPipeName(QStringLiteral("C:\\b")));
}

TEST(ControlNames, TheConfigHomeIsTheOptionThenTheEnvironmentThenTheFallback) {
  qunsetenv("WABAR_CONFIG_HOME");
  EXPECT_EQ(resolveConfigHome(QString(), QStringLiteral("fallback")), QStringLiteral("fallback"));
  qputenv("WABAR_CONFIG_HOME", "from-env");
  EXPECT_EQ(resolveConfigHome(QString(), QStringLiteral("fallback")), QStringLiteral("from-env"));
  EXPECT_EQ(resolveConfigHome(QStringLiteral("from-option"), QStringLiteral("fallback")),
            QStringLiteral("from-option"));
  qunsetenv("WABAR_CONFIG_HOME");
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

TEST(ControlChannel, ALineThatArrivedWhileTheThreadWasBusyIsAnswered) {
  const QString name = uniqueName();
  // "busy" blocks the thread for longer than the idle timeout a moment after it was read, as a
  // reload does: a second client that connected meanwhile has its timer and its line pending
  // when the loop runs again.
  ControlServer server(
      name,
      [](const QString& command, ControlReplyFn reply) {
        if (command != QLatin1String("busy")) {
          reply(QStringLiteral("got:") + command);
          return;
        }
        QTimer::singleShot(100, [reply] {
          QThread::msleep(600);
          reply(QStringLiteral("done"));
        });
      },
      nullptr, 200);
  ASSERT_TRUE(server.listen());
  auto busy = ask(name, QStringLiteral("busy"), 5000);
  auto status = ask(name, QStringLiteral("status"), 5000);
  ASSERT_TRUE(QTest::qWaitFor([&] { return busy->done.load() && status->done.load(); }, 5000));
  busy->thread.join();
  status->thread.join();
  EXPECT_EQ(busy->reply.text, QStringLiteral("done"));
  EXPECT_EQ(status->reply.status, ControlReply::Ok);
  EXPECT_EQ(status->reply.text, QStringLiteral("got:status"));
}

TEST(ControlChannel, ADeferredAnswerLaterThanTheIdleTimeoutArrives) {
  const QString name = uniqueName();
  ControlServer server(
      name,
      [](const QString& command, ControlReplyFn reply) {
        QTimer::singleShot(600, [command, reply] { reply(QStringLiteral("late:") + command); });
      },
      nullptr, 200);
  ASSERT_TRUE(server.listen());
  const ControlReply reply = askAndWait(name, QStringLiteral("reload"));
  EXPECT_EQ(reply.status, ControlReply::Ok);
  EXPECT_EQ(reply.text, QStringLiteral("late:reload"));
}

TEST(ControlChannel, AnAsyncHandlerMayAnswerAtOnceAndOnlyOnce) {
  const QString name = uniqueName();
  ControlServer server(
      name,
      [](const QString& command, ControlReplyFn reply) {
        reply(QStringLiteral("first:") + command);
        reply(QStringLiteral("second"));
      },
      nullptr);
  ASSERT_TRUE(server.listen());
  EXPECT_EQ(askAndWait(name, QStringLiteral("x")).text, QStringLiteral("first:x"));
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
