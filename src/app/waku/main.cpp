#include <Windows.h>
#include <combaseapi.h>

#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlEngine>
#include <QQuickWindow>
#include <memory>

#include "app/waku/font_engine.h"
#include "app/waku/quickshell_glue.h"
#include "control/control.h"
#include "features/tiling/qml/tiling_service.h"
#include "features/tiling/tiling_command.h"
#include "features/wallpapers/qml/wallpapers_service.h"
#include "features/wallpapers/wallpaper_command.h"
#include "features/workspaces/desktop_command.h"
#include "features/workspaces/qml/virtual_desktops_service.h"
#include "host/plugin_guard.h"
#include "host/qml/control_handler.h"
#include "host/qml/shell_host.h"
#include "host/run_log.h"
#include "host/startup_trace.h"
#include "host/windows_taskbar.h"
#include "kernel/qt/app_data.h"
#include "kernel/qt/live_desktop.h"

namespace {

QString keysExecutable() {
  return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("wakukeys.exe"));
}

// Starts wakukeys.exe with the config home; false when there is nothing to start (no daemon beside
// waku, no keys.conf) or one already runs. The daemon is a single instance per user, so a second
// start would only refuse anyway.
bool startKeys(const QString& configHome) {
  const QString exe = keysExecutable();
  if (!QFileInfo::exists(exe) || !QFileInfo::exists(QDir(configHome).filePath("keys.conf"))) {
    return false;
  }
  QProcess status;
  status.start(exe, {QStringLiteral("status")});
  if (status.waitForFinished(3000) && status.exitCode() == 0) return false;
  return QProcess::startDetached(exe, {QStringLiteral("--config-home"), configHome});
}

void stopKeys() { QProcess::execute(keysExecutable(), {QStringLiteral("stop")}); }

}  // namespace

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
  // layer saves a depth buffer at physical resolution: 52 MB with the celeste port at 2880x1920
  // (docs/reviews/2026-10-31-memory-profile.md). A shell that needs depth (Qt Quick 3D) starts
  // waku with QSG_NO_DEPTH_BUFFER=0.
  if (!qEnvironmentVariableIsSet("QSG_NO_DEPTH_BUFFER")) qputenv("QSG_NO_DEPTH_BUFFER", "1");
  // No texture atlas: every image gets its own texture. Qt packs small images into one atlas
  // texture per window (4096x2048, 32 MB, at 2880x1920) and uploads each new image into it. After
  // a burst of such uploads (the celeste launcher's app icons) the D3D11 driver on the Adreno X1
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
  QCommandLineOption noAppBarOpt(
      QStringLiteral("no-app-bar"),
      QStringLiteral("Never reserve screen work area. By default windows that ask reserve it, on a "
                     "live desktop only; use this for tests and tools."));
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
  QCommandLineOption keepTaskbarOpt(
      QStringLiteral("keep-taskbar"),
      QStringLiteral(
          "Leave the Windows taskbar alone. By default, on a live desktop, waku hides it "
          "(auto-hide on, taskbar windows hidden) while it runs and restores it on exit; "
          "use this for tests and tools."));
  QCommandLineOption noKeysOpt(
      QStringLiteral("no-keys"),
      QStringLiteral("Do not start wakukeys.exe (the key daemon) when the config home has a "
                     "keys.conf."));
  QCommandLineOption noShellDesktopsOpt(
      QStringLiteral("no-shell-desktops"),
      QStringLiteral("Never use the undocumented shell interface behind Task View for virtual "
                     "desktops (close, create without a keystroke, animated switch, "
                     "notifications). The registry and the documented calls remain."));
  QCommandLineOption noTilingOpt(
      QStringLiteral("no-tiling"),
      QStringLiteral("Never start the built-in tiler, even when the shell sets Tiling.enabled."));
  QCommandLineOption watchOpt(
      QStringLiteral("watch"),
      QStringLiteral("Reload the shell when a file in the config home changes. Off by default: "
                     "run `wakuc reload` to apply an edit."));
  QCommandLineOption logFileOpt(
      QStringLiteral("log-file"),
      QStringLiteral("Write a diagnostics log (JSON Lines) to <file>: every log message, the start "
                     "and stop of the run, screen and scale changes and shell reloads. Off by "
                     "default. See docs/logging.md."),
      QStringLiteral("file"));
  parser.addOption(configHomeOpt);
  parser.addOption(importPathOpt);
  parser.addOption(logFileOpt);
  parser.addOption(watchOpt);
  parser.addOption(noShellDesktopsOpt);
  parser.addOption(noTilingOpt);
  parser.addOption(keepTaskbarOpt);
  parser.addOption(noKeysOpt);
  parser.addOption(noHotkeysOpt);
  parser.addOption(fontEngineOpt);
  parser.addOption(noTrayOpt);
  parser.addOption(noAppBarOpt);
  parser.addOption(safeOpt);
  parser.process(app);
  if (!fontEngine.error.isEmpty()) qWarning().noquote() << "waku:" << fontEngine.error;
  waku::kernel::setCapabilityDisabled(QStringLiteral("tray"), parser.isSet(noTrayOpt));
  waku::kernel::setCapabilityDisabled(QStringLiteral("hotkeys"), parser.isSet(noHotkeysOpt));
  waku::kernel::setCapabilityDisabled(QStringLiteral("shell_desktops"),
                                      parser.isSet(noShellDesktopsOpt));
  waku::kernel::setCapabilityDisabled(QStringLiteral("tiling"), parser.isSet(noTilingOpt));

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
  options.allowReserve = !parser.isSet(noAppBarOpt) && waku::kernel::liveDesktop();
  options.safe = parser.isSet(safeOpt);
  options.hooks = waku::host::ShellHost::NativeHooks::win32();
  waku::host::ShellHost host(&engine, std::move(options));
  waku::app::connectQuickshell(&engine, &host, configHome);
  waku::host::traceStep("host built");

  // The Windows taskbar: inert off the live desktop. A stale marker of a crashed run is undone
  // first; the taskbar is hidden (unless --keep-taskbar) before the first reload, so a bar that
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
            }},
           {QStringLiteral("tile"),
            [&engine](const QStringList& words) {
              // Built on the first `tile` command if no config did; inert unless the shell sets
              // Tiling.enabled.
              return waku::tiling::answerTileCommand(
                  words, engine.singletonInstance<waku::tiling::Tiling*>(
                             QStringLiteral("waku.services"), QStringLiteral("Tiling")));
            }},
           {QStringLiteral("desktop"),
            [&engine](const QStringList& words) {
              // Built on the first `desktop` command if no config did.
              return waku::workspaces::answerDesktopCommand(
                  words, engine.singletonInstance<waku::workspaces::VirtualDesktops*>(
                             QStringLiteral("waku.services"), QStringLiteral("VirtualDesktops")));
            }}},
          taskbar.get())));
  if (!control.listen()) {
    qCritical().noquote() << "waku is already running for" << configHome << "(wakuc quit stops it)";
    return 1;
  }

  if (taskbar) taskbar->start(!parser.isSet(keepTaskbarOpt));
  // The tiler is on by default: building the Tiling singleton turns it on, and a reload deletes the
  // singleton, so it is built again after every load. A shell can still set Tiling.enabled = false.
  const bool tilerWanted = !parser.isSet(noTilingOpt);
  const auto ensureTiler = [&engine, tilerWanted] {
    if (tilerWanted) {
      engine.singletonInstance<waku::tiling::Tiling*>(QStringLiteral("waku.services"),
                                                      QStringLiteral("Tiling"));
    }
  };
  QObject::connect(&host, &waku::host::ShellHost::reloaded, &host, [&host, ensureTiler](bool ok) {
    ensureTiler();
    waku::host::RunLog::event(
        QStringLiteral("shell.reload"),
        {{QStringLiteral("ok"), ok}, {QStringLiteral("error"), host.error()}});
  });
  host.reload();
  ensureTiler();
  waku::host::traceStep("first reload done");
  // The key daemon: started beside waku when the config home has a keys.conf, stopped with it. A
  // daemon that already runs (a logon task) is left alone.
  if (waku::kernel::liveDesktop() && !parser.isSet(noKeysOpt) && startKeys(configHome)) {
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [] { stopKeys(); });
  }
  if (parser.isSet(watchOpt)) {
    host.watch();
    waku::host::traceStep("watch done");
  }
  const int exitCode = app.exec();
  waku::host::RunLog::stop(exitCode);
  return exitCode;
}
