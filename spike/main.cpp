#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>

#include "control/control.h"
#include "features/wallpapers/qml/wallpapers_service.h"
#include "kernel/qt/live_desktop.h"
#include "spike/bar/bar_host.h"
#include "spike/bar/control_handler.h"

int main(int argc, char* argv[]) {
  // As app/main.cpp setFontEngine(): Qt's DirectWrite font database maps every installed font
  // (about 1.7k handles and 100 MiB working set). Respect a platform the caller chose.
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "windows:fontengine=gdi");
  }
  // Config is edited by hand and reloaded: a cached compilation could outlive an edit made in the
  // same instant, and nothing here is slow enough to need the cache.
  qputenv("QML_DISABLE_DISK_CACHE", "1");
  // A transparent, blurred window needs an alpha swap chain.
  QQuickWindow::setDefaultAlphaBuffer(true);
  QGuiApplication app(argc, argv);

  QCommandLineParser parser;
  parser.addHelpOption();
  QCommandLineOption configHomeOpt(
      QStringLiteral("config-home"),
      QStringLiteral("Folder with bar.qml (default: $WABAR_CONFIG_HOME, else the sample config "
                     "of the repository)."),
      QStringLiteral("dir"));
  QCommandLineOption appBarOpt(QStringLiteral("register-app-bar"),
                               QStringLiteral("Reserve screen work area for bars that ask."));
  QCommandLineOption safeOpt(
      QStringLiteral("safe"),
      QStringLiteral("Start the built-in bar with the default theme and ignore the config home."));
  QCommandLineOption noTrayOpt(QStringLiteral("no-tray"),
                               QStringLiteral("Never hook Explorer; the Tray widget stays empty."));
  parser.addOption(configHomeOpt);
  parser.addOption(noTrayOpt);
  parser.addOption(appBarOpt);
  parser.addOption(safeOpt);
  parser.process(app);
  wabar::kernel::setCapabilityDisabled(QStringLiteral("tray"), parser.isSet(noTrayOpt));

  const QString configHome =
      wabar::resolveConfigHome(parser.value(configHomeOpt), QStringLiteral(SPIKE_CONFIG_DIR));

  QQmlEngine engine;
  wabar::BarHost::Options options;
  options.configHome = configHome;
  options.allowReserve = parser.isSet(appBarOpt);
  options.safe = parser.isSet(safeOpt);
  options.hooks = wabar::BarHost::NativeHooks::win32();
  wabar::BarHost host(&engine, std::move(options));

  // One bar per config home: `wabarc` finds it by the same name, and a second start would put
  // a second bar over the first.
  wabar::ControlServer control(
      wabar::controlPipeName(configHome),
      wabar::makeControlHandler(host, parser.isSet(safeOpt), {},
                                // Built on the first wallpaper command if no config did.
                                [&engine]() -> wabar::WallpaperControl* {
                                  return engine.singletonInstance<wabar::wallpapers::Wallpapers*>(
                                      QStringLiteral("wabar.services"),
                                      QStringLiteral("Wallpapers"));
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
