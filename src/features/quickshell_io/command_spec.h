#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <optional>

namespace waku::quickshell_io {

// What Quickshell's Process.exec() and Quickshell.execDetached() accept: an argument list, or an
// object with `command`, `environment`, `clearEnvironment` and `workingDirectory`.
struct CommandSpec {
  QStringList command;
  // A value is a string to set; null or invalid removes the variable (or, with clearEnvironment,
  // passes the system value through).
  QVariantMap environment;
  bool clearEnvironment = false;
  QString workingDirectory;
};

// Parses a QVariant holding a list (the command) or a map. Returns nothing for any other value or
// an empty command.
std::optional<CommandSpec> commandSpecFrom(const QVariant& value);

// The environment a child gets: the system environment (an empty one with clearEnvironment) with
// `spec.environment` applied.
QProcessEnvironment makeEnvironment(const CommandSpec& spec);

// Starts the command detached and hidden (no console window). False when it is empty or fails.
bool startDetachedSpec(const CommandSpec& spec);

}  // namespace waku::quickshell_io
