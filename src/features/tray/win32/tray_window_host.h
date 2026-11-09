#pragma once

#include <QString>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "features/tray/tray_host.h"
#include "features/tray/tray_icon_registry.h"

namespace waku::tray {

// A tray host that is the notification area itself: a window of class `Shell_TrayWnd` in waku's own
// process, which the programs that call Shell_NotifyIcon find and send their icons to. No DLL goes
// into Explorer, so it works on every architecture (an x64 build under emulation too) and an
// Explorer restart cannot break it.
//
// How it is found. Shell_NotifyIcon calls FindWindow("Shell_TrayWnd") on every call, and
// FindWindow walks the top-level windows in Z-order, hidden ones included. While the Windows
// taskbar is hidden this window sits above Explorer's in the topmost band, so it is found first.
// It claims that place only while no Explorer taskbar window is visible (waku hides them by
// default; `--keep-taskbar` leaves them visible and this host then stays at the bottom, idle, and
// Explorer's own tray keeps the icons). Every change of role broadcasts TaskbarCreated, so the
// programs send their icons again to the window that is now found.
//
// What is not ours is passed on, and only that: the app bar protocol of SHAppBarMessage
// (WM_COPYDATA, dwData 0) and the icon rectangle query (dwData 3) go to Explorer's own
// Shell_TrayWnd (the one in Explorer's process, whatever else has that class), so a program that
// asks "the taskbar" gets what Explorer answers. Nothing else is forwarded: a window of ours that
// passed on commands would run them for any sender with waku's identity. The sends are forwarded
// as sends, with a time limit.
//
// The host knows the icons it was sent and `replay()` announces them again, so it can outlive the
// widget that shows them (a reload) without every program having to send its icon a second time.
//
// Threads. One worker thread owns the window and its message loop; the host's signals are emitted
// from it (see TrayHost). The icon is read and scaled there too, inside the sender's call, so a
// program waits for that while it calls Shell_NotifyIcon, as it does for Explorer.
//
// Trust. Any program of the session can send to this window, as it can to Explorer's, and the data
// is parsed as untrusted: sizes are checked, and the icon handle is only read with GetIconInfo and
// GetDIBits on a copy.
class TrayWindowHost : public TrayHost {
  Q_OBJECT
 public:
  struct Options {
    // Window class, and the class Explorer's own tray window has. Tests use their own name.
    std::wstring windowClass = L"Shell_TrayWnd";
    // One host per session: a second one finds the mutex taken and is not valid.
    QString mutexName = QStringLiteral("Local\\WakuTrayWindowHost");
    // Claim and keep the Z-order place while Explorer's taskbar is hidden. Off in tests: the
    // window then only receives what is sent to it, and never competes with a real taskbar.
    bool claimOrder = true;
    // Pass what is not a tray notification on to Explorer's window. Off in tests.
    bool forwardToExplorer = true;
    // How often the role is checked (taskbar shown or hidden, Z-order lost to Explorer).
    std::chrono::milliseconds pollInterval{1000};
  };

  explicit TrayWindowHost(Options options = {}, QObject* parent = nullptr);
  ~TrayWindowHost() override;

  // Creates the window on the worker thread and returns when it exists (or failed).
  void start() override;
  void stop() override;
  QString mode() const override { return QStringLiteral("window"); }
  void replay() override;

  // True when the one-per-session mutex was taken.
  bool valid() const { return mutex_ != nullptr; }
  // The window (an HWND), 0 before start() or when it could not be created.
  std::uint64_t windowHandle() const { return window_.load(); }
  // True while this window is the one apps find (the Z-order place is claimed).
  bool owning() const { return owning_.load(); }

  // The window procedure's body. Internal: called from the worker thread only.
  std::int64_t handle(void* hwnd, unsigned message, std::uint64_t wParam, std::int64_t lParam);

 private:
  void run(void* ready);
  std::int64_t onCopyData(void* hwnd, std::uint64_t wParam, std::int64_t lParam);
  std::int64_t forward(void* hwnd, unsigned message, std::uint64_t wParam, std::int64_t lParam);
  void poll(void* hwnd);
  void broadcastTaskbarCreated();
  // Explorer's own Shell_TrayWnd (found by the process that owns it), null when there is none.
  void* explorerTaskbar(void* self);

  Options options_;
  void* mutex_ = nullptr;
  std::thread thread_;
  std::atomic<std::uint64_t> window_{0};
  std::atomic<bool> owning_{false};
  unsigned taskbarCreated_ = 0;
  // GetTickCount64 until which a TaskbarCreated seen is our own broadcast, not Explorer's.
  std::uint64_t ownBroadcastUntil_ = 0;
  // GetTickCount64 at which TaskbarCreated is sent once more; 0 when nothing is due.
  std::uint64_t rebroadcastAt_ = 0;
  // Polls in a row in which an Explorer taskbar window was visible while this host owned the
  // lookup; it gives the lookup up only after two (a restart is usually hidden again by then).
  int visiblePolls_ = 0;
  // Worker thread only: Explorer's process and its taskbar window, as last found.
  std::uint32_t explorerPid_ = 0;
  void* explorerWnd_ = nullptr;
  // The icons sent so far, merged the way the widget merges them; for replay().
  std::mutex iconsMutex_;
  TrayIconRegistry icons_;
};

}  // namespace waku::tray
