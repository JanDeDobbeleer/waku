#include <Windows.h>
#include <combaseapi.h>

#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>
#include <memory>

#include "app/wabar/font_engine.h"
#include "app/wabar/quickshell_glue.h"
#include "control/control.h"
#include "features/wallpapers/qml/wallpapers_service.h"
#include "features/workspaces/komorebi_workarea.h"
#include "host/plugin_guard.h"
#include "host/qml/control_handler.h"
#include "host/qml/shell_host.h"
#include "host/startup_trace.h"
#include "host/taskbar_control.h"
#include "kernel/qt/app_data.h"
#include "kernel/qt/live_desktop.h"

int main(int argc, char* argv[]) {
  wabar::host::startTrace();
  // Keeps the process MTA alive for the whole run. Icon and shortcut reads on pool threads do
  // CoInitializeEx(MTA) ... CoUninitialize() per call; without this the last one out tears the MTA
  // down, unloading the in-process shell extensions and codecs it loaded, possibly while another
  // pool thread is inside one. Never released: the process exit ends it.
  CO_MTA_USAGE_COOKIE mtaUsage = nullptr;
  if (FAILED(CoIncrementMTAUsage(&mtaUsage))) {
    qWarning() << "CoIncrementMTAUsage failed; COM may unload shell extensions between icon reads";
  }

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
  // Qt Quick draws 2D content as well without a depth buffer: the renderer then draws opaque
  // batches back to front instead of sorting them by depth. Without it every window and every item
  // layer saves a depth buffer at physical resolution: 52 MB with the caelestia port at 2880x1920
  // (docs/reviews/2026-10-31-memory-profile.md). A shell that needs depth (Qt Quick 3D) starts
  // wabar with QSG_NO_DEPTH_BUFFER=0.
  if (!qEnvironmentVariableIsSet("QSG_NO_DEPTH_BUFFER")) qputenv("QSG_NO_DEPTH_BUFFER", "1");
  QGuiApplication app(argc, argv);
  wabar::host::traceStep("application built");

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
  QCommandLineOption hideTaskbarOpt(
      QStringLiteral("hide-taskbar"),
      QStringLiteral("Hide the Windows taskbar while wabar runs (auto-hide on, taskbar windows "
                     "hidden) and restore it on exit. Off by default."));
  QCommandLineOption noWorkAreaSyncOpt(
      QStringLiteral("no-workarea-sync"),
      QStringLiteral(
          "Never nudge a tiling window manager (komorebi) after the work area changed."));
  parser.addOption(configHomeOpt);
  parser.addOption(importPathOpt);
  parser.addOption(noWorkAreaSyncOpt);
  parser.addOption(hideTaskbarOpt);
  parser.addOption(noHotkeysOpt);
  parser.addOption(fontEngineOpt);
  parser.addOption(noTrayOpt);
  parser.addOption(appBarOpt);
  parser.addOption(safeOpt);
  parser.process(app);
  if (!fontEngine.error.isEmpty()) qWarning().noquote() << "wabar:" << fontEngine.error;
  wabar::kernel::setCapabilityDisabled(QStringLiteral("tray"), parser.isSet(noTrayOpt));
  wabar::kernel::setCapabilityDisabled(QStringLiteral("hotkeys"), parser.isSet(noHotkeysOpt));
  wabar::kernel::setCapabilityDisabled(QStringLiteral("workarea_sync"),
                                       parser.isSet(noWorkAreaSyncOpt));

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
  wabar::host::traceStep("engine built, import roots checked");
  wabar::host::ShellHost::Options options;
  options.configHome = configHome;
  options.allowReserve = parser.isSet(appBarOpt);
  options.safe = parser.isSet(safeOpt);
  options.hooks = wabar::host::ShellHost::NativeHooks::win32();
  wabar::host::ShellHost host(&engine, std::move(options));
  wabar::app::connectQuickshell(&engine, &host, configHome);
  wabar::host::traceStep("host built");

  // The Windows taskbar: inert off the live desktop. A stale marker of a crashed run is undone
  // first; with --hide-taskbar the taskbar is hidden before the first reload, so a bar that
  // reserves work area computes it without the taskbar. Restored on aboutToQuit and, at the
  // latest, when `taskbar` is destroyed. The hide waits for the control pipe (below), so a
  // second start that is refused does not touch the taskbar.
  std::unique_ptr<wabar::host::TaskbarSystem> taskbarSystem;
  std::unique_ptr<wabar::host::TaskbarMarkerStore> taskbarMarkers;
  std::unique_ptr<wabar::host::WabarTaskbar> taskbar;
  if (wabar::kernel::liveDesktop()) {
    taskbarSystem = wabar::host::makeWin32TaskbarSystem();
    taskbarMarkers = wabar::host::makeFileMarkerStore(
        QDir(wabar::kernel::appDataDir()).filePath(QStringLiteral("taskbar_hidden.marker")));
    taskbar = std::make_unique<wabar::host::WabarTaskbar>(*taskbarSystem, *taskbarMarkers,
                                                          QCoreApplication::applicationPid());
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                     [&taskbar] { taskbar->restore(); });
  }

  // One shell per config home: `wabarc` finds it by the same name, and a second start would put
  // a second shell over the first.
  wabar::control::ControlServer control(
      wabar::control::controlPipeName(configHome),
      wabar::host::makeAsyncControlHandler(wabar::host::makeControlHandler(
          host, parser.isSet(safeOpt), {},
          // Built on the first wallpaper command if no config did.
          [&engine]() -> wabar::control::WallpaperControl* {
            return engine.singletonInstance<wabar::wallpapers::Wallpapers*>(
                QStringLiteral("wabar.services"), QStringLiteral("Wallpapers"));
          },
          taskbar.get())));
  if (!control.listen()) {
    qCritical().noquote() << "wabar is already running for" << configHome
                          << "(wabarc quit stops it)";
    return 1;
  }

  if (taskbar) taskbar->start(parser.isSet(hideTaskbarOpt));
  // komorebi's work area follows the OS after every screen change, the first registration
  // included. Lives for the run, so a reload keeps a pending late notification. Inert off the
  // live desktop and with --no-workarea-sync (features/workspaces/komorebi_workarea.h).
  const auto workAreaGuard = wabar::workspaces::komorebi::makeKomorebiWorkAreaGuard();
  host.reload();
  wabar::host::traceStep("first reload done");
  host.watch();
  wabar::host::traceStep("watch done");
  return app.exec();
}
