#pragma once

#include <QByteArray>
#include <QString>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "features/tray/tray_host.h"

namespace waku::tray {

// Names shared with hook/trayhook.cpp. CMake passes them to both targets as compile definitions
// (WAKU_TRAY_PIPE_NAME, WAKU_TRAY_MUTEX_NAME); the defaults below keep other build systems
// working.
#ifndef WAKU_TRAY_PIPE_NAME
#define WAKU_TRAY_PIPE_NAME "waku_systray_monitor"
#endif
#ifndef WAKU_TRAY_MUTEX_NAME
#define WAKU_TRAY_MUTEX_NAME "WakuTrayHookAlive"
#endif

inline constexpr int kHookPipeBufferSize = 32 * 1024;
// The pipe's own buffer quota: a 256x256 icon message (about 263 KB) fits, so a busy host does not
// make the hook DLL wait in Explorer.
inline constexpr int kHookPipeQuota = 512 * 1024;
inline constexpr std::chrono::milliseconds kNoExplorerDelay{1000};
inline constexpr std::chrono::milliseconds kInjectionFailedDelay{5000};
inline constexpr std::chrono::milliseconds kAfterSessionDelay{3000};

// How the hook DLL gets into Explorer. The real one (`makeExplorerInjector`) uses
// SetWindowsHookEx(WH_GETMESSAGE); tests substitute a fake.
class TrayInjector {
 public:
  virtual ~TrayInjector() = default;
  virtual std::optional<std::uint32_t> explorerPid() = 0;
  // The hook DLL is already loaded in Explorer (a previous run left it there).
  virtual bool isHookLoaded(std::uint32_t pid) = 0;
  // Returns the hook handle (HHOOK) or 0 on failure.
  virtual std::uintptr_t inject(std::uint32_t pid) = 0;
  // UnhookWindowsHookEx for a handle returned by inject.
  virtual void release(std::uintptr_t hook) = 0;
  // The host's stop request: the waits inside explorerPid and inject end early once it returns
  // true, so stop() does not wait out an Explorer restart. Set once, before the worker starts.
  virtual void setStopCheck(std::function<bool()> stopping) { (void)stopping; }
};

// `dllPath` is the full path of WakuTrayHook[_arm64].dll.
std::unique_ptr<TrayInjector> makeExplorerInjector(const QString& dllPath);

// Path of the hook DLL for this host, searched next to the executable then in a lib folder.
// Empty when the DLL is missing or the architecture unsupported; `reason` says why.
QString findTrayHookDll(const QString& appDir, QString* reason = nullptr);

// Port of services/systray/systray_hook.py: owns the watchdog mutex and the message pipe the
// injected DLL writes to, injects the DLL into Explorer and turns the pipe messages into
// TrayHost signals.
//
// Threads: the constructor and stop() run on the creating (GUI) thread, run() on one worker
// thread (started by start(), or by a test). The worker owns the OVERLAPPED structures, the read
// buffer and the completion event; stop() only cancels I/O, sets the stop flag and the stop event
// and, when no worker is active, closes the pipe itself. The worker's I/O waits block on the
// completion event and the stop event, so an idle pipe costs no wakeups. The stop event is closed
// by the destructor. stop() joins the worker started by start().
class SystrayHook : public TrayHost {
  Q_OBJECT
 public:
  struct Options {
    QString pipeName = QStringLiteral("\\\\.\\pipe\\" WAKU_TRAY_PIPE_NAME);
    QString mutexName = QStringLiteral("Global\\" WAKU_TRAY_MUTEX_NAME);
    std::unique_ptr<TrayInjector> injector;
    // Called with every retry delay before the worker waits (tests record them).
    std::function<void(std::chrono::milliseconds)> retryObserver;
    // The pause after a pipe session ends (the DLL disconnected) before the next attempt.
    std::chrono::milliseconds afterSessionDelay = kAfterSessionDelay;
  };

  explicit SystrayHook(Options options, QObject* parent = nullptr);
  ~SystrayHook() override;

  void start() override;
  void stop() override;
  QString mode() const override { return QStringLiteral("hook"); }

  // True when the watchdog mutex and the pipe were created.
  bool valid() const;
  // The worker loop; blocks until stop(). Exposed for tests.
  void run();

 protected:
  // One complete pipe message. Runs on the worker thread.
  // Returns false to end the session (Python: a malformed message raises out of the read loop).
  virtual bool handleMessage(const QByteArray& message);

 private:
  enum class Connect { Connected, Stopped, Broken };
  enum class Read { Message, Stopped, Broken };
  struct Io;

  bool ensureInjected();
  void runPipeSession(Io& io);
  Connect connectPipe(Io& io);
  Read readMessage(Io& io, QByteArray& message);
  bool waitForIo(Io& io, void* overlapped);
  // Waits up to `delay` or until stop(); true when stop was requested.
  bool waitStop(std::chrono::milliseconds delay);
  void unhookLocked();

  Options options_;
  std::mutex state_;
  std::condition_variable stopCv_;
  bool stopRequested_ = false;
  // stopRequested_ without the lock, for the injector's waits.
  std::atomic<bool> stopping_{false};
  std::atomic<bool> running_{false};
  bool workerActive_ = false;
  void* mutex_ = nullptr;
  void* pipe_ = nullptr;
  void* stopEvent_ = nullptr;  // manual-reset; set by stop(), closed by the destructor
  std::uintptr_t hook_ = 0;
  std::thread thread_;
};

}  // namespace waku::tray
