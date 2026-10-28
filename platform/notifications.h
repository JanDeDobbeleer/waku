#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <memory>

namespace yasb::platform {

// Receives source events. A source may call it from any thread.
class NotificationSink {
 public:
  virtual ~NotificationSink() = default;
  // Number of toasts in the Action Center (Windows 11 total, Windows 10 unread badge).
  virtual void countChanged(std::uint32_t count) = 0;
};

class NotificationSource {
 public:
  virtual ~NotificationSource() = default;
  // `sink` must stay valid until stop() returns; no sink call happens after that.
  virtual void start(NotificationSink* sink) = 0;
  virtual void stop() = 0;
  // Removes every toast from the Action Center. Asynchronous; the new count arrives via the sink.
  virtual void clearAll() = 0;
};

// WNF count subscription plus a UserNotificationListener worker (port of
// services/notifications/windows_notification.py, count and clear-all only).
std::unique_ptr<NotificationSource> createSystemNotificationSource();

// Shared notification count service (the notifications widget and, later, the control center
// share one through the runtime's shared-service registry under kServiceKey).
//
// Threading: the object lives on the GUI thread. The source's threads only call the sink, which
// re-posts every event to this object's thread; count_ is GUI-thread only. stop() (or the
// destructor) stops the source, which unsubscribes WNF and joins its worker thread.
class NotificationService final : public QObject {
  Q_OBJECT

 public:
  static constexpr const char* kServiceKey = "notifications";

  explicit NotificationService(std::unique_ptr<NotificationSource> source,
                               QObject* parent = nullptr);
  ~NotificationService() override;

  void start();
  Q_INVOKABLE void stop();

  int count() const { return count_; }
  void clearAll();

  // Applies a count on the GUI thread; emits countChanged only when it differs (the source's
  // sink posts here).
  void applyCount(std::uint32_t count);

 signals:
  void countChanged(int count);

 private:
  class Sink;

  std::unique_ptr<NotificationSource> source_;
  std::unique_ptr<Sink> sink_;
  int count_ = 0;
  bool started_ = false;
};

}  // namespace yasb::platform
