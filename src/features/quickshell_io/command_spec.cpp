#include "features/quickshell_io/command_spec.h"

#include <QMetaType>
#include <QProcess>
#include <utility>

#include "kernel/win32/shell_launch.h"

namespace waku::quickshell_io {

namespace {

QStringList toStrings(const QVariant& value) {
  QStringList out;
  const QVariantList list = value.toList();
  out.reserve(list.size());
  for (const QVariant& item : list) out.append(item.toString());
  return out;
}

bool isNull(const QVariant& value) {
  return !value.isValid() || value.metaType().id() == QMetaType::Nullptr;
}

}  // namespace

std::optional<CommandSpec> commandSpecFrom(const QVariant& value) {
  CommandSpec spec;
  if (value.metaType().id() == QMetaType::QVariantMap) {
    const QVariantMap map = value.toMap();
    spec.command = toStrings(map.value(QStringLiteral("command")));
    if (const auto it = map.constFind(QStringLiteral("environment")); it != map.constEnd()) {
      spec.environment = it->toMap();
    }
    spec.clearEnvironment = map.value(QStringLiteral("clearEnvironment")).toBool();
    spec.workingDirectory = map.value(QStringLiteral("workingDirectory")).toString();
  } else if (value.metaType().id() != QMetaType::QString && value.canConvert<QVariantList>()) {
    spec.command = toStrings(value);
  } else {
    return std::nullopt;
  }
  if (spec.command.isEmpty()) return std::nullopt;
  return spec;
}

QProcessEnvironment makeEnvironment(const CommandSpec& spec) {
  const QProcessEnvironment system = QProcessEnvironment::systemEnvironment();
  QProcessEnvironment env = spec.clearEnvironment ? QProcessEnvironment() : system;
  for (auto it = spec.environment.cbegin(); it != spec.environment.cend(); ++it) {
    if (isNull(it.value())) {
      if (spec.clearEnvironment) {
        if (system.contains(it.key())) env.insert(it.key(), system.value(it.key()));
      } else {
        env.remove(it.key());
      }
    } else {
      env.insert(it.key(), it.value().toString());
    }
  }
  return env;
}

bool startDetachedSpec(const CommandSpec& spec) {
  if (spec.command.isEmpty()) return false;
  QProcess process;
  process.setProgram(spec.command.first());
  process.setArguments(spec.command.mid(1));
  process.setProcessEnvironment(makeEnvironment(spec));
  if (!spec.workingDirectory.isEmpty()) process.setWorkingDirectory(spec.workingDirectory);
  kernel::hideConsole(process);
  return process.startDetached();
}

}  // namespace waku::quickshell_io
