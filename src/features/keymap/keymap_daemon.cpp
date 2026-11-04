#include "features/keymap/keymap_daemon.h"

#include <QFile>
#include <QRegularExpression>
#include <QThreadPool>
#include <QTimer>
#include <exception>

#include "features/keymap/keymap_action.h"
#include "features/keymap/keymap_parser.h"

namespace waku::keymap {
namespace {

quint64 comboKey(const kernel::HotkeyCombo& combo) {
  return (static_cast<quint64>(combo.modifiers) << 32) | combo.virtualKey;
}

// A reload answers when the new shell is up, which takes seconds with a large shell.
constexpr int kBarTimeoutMs = 15000;
constexpr int kBarReloadTimeoutMs = 60000;
constexpr int kStatusTimeoutMs = 3000;

}  // namespace

Executor poolExecutor() {
  return [](std::function<void()> job) { QThreadPool::globalInstance()->start(std::move(job)); };
}

KeymapDaemon::KeymapDaemon(Deps deps, QObject* parent) : QObject(parent), deps_(std::move(deps)) {
  if (!deps_.executor) deps_.executor = poolExecutor();
}

KeymapDaemon::~KeymapDaemon() { unregisterAll(); }

void KeymapDaemon::log(const QString& line) const {
  if (deps_.log) deps_.log(line);
}

void KeymapDaemon::post(std::function<void()> job) {
  const auto logger = deps_.log;
  deps_.executor([job = std::move(job), logger] {
    try {
      job();
    } catch (const std::exception& error) {
      if (logger)
        logger(QStringLiteral("warning: action failed: %1").arg(QString::fromUtf8(error.what())));
    } catch (...) {
      if (logger) logger(QStringLiteral("warning: action failed"));
    }
  });
}

void KeymapDaemon::unregisterAll() {
  for (const kernel::Hotkeys::Handle handle : handles_) kernel::Hotkeys::instance().remove(handle);
  handles_.clear();
}

QString KeymapDaemon::statusText() const {
  return QStringLiteral("bindings=%1; conflicts=%2; issues=%3; paused=%4")
      .arg(bound_)
      .arg(conflicts_)
      .arg(issues_)
      .arg(paused_ ? QStringLiteral("yes") : QStringLiteral("no"));
}

KeymapDaemon::LoadResult KeymapDaemon::load(const QString& text) {
  LoadResult result;
  KeymapParseResult parsed = parseKeymap(text);
  result.issues = static_cast<int>(parsed.issues.size());
  for (const KeymapIssue& issue : parsed.issues) {
    log(QStringLiteral("warning: %1").arg(issueText(issue)));
  }
  if (parsed.keymap.bindings.empty() && !parsed.issues.empty()) {
    result.message = QStringLiteral("error: no valid binding in the file (%1 problem%2); %3")
                         .arg(result.issues)
                         .arg(result.issues == 1 ? QString() : QStringLiteral("s"))
                         .arg(handles_.empty() ? QStringLiteral("nothing is bound")
                                               : QStringLiteral("the previous bindings stay"));
    log(result.message);
    return result;
  }
  if (!kernel::Hotkeys::instance().active()) {
    result.message = QStringLiteral("error: global hotkeys are not available on this desktop");
    log(result.message);
    return result;
  }

  unregisterAll();
  keymap_ = std::move(parsed.keymap);
  issues_ = result.issues;
  conflicts_ = 0;
  bound_ = 0;
  paused_ = false;

  const auto taken = [this](const QString& keys, int line, const kernel::HotkeyCombo& combo) {
    ++conflicts_;
    if (reportedConflicts_.contains(comboKey(combo))) return;
    reportedConflicts_.insert(comboKey(combo));
    log(QStringLiteral("warning: line %1: '%2' is taken by another program; not bound")
            .arg(line)
            .arg(keys));
  };
  for (int i = 0; i < static_cast<int>(keymap_.bindings.size()); ++i) {
    const Binding& binding = keymap_.bindings[static_cast<size_t>(i)];
    const kernel::Hotkeys::Handle handle =
        kernel::Hotkeys::instance().add(binding.combo, [this, i] { trigger(i); });
    if (handle == 0) {
      taken(binding.keys, binding.line, binding.combo);
    } else {
      handles_.push_back(handle);
      ++bound_;
    }
  }
  if (keymap_.pause) {
    const kernel::Hotkeys::Handle handle =
        kernel::Hotkeys::instance().add(*keymap_.pause, [this] { togglePause(); });
    if (handle == 0) {
      taken(keymap_.pauseKeys, keymap_.pauseLine, *keymap_.pause);
    } else {
      handles_.push_back(handle);
    }
  }

  result.applied = true;
  result.bindings = bound_;
  result.conflicts = conflicts_;
  result.message = QStringLiteral("ok: %1 binding%2 active; conflicts=%3; issues=%4")
                       .arg(result.bindings)
                       .arg(result.bindings == 1 ? QString() : QStringLiteral("s"))
                       .arg(conflicts_)
                       .arg(issues_);
  log(result.message);
  return result;
}

KeymapDaemon::LoadResult KeymapDaemon::loadFile(const QString& path) {
  path_ = path;
  return reloadFile(false);
}

KeymapDaemon::LoadResult KeymapDaemon::reloadFile(bool onlyIfChanged) {
  QFile file(path_);
  if (!file.open(QIODevice::ReadOnly)) {
    LoadResult result;
    result.message = QStringLiteral("error: cannot read %1: %2; the previous bindings stay")
                         .arg(path_, file.errorString());
    log(result.message);
    return result;
  }
  const QString text = QString::fromUtf8(file.readAll());
  if (onlyIfChanged && haveText_ && text == lastText_) {
    LoadResult result;
    result.message = QStringLiteral("ok: unchanged");
    return result;
  }
  LoadResult result = load(text);
  if (result.applied) {
    lastText_ = text;
    haveText_ = true;
  }
  return result;
}

void KeymapDaemon::togglePause() {
  paused_ = !paused_;
  log(paused_ ? QStringLiteral("paused: the bindings do nothing until .pause is pressed again")
              : QStringLiteral("resumed"));
  if (!keymap_.pauseHook.isEmpty()) run(keymap_.pauseHook, QStringLiteral(".pause_hook"));
}

void KeymapDaemon::trigger(int index) {
  if (index < 0 || index >= bindingCount()) return;
  if (paused_) return;
  const Binding& binding = keymap_.bindings[static_cast<size_t>(index)];
  const bool needsForeground = !binding.rules.empty() || !keymap_.blacklist.isEmpty();
  const QString foreground =
      needsForeground && deps_.windows ? deps_.windows->foregroundProcessName() : QString();
  QString reason;
  const QString command = commandForForeground(binding, keymap_.blacklist, foreground, &reason);
  if (command.isEmpty()) {
    if (!reason.isEmpty()) log(QStringLiteral("%1: skipped, %2").arg(binding.keys, reason));
    return;
  }
  run(command, binding.keys);
}

void KeymapDaemon::launch(const QString& command, const QString& keys) {
  const ShellInvocation invocation = shellInvocation(keymap_.shell, command);
  ProcessLauncher* launcher = deps_.launcher;
  const auto logger = deps_.log;
  log(QStringLiteral("%1: run %2").arg(keys, command));
  post([launcher, invocation, logger, keys] {
    if (launcher && !launcher->launch(invocation) && logger) {
      logger(QStringLiteral("warning: %1: could not start %2").arg(keys, invocation.program));
    }
  });
}

void KeymapDaemon::run(const QString& command, const QString& keys) {
  const ActionParse parsed = parseAction(command);
  if (!parsed.action) {
    log(QStringLiteral("warning: %1: %2").arg(keys, parsed.error));
    return;
  }
  const Action& action = *parsed.action;
  switch (action.kind) {
    case Action::Kind::Shell:
      launch(action.text, keys);
      return;
    case Action::Kind::KeysReload:
      log(QStringLiteral("%1: reload the key file").arg(keys));
      // Not inside the hotkey callback: a reload replaces the table the callback runs from.
      QTimer::singleShot(0, this, [this] { reloadFile(false); });
      return;
    case Action::Kind::KeysStop:
      log(QStringLiteral("%1: stop").arg(keys));
      QTimer::singleShot(0, this, [this] {
        if (deps_.stop) deps_.stop();
      });
      return;
    case Action::Kind::ActivateOrLaunch:
      if (deps_.windows && deps_.windows->activateWindow(action.target)) {
        log(QStringLiteral("%1: activated %2").arg(keys, action.target));
        return;
      }
      log(QStringLiteral("%1: no window for %2").arg(keys, action.target));
      launch(action.text, keys);
      return;
    case Action::Kind::Bar: {
      const QString line = action.text;
      ControlClient* bar = deps_.bar;
      WindowSystem* windows = deps_.windows;
      const auto logger = deps_.log;
      log(QStringLiteral("%1: bar %2").arg(keys, line));
      post([bar, windows, logger, line, keys] {
        if (!bar) return;
        const bool takesFocus = line.startsWith(QLatin1String("surface show ")) ||
                                line.startsWith(QLatin1String("surface toggle "));
        if (takesFocus && windows) {
          // The bar can only take the keyboard focus if this process, which the user's input just
          // reached, allows it (the same step as `wakuc surface show`). The pid is in `status`.
          const control::ControlReply status =
              bar->send(QStringLiteral("status"), kStatusTimeoutMs);
          if (status.status == control::ControlReply::Ok) {
            const QRegularExpressionMatch match =
                QRegularExpression(QStringLiteral("\\bpid=(\\d+)")).match(status.text);
            if (match.hasMatch()) windows->allowForeground(match.captured(1).toULong());
          }
        }
        const int timeout = line == QLatin1String("reload") ? kBarReloadTimeoutMs : kBarTimeoutMs;
        const control::ControlReply reply = bar->send(line, timeout);
        if (!logger) return;
        switch (reply.status) {
          case control::ControlReply::NotRunning:
            logger(QStringLiteral("warning: %1: no bar is running").arg(keys));
            break;
          case control::ControlReply::Failed:
            logger(QStringLiteral("warning: %1: %2").arg(keys, reply.text));
            break;
          case control::ControlReply::Ok:
            logger(QStringLiteral("%1: bar answered %2").arg(keys, reply.text));
            break;
        }
      });
      return;
    }
  }
}

}  // namespace waku::keymap
