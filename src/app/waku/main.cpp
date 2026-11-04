#include <Windows.h>
#include <combaseapi.h>

#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>
#include <memory>

#include "app/waku/font_engine.h"
#include "app/waku/quickshell_glue.h"
#include "control/control.h"
#include "features/wallpapers/qml/wallpapers_service.h"
#include "features/wallpapers/wallpaper_command.h"
#include "features/workspaces/komorebi_workarea.h"
#include "host/plugin_guard.h"
#include "host/qml/control_handler.h"
#include "host/qml/shell_host.h"
#include "host/run_log.h"
#include "host/startup_trace.h"
#include "host/windows_taskbar.h"
#include "kernel/qt/app_data.h"
#include "kernel/qt/live_desktop.h"

int main(int argc, char* argv[]) {
  waku::host::startTrace();
  // Keeps the process MTA alive for the whole run. Icon and shortcut reads on pool threads do
  // CoInitializeEx(MTA) ... CoUninitialize() per call; without this the last one out tears the MTA
  // down, unloading the in-process shell extensions and codecs it loaded, possibly while another
  // pool thread is inside one. Never released: the process exit ends it.
  CO_MTA_USAGE_COOKIE mtaUsage = nullptr;
  if (FAILED(CoIncrementMTAUsage(&mtaUsage))) {
    qWarning() << "CoIncrementMTAUsage failed; COM may unload shell extensions between icon reads";
  }

  // As app/main.cpp setFontEngine(): Qt's DirectWrite font database maps every installed font
  // (20-120 MiB more working set and 1,000-1,700 more handles). Respect a platform the caller
  // chose.
  // `--font-engine=directwrite` opts back in for a shell that needs variable-font axes, which the
  // GDI engine ignores (app/waku/font_engine.h).
  QStringList rawArguments;
  for (int i = 1; i < argc; ++i) rawArguments << QString::fromLocal8Bit(argv[i]);
  const waku::app::FontEngineChoice fontEngine = waku::app::parseFontEngine(rawArguments);
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", waku::app::platformSpec(fontEngine.engine).toLocal8Bit());
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
  // waku with QSG_NO_DEPTH_BUFFER=0.
  if (!qEnvironmentVariableIsSet("QSG_NO_DEPTH_BUFFER")) qputenv("QSG_NO_DEPTH_BUFFER", "1");
  // No texture atlas: every image gets its own texture. Qt packs small images into one atlas
  // texture per window (4096x2048, 32 MB, at 2880x1920) and uploads each new image into it. After
  // a burst of such uploads (the caelestia launcher's app icons) the D3D11 driver on the Adreno X1
  // holds 9-11 more atlas-sized allocations, about 290 MB of GPU memory, for the life of the
  // process (docs/reviews/2026-10-31-memory-profile.md, "Atlas copies"). A caller that wants the
  // atlas sets QSG_ATLAS_SIZE_LIMIT itself.
  if (!qEnvironmentVariableIsSet("QSG_ATLAS_SIZE_LIMIT")) qputenv("QSG_ATLAS_SIZE_LIMIT", "1");
  QGuiApplication app(argc, argv);
  waku::host::traceStep("application built");

  QCommandLineParser parser;
  parser.addHelpOption();
  QCommandLineOption configHomeOpt(
      QStringLiteral("config-home"),
      QStringLiteral("Folder with shell.qml (default: $WAKU_CONFIG_HOME, else the sample config "
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
      QStringLiteral("gdi (default, small) or directwrite (honours variable-font axes, tens of MB "
                     "more memory and about 1,000 more handles). QT_QPA_PLATFORM wins when set."),
      QStringLiteral("engine"));
  QCommandLineOption noHotkeysOpt(
      QStringLiteral("no-hotkeys"),
      QStringLiteral("Register no global hotkey; GlobalShortcut fires only through wakuc."));
  QCommandLineOption importPathOpt(
      QStringLiteral("import-path"),
      QStringLiteral("Add a folder of QML modules (user-built plugins) to the import path. May be "
                     "repeated. <config home>/modules is always added when it exists."),
      QStringLiteral("dir"));
  QCommandLineOption hideTaskbarOpt(
      QStringLiteral("hide-taskbar"),
      QStringLiteral("Hide the Windows taskbar while waku runs (auto-hide on, taskbar windows "
                     "hidden) and restore it on exit. Off by default."));
  QCommandLineOption noWorkAreaSyncOpt(
      QStringLiteral("no-workarea-sync"),
      QStringLiteral(
          "Never nudge a tiling window manager (komorebi) after the work area changed."));
  QCommandLineOption manageKomorebiOpt(
      QStringLiteral("manage-komorebi"),
      QStringLiteral("After a screen or scale change, run `komorebic replace-configuration` so "
                     "komorebi and komorebi-bar pick it up. Only when both run. Off by default."));
  QCommandLineOption logFileOpt(
      QStringLiteral("log-file"),
      QStringLiteral("Write a diagnostics log (JSON Lines) to <file>: every log message, the start "
                     "and stop of the run, screen and scale changes and shell reloads. Off by "
                     "default. See docs/logging.md."),
      QStringLiteral("file"));
  parser.addOption(configHomeOpt);
  parser.addOption(importPathOpt);
  parser.addOption(logFileOpt);
  parser.addOption(noWorkAreaSyncOpt);
  parser.addOption(manageKomorebiOpt);
  parser.addOption(hideTaskbarOpt);
  parser.addOption(noHotkeysOpt);
  parser.addOption(fontEngineOpt);
  parser.addOption(noTrayOpt);
  parser.addOption(appBarOpt);
  parser.addOption(safeOpt);
  parser.process(app);
  if (!fontEngine.error.isEmpty()) qWarning().noquote() << "waku:" << fontEngine.error;
  waku::kernel::setCapabilityDisabled(QStringLiteral("tray"), parser.isSet(noTrayOpt));
  waku::kernel::setCapabilityDisabled(QStringLiteral("hotkeys"), parser.isSet(noHotkeysOpt));
  waku::kernel::setCapabilityDisabled(QStringLiteral("workarea_sync"),
                                      parser.isSet(noWorkAreaSyncOpt));
  waku::kernel::setCapabilityDisabled(QStringLiteral("manage_komorebi"),
                                      !parser.isSet(manageKomorebiOpt));

  const waku::control::ConfigHome resolvedHome =
      waku::control::resolveConfigHome(parser.value(configHomeOpt), QDir::homePath());
  const QString configHome = resolvedHome.path;
  if (parser.isSet(logFileOpt)) {
    waku::host::RunLogOptions logOptions;
    logOptions.path = QDir::current().absoluteFilePath(parser.value(logFileOpt));
    if (waku::host::RunLog::start(logOptions, rawArguments, configHome)) {
      waku::host::RunLog::watchScreens();
    }
  }

  QQmlEngine engine;
  // User-built QML plugins. A folder whose plugin was built for another Qt, flavour or processor
  // is refused, with one log line, before the engine can load anything from it. --safe loads
  // none: it ignores the config home, and recovery must not depend on a plugin.
  if (!parser.isSet(safeOpt)) {
    for (const QString& root : waku::host::usableImportRoots(
             waku::host::importRoots(configHome, parser.values(importPathOpt)))) {
      engine.addImportPath(root);
    }
  }
  waku::host::traceStep("engine built, import roots checked");
  waku::host::ShellHost::Options options;
  options.configHome = configHome;
  options.implicitConfigHome = resolvedHome.implicit;
  options.allowReserve = parser.isSet(appBarOpt);
  options.safe = parser.isSet(safeOpt);
  options.hooks = waku::host::ShellHost::NativeHooks::win32();
  waku::host::ShellHost host(&engine, std::move(options));
  waku::app::connectQuickshell(&engine, &host, configHome);
  waku::host::traceStep("host built");

  // The Windows taskbar: inert off the live desktop. A stale marker of a crashed run is undone
  // first; with --hide-taskbar the taskbar is hidden before the first reload, so a bar that
  // reserves work area computes it without the taskbar. Restored on aboutToQuit and, at the
  // latest, when `taskbar` is destroyed. The hide waits for the control pipe (below), so a
  // second start that is refused does not touch the taskbar.
  std::unique_ptr<waku::host::TaskbarSystem> taskbarSystem;
  std::unique_ptr<waku::host::TaskbarMarkerStore> taskbarMarkers;
  std::unique_ptr<waku::host::WindowsTaskbarHider> taskbar;
  if (waku::kernel::liveDesktop()) {
    taskbarSystem = waku::host::makeWin32TaskbarSystem();
    taskbarMarkers = waku::host::makeFileMarkerStore(
        QDir(waku::kernel::appDataDir()).filePath(QStringLiteral("taskbar_hidden.marker")));
    taskbar = std::make_unique<waku::host::WindowsTaskbarHider>(*taskbarSystem, *taskbarMarkers,
                                                                QCoreApplication::applicationPid());
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                     [&taskbar] { taskbar->restore(); });
  }

  // One shell per config home: `wakuc` finds it by the same name, and a second start would put
  // a second shell over the first.
  waku::control::ControlServer control(
      waku::control::controlPipeName(configHome),
      waku::host::makeAsyncControlHandler(waku::host::makeControlHandler(
          host, parser.isSet(safeOpt), {},
          {{QStringLiteral("wallpaper"),
            [&engine](const QStringList& words) {
              // Built on the first wallpaper command if no config did.
              return waku::wallpapers::answerWallpaperCommand(
                  words, engine.singletonInstance<waku::wallpapers::Wallpapers*>(
                             QStringLiteral("waku.services"), QStringLiteral("Wallpapers")));
            }}},
          taskbar.get())));
  if (!control.listen()) {
    qCritical().noquote() << "waku is already running for" << configHome << "(wakuc quit stops it)";
    return 1;
  }

  if (taskbar) taskbar->start(parser.isSet(hideTaskbarOpt));
  // komorebi's work area follows the OS after every screen change, the first registration
  // included. Lives for the run, so a reload keeps a pending late notification. Inert off the
  // live desktop and with --no-workarea-sync (features/workspaces/komorebi_workarea.h).
  const auto workAreaGuard = waku::workspaces::komorebi::makeKomorebiWorkAreaGuard();
  QObject::connect(&host, &waku::host::ShellHost::reloaded, &host, [&host](bool ok) {
    waku::host::RunLog::event(
        QStringLiteral("shell.reload"),
        {{QStringLiteral("ok"), ok}, {QStringLiteral("error"), host.error()}});
  });
  host.reload();
  waku::host::traceStep("first reload done");
  host.watch();
  waku::host::traceStep("watch done");
  const int exitCode = app.exec();
  waku::host::RunLog::stop(exitCode);
  return exitCode;
}
