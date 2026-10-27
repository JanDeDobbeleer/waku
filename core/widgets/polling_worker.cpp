#include "polling_worker.h"

namespace yasb::widgets {

PollingWorker::PollingWorker(PollFn poll, int intervalMs, QObject* parent)
    : QObject(parent), m_poll(std::move(poll)), m_interval(intervalMs) {}

PollingWorker::~PollingWorker() { stop(); }

void PollingWorker::start(bool pollImmediately) {
  if (m_thread.isRunning()) return;
  m_timer = new QTimer;
  m_timer->setInterval(m_interval);
  m_timer->moveToThread(&m_thread);
  connect(m_timer, &QTimer::timeout, m_timer, [this] { emit dataReady(m_poll()); });
  connect(&m_thread, &QThread::started, m_timer, [this, pollImmediately] {
    if (pollImmediately) emit dataReady(m_poll());
    m_timer->start();
  });
  connect(&m_thread, &QThread::finished, m_timer, &QObject::deleteLater);
  m_thread.start();
}

void PollingWorker::stop() {
  if (!m_thread.isRunning()) return;
  m_thread.quit();
  m_thread.wait();
  m_timer = nullptr;
}

}  // namespace yasb::widgets
