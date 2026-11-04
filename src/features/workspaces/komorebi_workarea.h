#pragma once

#include <QObject>
#include <QtGlobal>
#include <functional>
#include <memory>

class QScreen;

namespace waku::workspaces::komorebi {

// What one notification did: whether komorebi's hidden window exists, whether the message was
// accepted, the window handle and the Win32 error of a refused send (0 otherwise).
struct WorkAreaNotifyResult {
  bool windowFound = false;
  bool sent = false;
  quintptr hwnd = 0;
  unsigned long error = 0;
};

// Tells komorebi that the work area changed. The real one (win32/komorebi_workarea_win.cpp) finds
// komorebi's hidden top-level window (class `komorebi-hidden`) and posts
// WM_SETTINGCHANGE(SPI_SETWORKAREA) to it with SendMessageTimeoutW (Windows refuses to queue this
// message to another process, so the send is synchronous with a 250 ms timeout). komorebi
// then compares its cached work areas with the OS and retiles only on a difference, so a
// notification it does not need costs nothing. Tests use testing/fake_workarea.h.
class WorkAreaNotifier {
 public:
  virtual ~WorkAreaNotifier() = default;
  virtual WorkAreaNotifyResult notify() = 0;
};

// Defined in win32/komorebi_workarea_win.cpp.
std::unique_ptr<WorkAreaNotifier> makeWin32WorkAreaNotifier();

// What one reload request did: whether komorebi and komorebi-bar both run, and whether
// `komorebic replace-configuration` was started.
struct KomorebiManageResult {
  bool barRunning = false;
  bool started = false;
};

// Makes a running komorebi and its bar pick up a changed display: `komorebic
// replace-configuration <komorebi.json>` rebuilds the window manager and respawns komorebi-bar,
// which does not follow a scale change on its own. The real one
// (win32/komorebi_manage_win.cpp) acts only when komorebi.exe and komorebi-bar.exe run, and
// starts komorebic without waiting for it. Created only with `--manage-komorebi`. Tests use
// testing/fake_workarea.h.
class KomorebiManager {
 public:
  virtual ~KomorebiManager() = default;
  virtual KomorebiManageResult reloadConfiguration() = 0;
};

// Defined in win32/komorebi_manage_win.cpp.
std::unique_ptr<KomorebiManager> makeWin32KomorebiManager();

// The guard's two single-shot timers, behind an interface so tests drive a fake clock. start()
// replaces a pending call of the same slot.
class WorkAreaTimers {
 public:
  enum class Slot { Settle, Late, Cooldown };
  virtual ~WorkAreaTimers() = default;
  virtual void start(Slot slot, int ms, std::function<void()> fire) = 0;
  virtual void stop(Slot slot) = 0;
  virtual bool active(Slot slot) const = 0;
};

// Two QTimers on the calling thread.
std::unique_ptr<WorkAreaTimers> makeQtWorkAreaTimers();

// Keeps komorebi's cached work area in step with the OS. komorebi refreshes its cache only from
// WM_SETTINGCHANGE(SPI_SETWORKAREA) and two other messages, and drops them when its bounded
// channel is full (a dock or scaling change); then windows tile under the bar. The guard listens
// to Qt's screen signals (QGuiApplication::screenAdded, screenRemoved, primaryScreenChanged;
// QScreen::availableGeometryChanged, geometryChanged, logicalDotsPerInchChanged: Qt emits
// availableGeometryChanged from the WM_SETTINGCHANGE Explorer broadcasts once a work area is
// applied), waits `settleMs` after the last one, then notifies at once and again `lateMs` later
// (after komorebi's 3 s monitor verification). A new burst re-arms the pair. No polling: idle, no
// timer runs.
//
// Created and owned by src/app/waku/main.cpp for the whole run, so a reload never loses the late
// notification. makeKomorebiWorkAreaGuard() gives it the real notifier only when
// kernel::liveDesktop("workarea_sync") (off with --no-workarea-sync and on the offscreen
// platform); without a notifier the guard is inert.
//
// Thread: GUI thread only. No thread of its own; the send waits at most 250 ms (SMTO_ABORTIFHUNG).
class KomorebiWorkAreaGuard : public QObject {
  Q_OBJECT
 public:
  struct Timing {
    int settleMs = 500;
    int lateMs = 5000;
    // After a reload request no other one starts for this long: the reload itself moves windows
    // and the bar, and that must not start another.
    int manageCooldownMs = 30000;
  };

  // A null `notifier` and a null `manager` make the guard inert; null `timers` means
  // makeQtWorkAreaTimers(). A `manager` reloads komorebi after a display change (see
  // displayChanged()).
  explicit KomorebiWorkAreaGuard(std::unique_ptr<WorkAreaNotifier> notifier,
                                 std::unique_ptr<WorkAreaTimers> timers = nullptr,
                                 Timing timing = {},
                                 std::unique_ptr<KomorebiManager> manager = nullptr,
                                 QObject* parent = nullptr);
  ~KomorebiWorkAreaGuard() override;

  // True when it has a notifier, so it can ever send.
  bool active() const { return notifier_ != nullptr; }
  // True when it has a manager, so it can ever reload komorebi.
  bool managing() const { return manager_ != nullptr; }

  // A display changed (a screen added or removed, the primary screen, a geometry or a scale): like
  // screensChanged(), and the settle also asks the manager to reload komorebi. A change of the
  // work area alone (the bar's own registration) does not.
  void displayChanged();

  // A screen changed: (re)starts the settle wait; the pair of notifications follows it. The slot
  // every screen signal is connected to.
  void screensChanged();
  // Notifies now and again `lateMs` later. The entry point for a trigger other than the screens
  // (for example komorebi's pipe connecting). Cancels a pending settle wait.
  void nudge();

 private:
  void watchScreen(QScreen* screen);
  void settled();
  void notifyOnce();
  void manage();

  std::unique_ptr<WorkAreaNotifier> notifier_;
  std::unique_ptr<WorkAreaTimers> timers_;
  Timing timing_;
  std::unique_ptr<KomorebiManager> manager_;
  // A display change is waiting for the settle.
  bool pendingManage_ = false;
  bool lastBarRunning_ = true;
  // The last "window found" answer, so a missing komorebi is logged once per change.
  bool lastFound_ = true;
  bool refusalLogged_ = false;
};

// The guard of a `waku` run: the real notifier when kernel::liveDesktop("workarea_sync"), none
// (inert) otherwise; the real manager when kernel::liveDesktop("manage_komorebi") (only with
// --manage-komorebi), none otherwise.
std::unique_ptr<KomorebiWorkAreaGuard> makeKomorebiWorkAreaGuard(QObject* parent = nullptr);

}  // namespace waku::workspaces::komorebi
