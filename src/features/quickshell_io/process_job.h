#pragma once

#include <QtGlobal>
#include <memory>

namespace wabar::quickshell_io {

// A Windows job object that owns one child and its descendants: closing the job (or terminate())
// kills the whole tree. One job per started process. Not thread-safe; GUI thread only.
class ProcessJob {
 public:
  ProcessJob();
  ~ProcessJob();
  ProcessJob(const ProcessJob&) = delete;
  ProcessJob& operator=(const ProcessJob&) = delete;

  // Puts the process `pid` into the job. False when the job or the assignment fails (the process
  // then runs outside it and only itself can be killed).
  bool adopt(qint64 pid);
  bool adopted() const;
  // Kills every process in the job.
  void terminate();
  // Lets the remaining descendants survive the close of the job (the child exited normally).
  void release();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace wabar::quickshell_io
