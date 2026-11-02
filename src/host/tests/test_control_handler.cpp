// The handlers of a running bar (reload, status, quit) behind the control pipe.
#include <gtest/gtest.h>

#include <QUuid>
#include <atomic>
#include <memory>
#include <thread>

#include "control/control.h"
#include "host/qml/control_handler.h"
#include "host/testing/shell_fixtures.h"

using namespace waku::control;
using namespace waku::host;

namespace {

// A pipe name no other test or running bar uses.
QString uniqueName() {
  return QStringLiteral("waku-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
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

// ---- the handler of a running bar ----

TEST(ControlHandlerTest, ReloadAppliesAChangedConfigAndAnswersOk) {
  Harness h;
  h.start(shellConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  bool quit = false;
  const ControlHandler handler = makeControlHandler(*h.host, false, [&quit] { quit = true; });

  write(h.shellFile(), shellConfig(QStringLiteral("after")));
  EXPECT_EQ(handler(QStringLiteral("reload")), QStringLiteral("ok"));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("after"));
  EXPECT_FALSE(quit);
}

TEST(ControlHandlerTest, AFailedReloadAnswersTheErrorAndKeepsTheBars) {
  Harness h;
  h.start(shellConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  QQuickWindow* before = h.window();
  const ControlHandler handler = makeControlHandler(*h.host, false);

  write(h.shellFile(), QStringLiteral("not qml {{{"));
  const QString answer = handler(QStringLiteral("reload"));
  EXPECT_TRUE(answer.startsWith(QStringLiteral("error: shell.qml:"))) << qPrintable(answer);
  EXPECT_EQ(h.window(), before);
}

TEST(ControlHandlerTest, StatusReportsBarsModeAndError) {
  Harness h;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  const ControlHandler handler = makeControlHandler(*h.host, false);
  EXPECT_EQ(handler(QStringLiteral("status")),
            QStringLiteral("running; pid=%1; windows=1; safe=no; error=none")
                .arg(QCoreApplication::applicationPid()));

  write(h.shellFile(), QStringLiteral("not qml {{{"));
  h.host->reload();
  const QString broken = handler(QStringLiteral("status"));
  EXPECT_TRUE(broken.contains(QStringLiteral("windows=1; safe=no; error=shell.qml:")))
      << qPrintable(broken);

  const ControlHandler safeHandler = makeControlHandler(*h.host, true);
  EXPECT_TRUE(safeHandler(QStringLiteral("status")).contains(QStringLiteral("safe=yes")));
}

TEST(ControlHandlerTest, QuitAnswersFirstThenRunsTheQuitFunction) {
  Harness h;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  bool quit = false;
  const ControlHandler handler = makeControlHandler(*h.host, false, [&quit] { quit = true; });
  EXPECT_EQ(handler(QStringLiteral("quit")), QStringLiteral("ok"));
  EXPECT_FALSE(quit);  // not yet: the answer is written first
  EXPECT_TRUE(QTest::qWaitFor([&] { return quit; }, 2000));
}

TEST(ControlHandlerTest, AnUnknownCommandIsAnError) {
  Harness h;
  h.start(shellConfig());
  const ControlHandler handler = makeControlHandler(*h.host, false);
  const QString answer = handler(QStringLiteral("explode"));
  EXPECT_TRUE(answer.startsWith(QStringLiteral("error: unknown command 'explode'")))
      << qPrintable(answer);
}

TEST(ControlHandlerTest, AnAddedCommandGetsTheWordsAfterItsVerb) {
  Harness h;
  h.start(shellConfig());
  QList<QStringList> seen;
  ControlCommands commands;
  commands[QStringLiteral("paint")] = [&seen](const QStringList& words) {
    seen << words;
    return QStringLiteral("ok: painted");
  };
  // A built-in verb is never taken over.
  commands[QStringLiteral("status")] = [](const QStringList&) { return QStringLiteral("ok: no"); };
  const ControlHandler handler = makeControlHandler(*h.host, false, {}, std::move(commands));

  EXPECT_EQ(handler(QStringLiteral("paint")), QStringLiteral("ok: painted"));
  EXPECT_EQ(handler(QStringLiteral("paint wall \"C:\\a b.png\"")), QStringLiteral("ok: painted"));
  EXPECT_EQ(handler(QStringLiteral("paint \"open")), QStringLiteral("error: unterminated quote"));
  EXPECT_EQ(seen,
            (QList<QStringList>{{}, {QStringLiteral("wall"), QStringLiteral("C:\\a b.png")}}));
  EXPECT_EQ(handler(QStringLiteral("status")).left(7), QStringLiteral("running"));
  // `painter` is not `paint`, and the error lists the added verb.
  const QString unknown = handler(QStringLiteral("painter"));
  EXPECT_TRUE(unknown.startsWith(QStringLiteral("error: unknown command 'painter'")))
      << qPrintable(unknown);
  EXPECT_TRUE(unknown.contains(QStringLiteral("call, paint)"))) << qPrintable(unknown);
}

TEST(ControlHandlerTest, TheWholePathFromAClientToAReload) {
  Harness h;
  h.start(shellConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  const QString name = uniqueName();
  ControlServer server(name, makeControlHandler(*h.host, false));
  ASSERT_TRUE(server.listen());

  write(h.shellFile(), shellConfig(QStringLiteral("after")));
  const ControlReply reply = askAndWait(name, QStringLiteral("reload"));
  EXPECT_EQ(reply.status, ControlReply::Ok);
  EXPECT_EQ(reply.text, QStringLiteral("ok"));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("after"));
}

TEST(ControlHandlerTest, TheAsyncHandlerReloadsFromAQueuedCallAndAnswersAfterIt) {
  Harness h;
  h.start(shellConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  const AsyncControlHandler handler = makeAsyncControlHandler(makeControlHandler(*h.host, false));

  write(h.shellFile(), shellConfig(QStringLiteral("after")));
  QString answer;
  handler(QStringLiteral("reload"), [&answer](const QString& line) { answer = line; });
  // Not inside the call: the reload waits for the event loop.
  EXPECT_TRUE(answer.isEmpty());
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("before"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !answer.isEmpty(); }, 5000));
  EXPECT_EQ(answer, QStringLiteral("ok"));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("after"));

  // Any other command answers at once.
  handler(QStringLiteral("status"), [&answer](const QString& line) { answer = line; });
  EXPECT_TRUE(answer.startsWith(QStringLiteral("running;"))) << qPrintable(answer);
}

TEST(ControlHandlerTest, StatusDuringAnAsyncReloadIsAnswered) {
  Harness h;
  h.start(shellConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  const QString name = uniqueName();
  ControlServer server(name, makeAsyncControlHandler(makeControlHandler(*h.host, false)), nullptr,
                       200);
  ASSERT_TRUE(server.listen());

  write(h.shellFile(), shellConfig(QStringLiteral("after")));
  auto reload = ask(name, QStringLiteral("reload"), 5000);
  auto status = ask(name, QStringLiteral("status"), 5000);
  ASSERT_TRUE(QTest::qWaitFor([&] { return reload->done.load() && status->done.load(); }, 8000));
  reload->thread.join();
  status->thread.join();
  EXPECT_EQ(reload->reply.text, QStringLiteral("ok"));
  EXPECT_TRUE(status->reply.text.startsWith(QStringLiteral("running;")))
      << qPrintable(status->reply.text);
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("after"));
}