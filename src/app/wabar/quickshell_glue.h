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
  auto* quickshell = engine->singletonInstance<quickshell_io::QuickshellSingleton*>(
      QStringLiteral("wabar.services"), QStringLiteral("QuickshellSingleton"));
  if (!quickshell) return;
  quickshell->setShellDir(configHome);
  // Queued: a widget asks for the reload from inside the shell the reload replaces.
  QObject::connect(
      quickshell, &quickshell_io::QuickshellSingleton::reloadRequested, host,
      [host](bool) { host->reload(); }, Qt::QueuedConnection);
  QObject::connect(host, &host::ShellHost::reloaded, quickshell, [quickshell, host](bool ok) {
    if (ok) {
      emit quickshell->reloadCompleted();
    } else {
      emit quickshell->reloadFailed(host->error());
    }
  });
}

}  // namespace wabar::app
