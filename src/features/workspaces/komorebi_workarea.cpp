#include "features/workspaces/komorebi_workarea.h"

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <array>
#include <cstddef>
#include <exception>
#include <utility>

#include "kernel/qt/live_desktop.h"

namespace waku::workspaces::komorebi {
namespace {

// ERROR_ACCESS_DENIED: UIPI refuses a message to a process of a higher integrity level.
constexpr unsigned long kAccessDenied = 5;

class QtWorkAreaTimers final : public WorkAreaTimers {
 public:
  QtWorkAreaTimers() {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
      slots_[i].timer.setSingleShot(true);
      QObject::connect(&slots_[i].timer, &QTimer::timeout, [this, i] {
        if (slots_[i].fire) slots_[i].fire();
      });
    }
  }
  void start(Slot slot, int ms, std::function<void()> fire) override {
    Entry& entry = slots_[index(slot)];
    entry.fire = std::move(fire);
    entry.timer.start(ms);
  }
  void stop(Slot slot) override { slots_[index(slot)].timer.stop(); }
  bool active(Slot slot) const override { return slots_[index(slot)].timer.isActive(); }

 private:
  struct Entry {
    QTimer timer;
    std::function<void()> fire;
  };
  static std::size_t index(Slot slot) { return static_cast<std::size_t>(slot); }

  std::array<Entry, 3> slots_;
};

}  // namespace

std::unique_ptr<WorkAreaTimers> makeQtWorkAreaTimers() {
  return std::make_unique<QtWorkAreaTimers>();
}

KomorebiWorkAreaGuard::KomorebiWorkAreaGuard(std::unique_ptr<WorkAreaNotifier> notifier,
                                             std::unique_ptr<WorkAreaTimers> timers, Timing timing,
                                             std::unique_ptr<KomorebiManager> manager,
                                             QObject* parent)
    : QObject(parent),
      notifier_(std::move(notifier)),
      timers_(timers ? std::move(timers) : makeQtWorkAreaTimers()),
      timing_(timing),
      manager_(std::move(manager)) {
  if (!notifier_ && !manager_) return;
  auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
  if (!app) return;
  connect(app, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
    watchScreen(screen);
    displayChanged();
  });
  connect(app, &QGuiApplication::screenRemoved, this, [this] { displayChanged(); });
  connect(app, &QGuiApplication::primaryScreenChanged, this, [this] { displayChanged(); });
  for (QScreen* screen : QGuiApplication::screens()) watchScreen(screen);
}

KomorebiWorkAreaGuard::~KomorebiWorkAreaGuard() {
  timers_->stop(WorkAreaTimers::Slot::Settle);
  timers_->stop(WorkAreaTimers::Slot::Late);
}

void KomorebiWorkAreaGuard::watchScreen(QScreen* screen) {
  if (!screen) return;
  connect(screen, &QScreen::availableGeometryChanged, this, [this] { screensChanged(); });
  connect(screen, &QScreen::geometryChanged, this, [this] { displayChanged(); });
  connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { displayChanged(); });
}

void KomorebiWorkAreaGuard::displayChanged() {
  if (manager_) pendingManage_ = true;
  screensChanged();
}

void KomorebiWorkAreaGuard::screensChanged() {
  if (!notifier_ && !manager_) return;
  // A new burst re-arms the pair: the late notification of the last one is superseded.
  timers_->stop(WorkAreaTimers::Slot::Late);
  timers_->start(WorkAreaTimers::Slot::Settle, timing_.settleMs, [this] { settled(); });
}

void KomorebiWorkAreaGuard::settled() {
  if (std::exchange(pendingManage_, false)) manage();
  nudge();
}

void KomorebiWorkAreaGuard::manage() {
  // Runs from a timer: nothing may escape into Qt.
  if (!manager_) return;
  try {
    if (timers_->active(WorkAreaTimers::Slot::Cooldown)) {
      qDebug().noquote() << "komorebi manage: skipped, a reload ran a moment ago";
      return;
    }
    const KomorebiManageResult result = manager_->reloadConfiguration();
    if (!result.barRunning) {
      if (std::exchange(lastBarRunning_, false)) {
        qDebug().noquote() << "komorebi manage: komorebi or komorebi-bar does not run, "
                              "nothing reloaded";
      }
      return;
    }
    lastBarRunning_ = true;
    if (!result.started) {
      qWarning().noquote() << "komorebi manage: could not start komorebic";
      return;
    }
    qInfo().noquote() << "komorebi manage: komorebic replace-configuration started";
    timers_->start(WorkAreaTimers::Slot::Cooldown, timing_.manageCooldownMs, [] {});
  } catch (const std::exception& error) {
    qWarning().noquote() << "komorebi manage failed:" << error.what();
  } catch (...) {
    qWarning().noquote() << "komorebi manage failed";
  }
}

void KomorebiWorkAreaGuard::nudge() {
  if (!notifier_) return;
  timers_->stop(WorkAreaTimers::Slot::Settle);
  notifyOnce();
  timers_->start(WorkAreaTimers::Slot::Late, timing_.lateMs, [this] { notifyOnce(); });
}

void KomorebiWorkAreaGuard::notifyOnce() {
  // Runs from a timer: nothing may escape into Qt.
  try {
    const WorkAreaNotifyResult result = notifier_->notify();
    if (!result.windowFound) {
      if (std::exchange(lastFound_, false)) {
        qDebug().noquote() << "komorebi work area: no komorebi window, nothing sent";
      }
      return;
    }
    lastFound_ = true;
    if (result.sent) {
      qInfo().noquote()
          << QStringLiteral("komorebi work area nudge sent to 0x%1").arg(result.hwnd, 0, 16);
      return;
    }
    if (std::exchange(refusalLogged_, true)) return;
    qWarning().noquote() << QStringLiteral("komorebi work area nudge refused (error %1)%2")
                                .arg(result.error)
                                .arg(result.error == kAccessDenied
                                         ? QStringLiteral(": komorebi runs elevated, waku not")
                                         : QString());
  } catch (const std::exception& error) {
    qWarning().noquote() << "komorebi work area nudge failed:" << error.what();
  } catch (...) {
    qWarning().noquote() << "komorebi work area nudge failed";
  }
}

std::unique_ptr<KomorebiWorkAreaGuard> makeKomorebiWorkAreaGuard(QObject* parent) {
  std::unique_ptr<WorkAreaNotifier> notifier;
  if (kernel::liveDesktop(QStringLiteral("workarea_sync"))) notifier = makeWin32WorkAreaNotifier();
  std::unique_ptr<KomorebiManager> manager;
  if (kernel::liveDesktop(QStringLiteral("manage_komorebi"))) manager = makeWin32KomorebiManager();
  return std::make_unique<KomorebiWorkAreaGuard>(
      std::move(notifier), nullptr, KomorebiWorkAreaGuard::Timing{}, std::move(manager), parent);
}

}  // namespace waku::workspaces::komorebi
