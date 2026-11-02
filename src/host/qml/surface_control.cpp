#include "host/qml/surface_control.h"

#include <QMap>

#include "host/qml/qs_window.h"
#include "host/qml/shell_host.h"

namespace waku::host {

namespace {

const QString& usage() {
  static const QString text = QStringLiteral(
      "error: usage: surface list | show <name> | hide <name> | toggle <name> | status <name>");
  return text;
}

QString stateOf(const QList<QsWindow*>& windows) {
  for (const QsWindow* window : windows) {
    if (window->visible()) return QStringLiteral("shown");
  }
  return QStringLiteral("hidden");
}

QString describe(const QString& name, const QList<QsWindow*>& windows) {
  QString text = name + QLatin1Char('=') + stateOf(windows);
  if (windows.size() > 1) text += QStringLiteral(" (%1 windows)").arg(windows.size());
  return text;
}

}  // namespace

QString answerSurfaceCommand(const QStringList& words, ShellHost* host) {
  if (words.isEmpty()) return usage();
  const QString verb = words.first();
  const bool known = verb == QLatin1String("list") || verb == QLatin1String("show") ||
                     verb == QLatin1String("hide") || verb == QLatin1String("toggle") ||
                     verb == QLatin1String("status");
  if (!known) {
    return QStringLiteral(
               "error: unknown surface command '%1' (try: list, show, hide, toggle, "
               "status)")
        .arg(verb.left(40));
  }
  const bool listing = verb == QLatin1String("list");
  if (words.size() != (listing ? 1 : 2)) return usage();
  if (!host) return QStringLiteral("error: the shell is not available");

  const QList<QsWindow*> all = host->namedWindows();
  if (listing) {
    QMap<QString, QList<QsWindow*>> byName;  // sorted, so the answer is stable
    for (QsWindow* window : all) byName[window->surfaceName()] << window;
    if (byName.isEmpty()) return QStringLiteral("ok: none");
    QStringList parts;
    for (auto it = byName.constBegin(); it != byName.constEnd(); ++it) {
      parts << describe(it.key(), it.value());
    }
    return QStringLiteral("ok: ") + parts.join(QStringLiteral(", "));
  }

  const QString name = words.at(1);
  const QList<QsWindow*> windows = host->namedWindows(name);
  if (windows.isEmpty()) {
    QStringList names;
    for (const QsWindow* window : all) {
      if (!names.contains(window->surfaceName())) names << window->surfaceName();
    }
    names.sort();
    return QStringLiteral("error: no surface named '%1' (known: %2)")
        .arg(name.left(40),
             names.isEmpty() ? QStringLiteral("none") : names.join(QLatin1Char(',')));
  }
  if (verb == QLatin1String("show") || verb == QLatin1String("hide") ||
      verb == QLatin1String("toggle")) {
    const bool show =
        verb == QLatin1String("show") ||
        (verb == QLatin1String("toggle") && stateOf(windows) == QLatin1String("hidden"));
    for (QsWindow* window : windows) window->setVisible(show);
  }
  QString answer = QStringLiteral("ok: %1 %2").arg(name, stateOf(windows));
  if (windows.size() > 1) answer += QStringLiteral(" (%1 windows)").arg(windows.size());
  return answer;
}

}  // namespace waku::host
