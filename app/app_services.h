#pragma once

#include <QObject>
#include <QPointer>
#include <nlohmann/json.hpp>

#include <memory>
#include <vector>

#include "app/cli_server.h"
#include "app/file_watcher.h"
#include "app/hotkeys.h"
#include "app/single_instance.h"

namespace yasb::bar {
class BarWindow;
}

namespace yasb::app {

class TrayIcon;

// Owns process-wide services. Create after QApplication and a successful config load,
// call start(), then register each BarWindow with addBar().
class AppServices : public QObject {
  Q_OBJECT

 public:
  explicit AppServices(nlohmann::json config, QObject* parent = nullptr);
  ~AppServices() override;

  bool start(bool restartWait);
  void addBar(yasb::bar::BarWindow* bar);
  void stop();
  const std::string& error() const { return error_; }

 signals:
  void stylesheetChanged();
  void restartRequested(const QString& reason);
  void exitRequested();

 private:
  void handleCommand(const QString& command);
  void requestRestart(const QString& reason);
  void applyBarCommand(const QString& action, const QString& screen);

  nlohmann::json config_;
  SingleInstance instance_;
  std::unique_ptr<FileWatcher> watcher_;
  std::unique_ptr<HotkeyService> hotkeys_;
  std::unique_ptr<CliServer> cli_;
  TrayIcon* tray_ = nullptr;
  std::vector<QPointer<yasb::bar::BarWindow>> bars_;
  std::string error_;
  bool started_ = false;
  bool restarting_ = false;
};

}  // namespace yasb::app
