#pragma once

#include <QString>
#include <memory>
#include <optional>

namespace waku::desktop_state {

struct LockKeys {
  bool caps = false;
  bool num = false;
  bool scroll = false;

  bool operator==(const LockKeys&) const = default;
};

struct KeyboardLayoutInfo {
  QString shortName;    // upper-case language code, for example "EN"
  QString displayName;  // localized language and region, for example "English (United States)"
  QString id;           // the layout handle in hex, for example "04090409"

  bool operator==(const KeyboardLayoutInfo&) const = default;
};

// What the desktop-state singleton reads and sets on the machine; tests give it a fake.
//
// Threading: GUI thread only. `setKeepAwake` uses SetThreadExecutionState, which acts on the
// calling thread, so it must always be called from the same thread (the GUI thread).
class DesktopProbe {
 public:
  virtual ~DesktopProbe() = default;

  // Toggle state of Caps Lock, Num Lock and Scroll Lock.
  virtual LockKeys lockKeys() = 0;
  // The keyboard layout of the foreground window's thread; empty when it cannot be read.
  virtual KeyboardLayoutInfo keyboardLayout() = 0;
  // Seconds since the last keyboard or mouse input of the session; nullopt when it cannot be read.
  virtual std::optional<double> idleSeconds() = 0;
  // Keeps the display and the system awake (true) or lets them sleep (false).
  virtual void setKeepAwake(bool keepAwake) = 0;
};

// The Win32 implementation (GetKeyState, GetKeyboardLayout, GetLastInputInfo,
// SetThreadExecutionState). Never null.
std::unique_ptr<DesktopProbe> createSystemDesktopProbe();

// Counts the holders that want the machine awake and calls `setKeepAwake` on the 0 to 1 and 1 to 0
// edges, so several IdleInhibitor objects share one execution state.
//
// Threading: GUI thread only. A null probe makes every call a no-op.
class IdleInhibitCounter {
 public:
  explicit IdleInhibitCounter(DesktopProbe* probe) : probe_(probe) {}
  ~IdleInhibitCounter() {
    if (count_ > 0 && probe_) probe_->setKeepAwake(false);
  }
  IdleInhibitCounter(const IdleInhibitCounter&) = delete;
  IdleInhibitCounter& operator=(const IdleInhibitCounter&) = delete;

  void acquire() {
    if (++count_ == 1 && probe_) probe_->setKeepAwake(true);
  }
  void release() {
    if (count_ > 0 && --count_ == 0 && probe_) probe_->setKeepAwake(false);
  }
  int count() const { return count_; }

 private:
  DesktopProbe* probe_;
  int count_ = 0;
};

// The counter of the process, over the system probe. Without a live desktop it has no probe.
IdleInhibitCounter& systemIdleInhibitCounter();

}  // namespace waku::desktop_state
