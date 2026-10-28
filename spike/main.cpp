#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>

#include "spike/bar/bar_host.h"

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
  QCommandLineOption configHomeOpt(
      QStringLiteral("config-home"),
      QStringLiteral("Folder with bar.qml (default: the sample config of the repository)."),
      QStringLiteral("dir"), QStringLiteral(SPIKE_CONFIG_DIR));
  QCommandLineOption appBarOpt(QStringLiteral("register-app-bar"),
                               QStringLiteral("Reserve screen work area for bars that ask."));
  parser.addOption(configHomeOpt);
  parser.addOption(appBarOpt);
  parser.process(app);

  QQmlEngine engine;
  barnacle::BarHost::Options options;
  options.configHome = parser.value(configHomeOpt);
  options.allowReserve = parser.isSet(appBarOpt);
  options.hooks = barnacle::BarHost::NativeHooks::win32();
  barnacle::BarHost host(&engine, std::move(options));
  host.reload();
  host.watch();

  return app.exec();
}
