// The handlers of a running bar (reload, status, quit) behind the control pipe.
#include <gtest/gtest.h>

#include <QUuid>
#include <atomic>
#include <memory>
#include <thread>

#include "control/control.h"
#include "host/qml/control_handler.h"
#include "host/testing/bar_fixtures.h"

using namespace wabar::control;
using namespace wabar::host;

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
