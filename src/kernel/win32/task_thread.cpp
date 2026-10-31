#include "kernel/win32/task_thread.h"

#include <QDebug>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <utility>

#include "kernel/win32/winrt_strings.h"

namespace wabar::kernel {

struct TaskThread::Poster::State {
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<Task> tasks;
  Task deferred;  // armed by postDeferred, runs at deferredAt
  std::chrono::steady_clock::time_point deferredAt;
  bool stopping = false;

  void post(Task task) {
    {
      std::lock_guard lock(mutex);
      if (stopping) return;
      tasks.push_back(std::move(task));
    }
    wake.notify_one();
  }
};

void TaskThread::Poster::operator()(Task task) const { state_->post(std::move(task)); }

TaskThread::TaskThread(QString label)
    : label_(std::move(label)), state_(std::make_shared<Poster::State>()) {}

TaskThread::~TaskThread() { stop(); }

void TaskThread::start(Task setUp, Task tearDown) {
  if (thread_.joinable()) return;
  {
    std::lock_guard lock(state_->mutex);
    state_->stopping = false;
  }
  thread_ = std::thread(
      [this, setUp = std::move(setUp), tearDown = std::move(tearDown)] { run(setUp, tearDown); });
}

void TaskThread::stop() {
  std::deque<Task> dropped;
  Task droppedDeferred;
  {
    std::lock_guard lock(state_->mutex);
    state_->stopping = true;
    dropped.swap(state_->tasks);
    droppedDeferred = std::exchange(state_->deferred, nullptr);
  }
  state_->wake.notify_all();
  if (thread_.joinable()) thread_.join();
}

void TaskThread::post(Task task) const { state_->post(std::move(task)); }

void TaskThread::postDeferred(std::chrono::milliseconds delay, Task task) const {
  {
    std::lock_guard lock(state_->mutex);
    if (state_->stopping || state_->deferred) return;
    state_->deferred = std::move(task);
    state_->deferredAt = std::chrono::steady_clock::now() + delay;
  }
  state_->wake.notify_one();
}

bool TaskThread::stopRequested() const {
  std::lock_guard lock(state_->mutex);
  return state_->stopping;
}

void TaskThread::run(const Task& setUp, const Task& tearDown) {
  bool apartment = false;
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    apartment = true;
  } catch (const winrt::hresult_error&) {
    // Already in an apartment; the process-wide MTA (main.cpp) keeps WinRT usable.
  }
  const auto guarded = [this](const Task& task) {
    if (!task) return;
    try {
      task();
    } catch (const winrt::hresult_error& error) {
      qWarning().noquote() << label_ << "task failed:" << toQString(error.message());
    } catch (const std::exception& error) {
      qWarning().noquote() << label_ << "task failed:" << error.what();
    } catch (...) {
      qWarning().noquote() << label_ << "task failed";
    }
  };

  guarded(setUp);
  Poster::State& state = *state_;
  for (;;) {
    Task task;
    {
      std::unique_lock lock(state.mutex);
      for (;;) {
        if (state.stopping) break;
        if (!state.tasks.empty()) {
          task = std::move(state.tasks.front());
          state.tasks.pop_front();
          break;
        }
        if (state.deferred && std::chrono::steady_clock::now() >= state.deferredAt) {
          task = std::exchange(state.deferred, nullptr);
          break;
        }
        if (state.deferred) {
          state.wake.wait_until(lock, state.deferredAt);
        } else {
          state.wake.wait(lock);
        }
      }
      if (state.stopping) break;
    }
    guarded(task);
  }
  guarded(tearDown);
  if (apartment) winrt::uninit_apartment();
}

}  // namespace wabar::kernel
