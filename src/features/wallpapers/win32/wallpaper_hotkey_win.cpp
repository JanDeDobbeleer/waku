#include "features/wallpapers/win32/wallpaper_hotkey_win.h"

#include <windows.h>

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDebug>
#include <exception>
#include <utility>

namespace wabar::wallpapers {
namespace {

// RegisterHotKey (documented) with a null window: WM_HOTKEY is posted to the registering thread's
// queue, which is the GUI thread, and Qt's message loop hands every message to native event
// filters. Owner: the Wallpapers singleton; removed in unregisterKey() and the destructor.
class RealHotkeyHost final : public HotkeyHost, public QAbstractNativeEventFilter {
 public:
  ~RealHotkeyHost() override { unregisterKey(); }

  bool registerKey(const HotkeyCombo& combo, std::function<void()> pressed) override {
    unregisterKey();
    constexpr unsigned kNoRepeat = 0x4000;  // MOD_NOREPEAT
    if (!RegisterHotKey(nullptr, kId, combo.modifiers | kNoRepeat, combo.virtualKey)) return false;
    pressed_ = std::move(pressed);
    registered_ = true;
    if (QCoreApplication::instance()) QCoreApplication::instance()->installNativeEventFilter(this);
    return true;
  }

  void unregisterKey() override {
    if (!registered_) return;
    UnregisterHotKey(nullptr, kId);
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
    registered_ = false;
    pressed_ = nullptr;
  }

  bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override {
    if (!registered_ || eventType != "windows_generic_MSG" || !message) return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_HOTKEY || msg->wParam != kId) return false;
    try {
      if (pressed_) pressed_();
    } catch (const std::exception& error) {
      qWarning().noquote() << "Wallpapers: hotkey failed:" << error.what();
    } catch (...) {
      qWarning().noquote() << "Wallpapers: hotkey failed";
    }
    return true;
  }

 private:
  static constexpr int kId = 0x7A11;
  bool registered_ = false;
  std::function<void()> pressed_;
};

}  // namespace

std::unique_ptr<HotkeyHost> makeWin32HotkeyHost() { return std::make_unique<RealHotkeyHost>(); }

}  // namespace wabar::wallpapers
