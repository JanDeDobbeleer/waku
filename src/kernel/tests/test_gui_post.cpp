#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QTest>
#include <QThread>
#include <atomic>
#include <thread>

#include "kernel/qt/gui_post.h"

using wabar::kernel::postToGui;

TEST(GuiPost, RunsOnTheGuiThreadWhileTheGuardLives) {
  QObject guard;
  QPointer<QObject> pointer(&guard);
  std::atomic<bool> ran{false};
  QThread* ranOn = nullptr;
  std::thread([&] {
    postToGui(pointer, [&] {
      ranOn = QThread::currentThread();
      ran = true;
    });
  }).join();
  EXPECT_FALSE(ran.load());  // queued, not run on the posting thread
  EXPECT_TRUE(QTest::qWaitFor([&] { return ran.load(); }, 3000));
  EXPECT_EQ(ranOn, QCoreApplication::instance()->thread());
}

TEST(GuiPost, DropsTheCallWhenTheGuardIsGone) {
  auto* guard = new QObject;
  QPointer<QObject> pointer(guard);
  bool ran = false;
  postToGui(pointer, [&] { ran = true; });
  delete guard;
  QCoreApplication::processEvents();
  QTest::qWait(20);
  EXPECT_FALSE(ran);
}
