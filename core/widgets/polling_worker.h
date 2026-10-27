#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <functional>

namespace yasb::widgets {

// Runs `poll` on a worker thread every `intervalMs` and delivers each result to the owner's thread.
// The poll function must be self-contained (no widget access). Stop it before destroying captured
// state.
class PollingWorker : public QObject {
  Q_OBJECT
 public:
  using PollFn = std::function<QVariant()>;

  PollingWorker(PollFn poll, int intervalMs, QObject* parent = nullptr);
  ~PollingWorker() override;

  void start(bool pollImmediately = true);
  void stop();
  bool isRunning() const { return m_thread.isRunning(); }

 signals:
  void dataReady(const QVariant& data);

 private:
  PollFn m_poll;
  int m_interval;
  QThread m_thread;
  QTimer* m_timer = nullptr;
};

}  // namespace yasb::widgets
