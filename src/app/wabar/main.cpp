#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>

#include "control/control.h"
#include "features/wallpapers/qml/wallpapers_service.h"
#include "host/qml/control_handler.h"
#include "host/qml/shell_host.h"
#include "kernel/qt/live_desktop.h"

int main(int argc, char* argv[]) {
  // As app/main.cpp setFontEngine(): Qt's DirectWrite font database maps every installed font
  // (about 1.7k handles and 100 MiB working set). Respect a platform the caller chose.
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "windows:fontengine=gdi");
  }
  // The QML disk cache stays on, so a large user shell does not recompile on every start. Qt keys
  // an entry on the source file's size and modification time, so an edit invalidates it. A caller
  // that wants none sets QML_DISABLE_DISK_CACHE itself.

  // A transparent, blurred window needs an alpha swap chain.
  QQuickWindow::setDefaultAlphaBuffer(true);
  QGuiApplication app(argc, argv);

  QCommandLineParser parser;
  parser.addHelpOption();
  QCommandLineOption configHomeOpt(
      QStringLiteral("config-home"),
      QStringLiteral("Folder with shell.qml (default: $WABAR_CONFIG_HOME, else the sample config "
                     "of the repository)."),
      QStringLiteral("dir"));
  QCommandLineOption appBarOpt(QStringLiteral("register-app-bar"),
                               QStringLiteral("Reserve screen work area for windows that ask."));
  QCommandLineOption safeOpt(
      QStringLiteral("safe"),
      QStringLiteral(
          "Start the built-in shell with the default theme and ignore the config home."));
  QCommandLineOption noTrayOpt(QStringLiteral("no-tray"),
                               QStringLiteral("Never hook Explorer; the Tray widget stays empty."));
  parser.addOption(configHomeOpt);
  parser.addOption(noTrayOpt);
  parser.addOption(appBarOpt);
  parser.addOption(safeOpt);
  parser.process(app);
  wabar::kernel::setCapabilityDisabled(QStringLiteral("tray"), parser.isSet(noTrayOpt));

  const QString configHome = wabar::control::resolveConfigHome(
      parser.value(configHomeOpt), QStringLiteral(WABAR_SAMPLE_CONFIG_DIR));

  QQmlEngine engine;
  wabar::host::ShellHost::Options options;
  options.configHome = configHome;
  options.allowReserve = parser.isSet(appBarOpt);
  options.safe = parser.isSet(safeOpt);
  options.hooks = wabar::host::ShellHost::NativeHooks::win32();
  wabar::host::ShellHost host(&engine, std::move(options));

  // One shell per config home: `wabarc` finds it by the same name, and a second start would put
  // a second shell over the first.
  wabar::control::ControlServer control(
      wabar::control::controlPipeName(configHome),
      wabar::host::makeControlHandler(
          host, parser.isSet(safeOpt), {},
          // Built on the first wallpaper command if no config did.
          [&engine]() -> wabar::control::WallpaperControl* {
            return engine.singletonInstance<wabar::wallpapers::Wallpapers*>(
                QStringLiteral("wabar.services"), QStringLiteral("Wallpapers"));
          }));
  if (!control.listen()) {
    qCritical().noquote() << "wabar is already running for" << configHome
                          << "(wabarc quit stops it)";
    return 1;
  }

  host.reload();
  host.watch();
  return app.exec();
}
