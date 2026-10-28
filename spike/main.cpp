#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>

#include "spike/bar/bar_host.h"
#include "spike/bar/control_handler.h"
#include "spike/control/control.h"

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
      QStringLiteral("Folder with bar.qml (default: $BARNACLE_CONFIG_HOME, else the sample config "
                     "of the repository)."),
      QStringLiteral("dir"));
  QCommandLineOption appBarOpt(QStringLiteral("register-app-bar"),
                               QStringLiteral("Reserve screen work area for bars that ask."));
  QCommandLineOption safeOpt(
      QStringLiteral("safe"),
      QStringLiteral("Start the built-in bar with the default theme and ignore the config home."));
  parser.addOption(configHomeOpt);
  parser.addOption(appBarOpt);
  parser.addOption(safeOpt);
  parser.process(app);

  const QString configHome =
      barnacle::resolveConfigHome(parser.value(configHomeOpt), QStringLiteral(SPIKE_CONFIG_DIR));

  QQmlEngine engine;
  barnacle::BarHost::Options options;
  options.configHome = configHome;
  options.allowReserve = parser.isSet(appBarOpt);
  options.safe = parser.isSet(safeOpt);
  options.hooks = barnacle::BarHost::NativeHooks::win32();
  barnacle::BarHost host(&engine, std::move(options));

  // One bar per config home: `barnaclec` finds it by the same name, and a second start would put
  // a second bar over the first.
  barnacle::ControlServer control(barnacle::controlPipeName(configHome),
                                  barnacle::makeControlHandler(host, parser.isSet(safeOpt)));
  if (!control.listen()) {
    qCritical().noquote() << "barnacle is already running for" << configHome
                          << "(barnaclec quit stops it)";
    return 1;
  }

  host.reload();
  host.watch();
  return app.exec();
}
