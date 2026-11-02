#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>

#include "app/wabar/font_engine.h"
#include "app/wabar/quickshell_glue.h"
#include "control/control.h"
#include "features/wallpapers/qml/wallpapers_service.h"
#include "host/plugin_guard.h"
#include "host/qml/control_handler.h"
#include "host/qml/shell_host.h"
#include "kernel/qt/live_desktop.h"

int main(int argc, char* argv[]) {
  // As app/main.cpp setFontEngine(): Qt's DirectWrite font database maps every installed font
  // (about 1.7k handles and 100 MiB working set). Respect a platform the caller chose.
  // `--font-engine=directwrite` opts back in for a shell that needs variable-font axes, which the
  // GDI engine ignores (app/wabar/font_engine.h).
  QStringList rawArguments;
  for (int i = 1; i < argc; ++i) rawArguments << QString::fromLocal8Bit(argv[i]);
  const wabar::app::FontEngineChoice fontEngine = wabar::app::parseFontEngine(rawArguments);
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", wabar::app::platformSpec(fontEngine.engine).toLocal8Bit());
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
  // Read from the raw arguments before the application exists; declared here for --help and so
  // the parser accepts it.
  QCommandLineOption fontEngineOpt(
      QStringLiteral("font-engine"),
      QStringLiteral("gdi (default, small) or directwrite (honours variable-font axes, about 2 MB "
                     "more memory). QT_QPA_PLATFORM wins when set."),
      QStringLiteral("engine"));
  QCommandLineOption noHotkeysOpt(
      QStringLiteral("no-hotkeys"),
      QStringLiteral("Register no global hotkey; GlobalShortcut fires only through wabarc."));
  QCommandLineOption importPathOpt(
      QStringLiteral("import-path"),
      QStringLiteral("Add a folder of QML modules (user-built plugins) to the import path. May be "
                     "repeated. <config home>/modules is always added when it exists."),
      QStringLiteral("dir"));
  parser.addOption(configHomeOpt);
  parser.addOption(importPathOpt);
  parser.addOption(noHotkeysOpt);
  parser.addOption(fontEngineOpt);
  parser.addOption(noTrayOpt);
  parser.addOption(appBarOpt);
  parser.addOption(safeOpt);
  parser.process(app);
  if (!fontEngine.error.isEmpty()) qWarning().noquote() << "wabar:" << fontEngine.error;
  wabar::kernel::setCapabilityDisabled(QStringLiteral("tray"), parser.isSet(noTrayOpt));
  wabar::kernel::setCapabilityDisabled(QStringLiteral("hotkeys"), parser.isSet(noHotkeysOpt));

  const QString configHome = wabar::control::resolveConfigHome(
      parser.value(configHomeOpt), QStringLiteral(WABAR_SAMPLE_CONFIG_DIR));

  QQmlEngine engine;
  // User-built QML plugins. A folder whose plugin was built for another Qt, flavour or processor
  // is refused, with one log line, before the engine can load anything from it. --safe loads
  // none: it ignores the config home, and recovery must not depend on a plugin.
  if (!parser.isSet(safeOpt)) {
    for (const QString& root : wabar::host::usableImportRoots(
             wabar::host::importRoots(configHome, parser.values(importPathOpt)))) {
      engine.addImportPath(root);
    }
  }
  wabar::host::ShellHost::Options options;
  options.configHome = configHome;
  options.allowReserve = parser.isSet(appBarOpt);
  options.safe = parser.isSet(safeOpt);
  options.hooks = wabar::host::ShellHost::NativeHooks::win32();
  wabar::host::ShellHost host(&engine, std::move(options));
  wabar::app::connectQuickshell(&engine, &host, configHome);

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
