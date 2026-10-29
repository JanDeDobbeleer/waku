#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <optional>
#include <thread>

namespace yasb::platform {

struct KomorebicResult {
  bool started = false;
  bool timedOut = false;
  int exitCode = -1;
  QByteArray standardOutput;
  QByteArray standardError;
};

// Process-level access to komorebi. Replaceable so tests never talk to a live komorebi.
struct KomorebiBackend {
  // True when a komorebi.exe process exists (Python: is_process_running("komorebi.exe")).
  std::function<bool()> isKomorebiRunning;
  // Runs `komorebic <args>` hidden and waits up to `timeoutMs` for it to exit.
  std::function<KomorebicResult(const QStringList& args, int timeoutMs)> runKomorebic;
};

// `komorebic.exe` from PATH, CREATE_NO_WINDOW.
KomorebiBackend defaultKomorebiBackend();

// Fire-and-forget `komorebic <args>` without a console window.
bool launchKomorebic(const QStringList& args);

// MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL); komorebi's monitor "id" is this HMONITOR.
std::optional<long long> monitorFromWindow(quintptr hwnd);

// Port of KomorebiEventListener.run: owns a message-mode named pipe `\\.\pipe\<pipeName>`,
// subscribes komorebi to it with `komorebic subscribe-pipe`, reads one JSON notification per pipe
// message and reconnects after komorebi goes away.
//
// Threading: start() spawns one std::thread that owns the pipe handle and calls every callback;
// callbacks must not touch widgets (marshal to the GUI thread). stop() (also from the destructor)
// signals an event that cancels any wait or pending overlapped I/O, joins the thread, then runs
// `komorebic unsubscribe-pipe` if komorebi was subscribed.
class KomorebiPipeListener {
 public:
  struct Callbacks {
    // Raw `komorebic state` output after komorebi connected. Return false to retry (as Python
    // retries when the state is None).
    std::function<bool(const QByteArray& state)> connected;
    // One raw pipe message (expected: {"event": ..., "state": ...}).
    std::function<void(const QByteArray& message)> message;
    // The pipe ended or failed; a reconnect follows unless stopping.
    std::function<void()> disconnected;
  };
  struct Timing {
    int subscribeRetryMs = 5000;
    int stateRetryMs = 2000;
    int reconnectMs = 3000;
    int stateTimeoutMs = 2000;
    int subscribeTimeoutMs = 10000;
    int unsubscribeTimeoutMs = 2000;
  };

  KomorebiPipeListener(KomorebiBackend backend, Callbacks callbacks, QString pipeName,
                       Timing timing);
  ~KomorebiPipeListener();
  KomorebiPipeListener(const KomorebiPipeListener&) = delete;
  KomorebiPipeListener& operator=(const KomorebiPipeListener&) = delete;

  void start();
  void stop();
  bool isRunning() const { return thread_.joinable(); }
  const QString& pipeName() const { return pipeName_; }

  static constexpr unsigned long kBufferSize = 64 * 1024 * 8;
  static constexpr qsizetype kMaxMessageBytes = 64 * 1024 * 1024;

 private:
  void run();
  // Each returns false when stopping (or the wait was cancelled).
  bool waitUntilSubscribed();
  bool connectPipe();
  bool fetchState();
  void readLoop();
  bool sleep(int ms);

  KomorebiBackend backend_;
  Callbacks callbacks_;
  QString pipeName_;
  Timing timing_;
  void* stopEvent_ = nullptr;
  void* ioEvent_ = nullptr;
  void* pipe_ = nullptr;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> subscribed_{false};
  std::thread thread_;
};

}  // namespace yasb::platform
