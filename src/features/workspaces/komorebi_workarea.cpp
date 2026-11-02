#include "features/workspaces/komorebi_workarea.h"

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
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
    settle_.setSingleShot(true);
    late_.setSingleShot(true);
    QObject::connect(&settle_, &QTimer::timeout, [this] { fire(settleFire_); });
    QObject::connect(&late_, &QTimer::timeout, [this] { fire(lateFire_); });
  }
  void start(Slot slot, int ms, std::function<void()> fire) override {
    (slot == Slot::Settle ? settleFire_ : lateFire_) = std::move(fire);
    timer(slot).start(ms);
  }
  void stop(Slot slot) override { timer(slot).stop(); }
  bool active(Slot slot) const override {
    return (slot == Slot::Settle ? settle_ : late_).isActive();
  }

 private:
  QTimer& timer(Slot slot) { return slot == Slot::Settle ? settle_ : late_; }
  static void fire(const std::function<void()>& callback) {
    if (callback) callback();
  }

  QTimer settle_;
  QTimer late_;
  std::function<void()> settleFire_;
  std::function<void()> lateFire_;
};

}  // namespace

std::unique_ptr<WorkAreaTimers> makeQtWorkAreaTimers() {
  return std::make_unique<QtWorkAreaTimers>();
}

KomorebiWorkAreaGuard::KomorebiWorkAreaGuard(std::unique_ptr<WorkAreaNotifier> notifier,
                                             std::unique_ptr<WorkAreaTimers> timers, Timing timing,
                                             QObject* parent)
    : QObject(parent),
      notifier_(std::move(notifier)),
      timers_(timers ? std::move(timers) : makeQtWorkAreaTimers()),
      timing_(timing) {
  if (!notifier_) return;
  auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
  if (!app) return;
  connect(app, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
    watchScreen(screen);
    screensChanged();
  });
  connect(app, &QGuiApplication::screenRemoved, this, [this] { screensChanged(); });
  connect(app, &QGuiApplication::primaryScreenChanged, this, [this] { screensChanged(); });
  for (QScreen* screen : QGuiApplication::screens()) watchScreen(screen);
}

KomorebiWorkAreaGuard::~KomorebiWorkAreaGuard() {
  timers_->stop(WorkAreaTimers::Slot::Settle);
  timers_->stop(WorkAreaTimers::Slot::Late);
}

void KomorebiWorkAreaGuard::watchScreen(QScreen* screen) {
  if (!screen) return;
  connect(screen, &QScreen::availableGeometryChanged, this, [this] { screensChanged(); });
  connect(screen, &QScreen::geometryChanged, this, [this] { screensChanged(); });
  connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { screensChanged(); });
}

void KomorebiWorkAreaGuard::screensChanged() {
  if (!notifier_) return;
  // A new burst re-arms the pair: the late notification of the last one is superseded.
  timers_->stop(WorkAreaTimers::Slot::Late);
  timers_->start(WorkAreaTimers::Slot::Settle, timing_.settleMs, [this] { nudge(); });
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
  return std::make_unique<KomorebiWorkAreaGuard>(std::move(notifier), nullptr,
                                                 KomorebiWorkAreaGuard::Timing{}, parent);
}

}  // namespace waku::workspaces::komorebi
