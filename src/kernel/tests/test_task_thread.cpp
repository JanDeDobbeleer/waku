#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "kernel/win32/task_thread.h"

using namespace std::chrono_literals;
using wabar::kernel::TaskThread;

namespace {

// Waits up to `timeout` for `done`.
template <typename Done>
bool waitFor(Done done, std::chrono::milliseconds timeout = 3000ms) {
  const auto end = std::chrono::steady_clock::now() + timeout;
  while (!done()) {
    if (std::chrono::steady_clock::now() > end) return false;
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

}  // namespace

TEST(TaskThread, RunsSetUpTasksInOrderAndTearDownOnTheWorker) {
  std::mutex mutex;
  std::vector<int> order;
  std::thread::id setUpThread;
  std::thread::id taskThread;
  std::thread::id tearDownThread;
  const auto push = [&](int value) {
    std::lock_guard lock(mutex);
    order.push_back(value);
  };
  {
    TaskThread worker(QStringLiteral("Test"));
    worker.post([&] { push(1); });  // before start: runs after setUp
    worker.start(
        [&] {
          setUpThread = std::this_thread::get_id();
          push(0);
        },
        [&] {
          tearDownThread = std::this_thread::get_id();
          push(9);
        });
    worker.post([&] {
      taskThread = std::this_thread::get_id();
      push(2);
    });
    EXPECT_TRUE(waitFor([&] {
      std::lock_guard lock(mutex);
      return order.size() == 3;
    }));
  }  // the destructor stops and joins
  EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 9}));
  EXPECT_NE(setUpThread, std::this_thread::get_id());
  EXPECT_EQ(setUpThread, taskThread);
  EXPECT_EQ(taskThread, tearDownThread);
}

TEST(TaskThread, ThrowingTaskDoesNotStopTheWorker) {
  TaskThread worker(QStringLiteral("Test"));
  worker.start();
  std::atomic<bool> ran{false};
  worker.post([] { throw std::runtime_error("boom"); });
  worker.post([] { throw 42; });
  worker.post([&] { ran = true; });
  EXPECT_TRUE(waitFor([&] { return ran.load(); }));
}

TEST(TaskThread, StopDropsQueuedTasksAndLaterPosts) {
  TaskThread worker(QStringLiteral("Test"));
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false;
  bool release = false;
  std::atomic<int> after{0};
  worker.start();
  worker.post([&] {
    std::unique_lock lock(mutex);
    entered = true;
    cv.notify_all();
    cv.wait(lock, [&] { return release; });
  });
  worker.post([&] { ++after; });
  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return entered; });
  }
  std::thread stopper([&] { worker.stop(); });
  EXPECT_TRUE(waitFor([&] { return worker.stopRequested(); }));
  {
    std::lock_guard lock(mutex);
    release = true;
  }
  cv.notify_all();
  stopper.join();
  worker.post([&] { ++after; });
  const TaskThread::Poster poster = worker.poster();
  poster([&] { ++after; });
  EXPECT_FALSE(worker.running());
  EXPECT_EQ(after.load(), 0);
}

TEST(TaskThread, PosterOutlivesTheThread) {
  std::optional<TaskThread::Poster> poster;
  {
    TaskThread worker(QStringLiteral("Test"));
    worker.start();
    poster = worker.poster();
  }
  bool ran = false;
  (*poster)([&] { ran = true; });  // dropped, no crash
  EXPECT_FALSE(ran);
}

TEST(TaskThread, DeferredTaskRunsOnceForABurst) {
  TaskThread worker(QStringLiteral("Test"));
  worker.start();
  std::atomic<int> runs{0};
  for (int i = 0; i < 5; ++i) worker.postDeferred(30ms, [&] { ++runs; });
  EXPECT_TRUE(waitFor([&] { return runs.load() == 1; }));
  std::this_thread::sleep_for(60ms);
  EXPECT_EQ(runs.load(), 1);
  // Once it ran, the next request arms it again.
  worker.postDeferred(0ms, [&] { ++runs; });
  EXPECT_TRUE(waitFor([&] { return runs.load() == 2; }));
}

TEST(TaskThread, QueuedTasksRunBeforeADueDeferredTask) {
  TaskThread worker(QStringLiteral("Test"));
  std::mutex mutex;
  std::vector<int> order;
  worker.postDeferred(0ms, [&] {
    std::lock_guard lock(mutex);
    order.push_back(2);
  });
  worker.post([&] {
    std::lock_guard lock(mutex);
    order.push_back(1);
  });
  worker.start();
  EXPECT_TRUE(waitFor([&] {
    std::lock_guard lock(mutex);
    return order.size() == 2;
  }));
  EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST(TaskThread, StartsAgainAfterStop) {
  TaskThread worker(QStringLiteral("Test"));
  worker.start();
  worker.stop();
  EXPECT_FALSE(worker.running());
  std::atomic<bool> ran{false};
  worker.start();
  worker.post([&] { ran = true; });
  EXPECT_TRUE(waitFor([&] { return ran.load(); }));
}
