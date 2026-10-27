#include "app/app_services.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QMetaObject>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <utility>

#include "app/log.h"
#include "app/tray_icon.h"
#include "core/bar/bar_window.h"
#include "core/config/loader.h"
#include "core/config/paths.h"

namespace yasb::app {
namespace {

constexpr int kScreenChangeDebounceMs = 250;

}  // namespace

AppServices::AppServices(nlohmann::json config, QObject* parent)
    : QObject(parent), config_(std::move(config)), instance_(L"yasb_reborn") {
  screenRestartTimer_.setSingleShot(true);
  screenRestartTimer_.setInterval(kScreenChangeDebounceMs);
  connect(&screenRestartTimer_, &QTimer::timeout, this,
          [this] { requestRestart(screenRestartReason_); });
}

AppServices::~AppServices() { stop(); }

bool AppServices::start(bool restartWait) {
  if (started_) return true;
  const bool debug = config_.is_object() && config_.value("debug", false);
  const QString logPath =
      QString::fromStdWString((yasb::config::configDir() / L"yasb-native.log").wstring());
  if (!initializeLog(logPath, debug)) {
    error_ = "Could not open application log: " + logPath.toStdString();
    return false;
  }
  if (!instance_.acquire(restartWait)) {
    error_ = instance_.error();
    qCritical().noquote() << "Process" << QCoreApplication::applicationPid()
                          << "not started:" << QString::fromStdString(error_);
    return false;
  }

  watcher_ = std::make_unique<FileWatcher>(
      QString::fromStdWString(yasb::config::configPath().wstring()),
      QString::fromStdWString(yasb::config::stylesheetPath().wstring()),
      [] { return yasb::config::loadConfigFile(yasb::config::configPath()).ok(); },
      [this] { requestRestart(QStringLiteral("Config changed")); },
      [this] { emit stylesheetChanged(); }, 250, config_.value("watch_config", true),
      config_.value("watch_stylesheet", true));
  if (auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    connect(guiApp, &QGuiApplication::screenAdded, this,
            [this](QScreen*) { scheduleScreenRestart(QStringLiteral("Screen added")); });
    connect(guiApp, &QGuiApplication::screenRemoved, this,
            [this](QScreen*) { scheduleScreenRestart(QStringLiteral("Screen removed")); });
  }

  tray_ = new TrayIcon(this);
  connect(tray_, &TrayIcon::reloadRequested, this,
          [this] { requestRestart(QStringLiteral("Reload requested")); });
  connect(tray_, &TrayIcon::exitRequested, this, &AppServices::exitRequested);
  connect(tray_, &TrayIcon::barActionRequested, this,
          [this](const QString& action) { applyBarCommand(action, QString()); });

  cli_ = std::make_unique<CliServer>();
  cli_->start([this](const QString& command) {
    QMetaObject::invokeMethod(
        this, [this, command] { handleCommand(command); }, Qt::QueuedConnection);
  });
  hotkeys_ = std::make_unique<HotkeyService>(collectHotkeys(config_));
  hotkeys_->start();
  started_ = true;
  qInfo() << "Application services started";
  return true;
}

void AppServices::addBar(yasb::bar::BarWindow* bar) {
  if (!bar) return;
  bars_.push_back(QPointer<yasb::bar::BarWindow>(bar));
  connect(bar, &yasb::bar::BarWindow::restartRequested, this, &AppServices::requestRestart);
}

void AppServices::stop() {
  screenRestartTimer_.stop();
  if (cli_) cli_->stop();
  if (hotkeys_) hotkeys_->stop();
  if (watcher_) watcher_.reset();
  if (tray_) tray_->hide();
  started_ = false;
}

void AppServices::handleCommand(const QString& command) {
  const QString trimmed = command.trimmed();
  const QString name = trimmed.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).toLower();
  if (name == QLatin1String("reload")) {
    requestRestart(QStringLiteral("Reload requested from CLI"));
  } else if (name == QLatin1String("stop")) {
    emit exitRequested();
  } else if (name == QLatin1String("show-bar") || name == QLatin1String("hide-bar") ||
             name == QLatin1String("toggle-bar")) {
    QString screen;
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("(?:--screen|-s)\\s+(.+)$")).match(trimmed);
    if (match.hasMatch()) screen = match.captured(1).trimmed();
    const QString action = name.section(QLatin1Char('-'), 0, 0);
    applyBarCommand(action, screen);
  }
}

void AppServices::scheduleScreenRestart(const QString& reason) {
  if (restarting_) return;
  if (screenRestartTimer_.isActive()) qInfo().noquote() << reason << "(restart pending, debounced)";
  screenRestartReason_ = reason;
  screenRestartTimer_.start();
}

void AppServices::requestRestart(const QString& reason) {
  if (restarting_) return;
  restarting_ = true;
  qInfo().noquote() << reason;
  QStringList args = QCoreApplication::arguments();
  if (!args.isEmpty()) args.removeFirst();
  if (!args.contains(QStringLiteral("--restart-wait")))
    args.append(QStringLiteral("--restart-wait"));
  qInfo() << "Starting replacement process with restart wait";
  QProcess replacement;
  replacement.setProgram(QCoreApplication::applicationFilePath());
  replacement.setArguments(args);
  replacement.setWorkingDirectory(QCoreApplication::applicationDirPath());
  qint64 pid = 0;
  if (!replacement.startDetached(&pid)) {
    restarting_ = false;
    qCritical().noquote() << "Could not start replacement application" << replacement.program()
                          << ":" << replacement.errorString();
    return;
  }
  // The replacement logs to the same file; if it cannot take over (the single-instance wait times
  // out) it logs "Process <pid> not started".
  qInfo() << "Replacement process started, pid" << pid;
  emit restartRequested(reason);
  QCoreApplication::quit();
}

void AppServices::applyBarCommand(const QString& action, const QString& screen) {
  for (const auto& bar : bars_) {
    if (!bar) continue;
    if (!screen.isEmpty() && bar->screenName() != screen && !bar->screenName().contains(screen))
      continue;
    if (action == QLatin1String("show")) {
      bar->show();
    } else if (action == QLatin1String("hide")) {
      bar->hide();
    } else if (bar->isVisible()) {
      bar->hide();
    } else {
      bar->show();
    }
  }
}

}  // namespace yasb::app
