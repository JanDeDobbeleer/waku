#include "app/tray_icon.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QMenu>
#include <QStyle>
#include <QUrl>

#include "core/config/paths.h"

namespace yasb::app {

TrayIcon::TrayIcon(QObject* parent) : QSystemTrayIcon(parent) {
  setToolTip(QStringLiteral("YASB Native"));
  setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));

  menu_ = new QMenu;
  QMenu* menu = menu_;
  QAction* openConfig = menu->addAction(QStringLiteral("Open Config"));
  connect(openConfig, &QAction::triggered, this, [] {
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QString::fromStdWString(yasb::config::configPath().wstring())));
  });
  QAction* showBars = menu->addAction(QStringLiteral("Show All Bars"));
  QAction* hide = menu->addAction(QStringLiteral("Hide All Bars"));
  QAction* toggle = menu->addAction(QStringLiteral("Toggle All Bars"));
  connect(showBars, &QAction::triggered, this,
          [this] { emit barActionRequested(QStringLiteral("show")); });
  connect(hide, &QAction::triggered, this,
          [this] { emit barActionRequested(QStringLiteral("hide")); });
  connect(toggle, &QAction::triggered, this,
          [this] { emit barActionRequested(QStringLiteral("toggle")); });
  menu->addSeparator();
  QAction* reload = menu->addAction(QStringLiteral("Reload YASB"));
  connect(reload, &QAction::triggered, this, &TrayIcon::reloadRequested);
  menu->addSeparator();
  QAction* exit = menu->addAction(QStringLiteral("Exit"));
  connect(exit, &QAction::triggered, this, &TrayIcon::exitRequested);
  setContextMenu(menu);
  if (isSystemTrayAvailable()) QSystemTrayIcon::show();
}

TrayIcon::~TrayIcon() {
  setContextMenu(nullptr);
  delete menu_;
}

}  // namespace yasb::app
