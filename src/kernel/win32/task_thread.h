#pragma once

#include <QString>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

namespace waku::kernel {

// One worker std::thread in the multi-threaded WinRT apartment that runs posted tasks in order, for
// a source whose WinRT or COM objects must live on one thread the GUI thread never blocks on.
//
// Threads: start() and stop() run on the owner's thread (the GUI thread); post(), postDeferred(),
// stopRequested() and a Poster work from any thread, including WinRT delegate threads. The worker
// owns whatever `setUp` creates; `tearDown` releases it on the worker after the last task. stop()
// sets the stop flag, wakes the worker and joins it, so it waits for the running task at most;
// queued tasks are dropped. A task that throws is logged as "<label> task failed" and the worker
// goes on. start() after stop() runs a fresh thread on the same queue.
class TaskThread {
 public:
  using Task = std::function<void()>;

  explicit TaskThread(QString label);
  ~TaskThread();
  TaskThread(const TaskThread&) = delete;
  TaskThread& operator=(const TaskThread&) = delete;

  // Starts the worker. `setUp` runs on it first, `tearDown` last; both are exception-guarded.
  // Tasks posted before start() run after `setUp`. Nothing happens while the worker runs.
  void start(Task setUp = {}, Task tearDown = {});
  // Stops and joins the worker. Never call it from a task.
  void stop();
  bool running() const { return thread_.joinable(); }

  // Queues `task`; dropped once stop() has begun.
  void post(Task task) const;
  // Arms `task` to run `delay` from now, after the queued tasks. While one is armed and not yet
  // started, further calls do nothing, so a burst of requests runs it once.
  void postDeferred(std::chrono::milliseconds delay, Task task) const;
  // True once stop() has begun; a long task checks it between steps to end early.
  bool stopRequested() const;

  // A copyable post() for delegates that may outlive this object: after stop() or destruction it
  // drops the task.
  class Poster {
   public:
    void operator()(Task task) const;

   private:
    friend class TaskThread;
    struct State;
    explicit Poster(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
  };
  Poster poster() const { return Poster(state_); }

 private:
  void run(const Task& setUp, const Task& tearDown);

  const QString label_;
  std::shared_ptr<Poster::State> state_;
  std::thread thread_;
};

}  // namespace waku::kernel
