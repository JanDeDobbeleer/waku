#pragma once

// Joins the `Quickshell` singleton of an engine to the shell host: the config home becomes
// `Quickshell.shellDir`, `Quickshell.reload()` reloads the shell, and the outcome of each reload
// reaches `reloadCompleted` / `reloadFailed`. The host unit cannot see the feature that holds the
// singleton, so the composition root (and the tests that stand in for it) do this.

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include "features/quickshell_io/qml/quickshell_singleton.h"
#include "host/qml/shell_host.h"

namespace wabar::app {

inline void connectQuickshell(QQmlEngine* engine, host::ShellHost* host,
                              const QString& configHome) {
  const auto singleton = [engine] {
    return engine->singletonInstance<quickshell_io::QuickshellSingleton*>(
        QStringLiteral("wabar.services"), QStringLiteral("QuickshellSingleton"));
  };
  // A reload makes the engine drop its singletons, this one among them: nothing keeps the pointer,
  // and the wiring is done again for the new instance (host->singletonsCleared).
  const auto wire = [host, configHome, singleton] {
    auto* quickshell = singleton();
    if (!quickshell) return;
    quickshell->setShellDir(configHome);
    // Queued: a widget asks for the reload from inside the shell the reload replaces.
    QObject::connect(
        quickshell, &quickshell_io::QuickshellSingleton::reloadRequested, host,
        [host](bool) { host->reload(); }, Qt::QueuedConnection);
  };
  wire();
  QObject::connect(host, &host::ShellHost::singletonsCleared, host, wire);
  QObject::connect(host, &host::ShellHost::reloaded, host, [host, singleton](bool ok) {
    auto* quickshell = singleton();
    if (!quickshell) return;
    if (ok) {
      emit quickshell->reloadCompleted();
    } else {
      emit quickshell->reloadFailed(host->error());
    }
  });
}

}  // namespace wabar::app
