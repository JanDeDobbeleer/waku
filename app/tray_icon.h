#pragma once

#include <QSystemTrayIcon>

class QMenu;

namespace yasb::app {

class TrayIcon : public QSystemTrayIcon {
  Q_OBJECT

 public:
  explicit TrayIcon(QObject* parent = nullptr);
  ~TrayIcon() override;

 private:
  QMenu* menu_ = nullptr;

 signals:
  void reloadRequested();
  void exitRequested();
  void barActionRequested(const QString& action);
};

}  // namespace yasb::app
