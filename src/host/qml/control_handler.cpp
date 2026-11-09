#include "host/qml/control_handler.h"

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

#include "host/qml/global_shortcut.h"
#include "host/qml/ipc_handler.h"
#include "host/qml/shell_host.h"
#include "host/qml/surface_control.h"
#include "host/qml/windows_taskbar_command.h"
#include "host/windows_taskbar.h"

namespace waku::host {

QString answerShortcutCommand(const QStringList& words) {
  static const QString usage = QStringLiteral("error: usage: shortcut <name> | shortcut list");
  if (words.size() != 1) return usage;
  if (words.first() == QLatin1String("list")) {
    const QStringList names = GlobalShortcut::names();
    return names.isEmpty() ? QStringLiteral("ok: no shortcuts")
                           : QStringLiteral("ok: ") + control::joinCommand(names);
  }
  if (GlobalShortcut::fireByName(words.first())) return QStringLiteral("ok");
  return QStringLiteral("error: no shortcut named '%1' (try: shortcut list)")
      .arg(words.first().left(40));
}

control::ControlHandler makeControlHandler(ShellHost& host, bool safe, std::function<void()> quit,
                                           ControlCommands commands, WindowsTaskbar* taskbar) {
  if (!quit) quit = [] { QCoreApplication::quit(); };
  QPointer<ShellHost> guard(&host);
  return [guard, safe, quit = std::move(quit), commands = std::move(commands),
          taskbar](const QString& command) -> QString {
    if (!guard) return QStringLiteral("error: the shell is shutting down");
    if (command == QLatin1String("reload")) {
      const bool ok = guard->reload("wakuc");
      if (ok && guard->error().isEmpty()) return QStringLiteral("ok");
      return QStringLiteral("error: ") + guard->error();
    }
    if (command == QLatin1String("status")) {
      const QString error = guard->error();
      return QStringLiteral("running; pid=%1; windows=%2; safe=%3; error=%4")
          .arg(QCoreApplication::applicationPid())
          .arg(guard->windows().size())
          .arg(safe ? QStringLiteral("yes") : QStringLiteral("no"))
          .arg(error.isEmpty() ? QStringLiteral("none") : error);
    }
    if (command == QLatin1String("quit")) {
      // After the answer has been written.
      QTimer::singleShot(100, [quit] { quit(); });
      return QStringLiteral("ok");
    }
    if (command == QLatin1String("shortcut") || command.startsWith(QLatin1String("shortcut "))) {
      QStringList words;
      if (!control::splitCommand(command, &words))
        return QStringLiteral("error: unterminated quote");
      words.removeFirst();
      return answerShortcutCommand(words);
    }
    if (command == QLatin1String("call") || command.startsWith(QLatin1String("call "))) {
      QStringList words;
      if (!control::splitCommand(command, &words))
        return QStringLiteral("error: unterminated quote");
      words.removeFirst();
      return IpcHandler::callByName(words);
    }
    if (command == QLatin1String("surface") || command.startsWith(QLatin1String("surface "))) {
      QStringList words;
      if (!control::splitCommand(command, &words))
        return QStringLiteral("error: unterminated quote");
      words.removeFirst();
      return answerSurfaceCommand(words, guard.data());
    }
    if (command == QLatin1String("taskbar") || command.startsWith(QLatin1String("taskbar "))) {
      QStringList words;
      if (!control::splitCommand(command, &words))
        return QStringLiteral("error: unterminated quote");
      words.removeFirst();
      return answerWindowsTaskbarCommand(words, taskbar);
    }
    // A command added by the composition root: the first word names it.
    const QString verb = command.section(QLatin1Char(' '), 0, 0);
    if (const auto added = commands.find(verb); added != commands.end() && added->second) {
      QStringList words;
      if (!control::splitCommand(command, &words))
        return QStringLiteral("error: unterminated quote");
      words.removeFirst();
      return added->second(words);
    }
    QStringList verbs{QStringLiteral("reload"),  QStringLiteral("status"),
                      QStringLiteral("quit"),    QStringLiteral("shortcut"),
                      QStringLiteral("surface"), QStringLiteral("taskbar"),
                      QStringLiteral("call")};
    for (const auto& entry : commands) {
      if (!verbs.contains(entry.first)) verbs << entry.first;
    }
    return QStringLiteral("error: unknown command '%1' (try: %2)")
        .arg(command.left(40), verbs.join(QStringLiteral(", ")));
  };
}

control::AsyncControlHandler makeAsyncControlHandler(control::ControlHandler handler) {
  return
      [handler = std::move(handler)](const QString& command, const control::ControlReplyFn& reply) {
        if (command != QLatin1String("reload")) {
          reply(handler(command));
          return;
        }
        // qApp, not the host, as the context: the reply is sent even when the host is gone first
        // (the handler then answers that the shell is shutting down).
        QTimer::singleShot(0, QCoreApplication::instance(), [handler, reply, command] {
          try {
            reply(handler(command));
          } catch (const std::exception& e) {
            reply(QStringLiteral("error: ") + QString::fromUtf8(e.what()));
          } catch (...) {
            reply(QStringLiteral("error: the command failed"));
          }
        });
      };
}

}  // namespace waku::host
