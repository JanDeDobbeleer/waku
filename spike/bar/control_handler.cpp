#include "spike/bar/control_handler.h"

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

#include "spike/bar/bar_host.h"

namespace wabar {

ControlHandler makeControlHandler(BarHost& host, bool safe, std::function<void()> quit) {
  if (!quit) quit = [] { QCoreApplication::quit(); };
  QPointer<BarHost> guard(&host);
  return [guard, safe, quit = std::move(quit)](const QString& command) -> QString {
    if (!guard) return QStringLiteral("error: the bar is shutting down");
    if (command == QLatin1String("reload")) {
      return guard->reload() ? QStringLiteral("ok") : QStringLiteral("error: ") + guard->error();
    }
    if (command == QLatin1String("status")) {
      const QString error = guard->error();
      return QStringLiteral("running; bars=%1; safe=%2; error=%3")
          .arg(guard->windows().size())
          .arg(safe ? QStringLiteral("yes") : QStringLiteral("no"))
          .arg(error.isEmpty() ? QStringLiteral("none") : error);
    }
    if (command == QLatin1String("quit")) {
      // After the answer has been written.
      QTimer::singleShot(100, [quit] { quit(); });
      return QStringLiteral("ok");
    }
    return QStringLiteral("error: unknown command '%1' (try: reload, status, quit)")
        .arg(command.left(40));
  };
}

}  // namespace wabar
