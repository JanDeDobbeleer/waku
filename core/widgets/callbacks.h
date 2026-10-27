#pragma once

#include <QString>
#include <QStringList>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace yasb::widgets {

struct ParsedCallback {
  QString name;
  QStringList args;
};

// Mirrors BaseWidget._run_callback: split only when a space is present; quoted args lose their
// quotes.
ParsedCallback parseCallback(const QString& callbackStr);

// Windows list2cmdline quoting, as subprocess.Popen(list, shell=True) builds the command.
QString quoteCmdLine(const QStringList& argv);

using CallbackFn = std::function<void(const QStringList& args)>;

// Launches `cmd.exe /c <commandLine>` detached. Replaceable so tests never spawn processes.
using ProcessLauncher = std::function<bool(const QString& commandLine)>;
ProcessLauncher& processLauncher();

// Named desktop actions ("notification_center", "search", ...) tried before spawning a process.
using SystemFunctionMap = std::map<QString, std::function<void()>>;
SystemFunctionMap& systemFunctions();
void registerDefaultSystemFunctions();

class CallbackRegistry {
 public:
  CallbackRegistry();

  // `arity` is the exact number of args accepted; -1 means variadic.
  void add(const QString& name, CallbackFn fn, int arity = 0);
  bool contains(const QString& name) const { return callbacks_.count(name) != 0; }

  // Unknown names log once and run nothing (as Python's KeyError path); bad arity or a throwing
  // handler also logs. Returns true only when a handler ran to completion.
  bool run(const QString& callbackStr);

 private:
  struct Entry {
    CallbackFn fn;
    int arity;
  };
  std::map<QString, Entry> callbacks_;
  std::set<QString> warned_;
};

}  // namespace yasb::widgets
