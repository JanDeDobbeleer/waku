#include "host/plugin_guard.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QtGlobal>

namespace wabar::host {
namespace {

constexpr quint16 kMachineX64 = 0x8664;
constexpr quint16 kMachineArm64 = 0xAA64;

QString machineName(quint16 machine) {
  switch (machine) {
    case kMachineX64:
      return QStringLiteral("x64");
    case kMachineArm64:
      return QStringLiteral("ARM64");
    case 0x014c:
      return QStringLiteral("x86");
    default:
      return QStringLiteral("machine 0x%1").arg(machine, 4, 16, QLatin1Char('0'));
  }
}

QString versionText(int version) {
  return QStringLiteral("%1.%2.%3")
      .arg(version >> 16)
      .arg((version >> 8) & 0xff)
      .arg(version & 0xff);
}

constexpr bool kDebugBuild =
#ifdef QT_DEBUG
    true;
#else
    false;
#endif

}  // namespace

PluginVerdict checkPluginMetadata(const QJsonObject& metaData, int qtVersion, bool debugBuild) {
  const QJsonValue version = metaData.value(QStringLiteral("version"));
  const QJsonValue debug = metaData.value(QStringLiteral("debug"));
  if (!version.isDouble() || !debug.isBool()) {
    return {false, QStringLiteral("no Qt plugin metadata (not a Qt plugin)")};
  }
  const int built = version.toInt();
  if ((built >> 16) != (qtVersion >> 16) || ((built >> 8) & 0xff) != ((qtVersion >> 8) & 0xff)) {
    return {false, QStringLiteral("built for Qt %1, this wabar uses Qt %2")
                       .arg(versionText(built), versionText(qtVersion))};
  }
  if (debug.toBool() != debugBuild) {
    return {false, QStringLiteral("built as a %1 plugin, this wabar is a %2 build")
                       .arg(debug.toBool() ? QStringLiteral("debug") : QStringLiteral("release"),
                            debugBuild ? QStringLiteral("debug") : QStringLiteral("release"))};
  }
  return {};
}

quint16 peMachine(const QByteArray& header) {
  if (header.size() < 0x40 || header[0] != 'M' || header[1] != 'Z') return 0;
  const auto byte = [&](qsizetype i) {
    return static_cast<quint32>(static_cast<uchar>(header[i]));
  };
  const quint32 pe = byte(0x3C) | byte(0x3D) << 8 | byte(0x3E) << 16 | byte(0x3F) << 24;
  if (pe > static_cast<quint32>(header.size()) - 6 ||
      header.mid(pe, 4) != QByteArray("PE\0\0", 4)) {
    return 0;
  }
  return static_cast<quint16>(byte(pe + 4) | byte(pe + 5) << 8);
}

quint16 hostMachine() {
#if defined(_M_ARM64)
  return kMachineArm64;
#elif defined(_M_X64)
  return kMachineX64;
#else
  return 0;
#endif
}

PluginVerdict checkPluginFile(const QString& libraryPath) {
  QFile file(libraryPath);
  if (!file.open(QIODevice::ReadOnly)) return {false, QStringLiteral("the file cannot be read")};
  const quint16 machine = peMachine(file.read(4096));
  file.close();
  if (machine == 0) return {false, QStringLiteral("not a Windows library")};
  if (hostMachine() != 0 && machine != hostMachine()) {
    return {false, QStringLiteral("built for %1, this wabar runs as %2")
                       .arg(machineName(machine), machineName(hostMachine()))};
  }
  // Reads the embedded metadata from the file; the library is not loaded.
  const QPluginLoader loader(libraryPath);
  return checkPluginMetadata(loader.metaData(), QT_VERSION, kDebugBuild);
}

QStringList findPluginFiles(const QString& root) {
  QStringList files;
  QDirIterator it(root, {QStringLiteral("qmldir")}, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QFileInfo qmldir(it.next());
    QFile file(qmldir.filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
    while (!file.atEnd()) {
      QStringList words = QString::fromUtf8(file.readLine())
                              .simplified()
                              .split(QLatin1Char(' '), Qt::SkipEmptyParts);
      if (!words.isEmpty() && words.first() == QLatin1String("optional")) words.removeFirst();
      if (words.size() < 2 || words.first() != QLatin1String("plugin")) continue;
      const QDir dir(words.size() > 2 ? qmldir.dir().filePath(words.at(2)) : qmldir.path());
      const QString name = words.at(1);
      const QString plain = dir.filePath(name + QStringLiteral(".dll"));
      const QString debug = dir.filePath(name + QStringLiteral("d.dll"));
      const QStringList order = kDebugBuild ? QStringList{debug, plain} : QStringList{plain, debug};
      for (const QString& candidate : order) {
        if (QFileInfo::exists(candidate)) {
          files << QDir::cleanPath(candidate);
          break;
        }
      }
    }
  }
  files.removeDuplicates();
  return files;
}

QStringList importRoots(const QString& configHome, const QStringList& extra) {
  QStringList roots;
  const auto add = [&roots](const QString& path) {
    const QFileInfo info(path);
    if (!info.isDir()) return;
    const QString clean = QDir::cleanPath(info.absoluteFilePath());
    if (!roots.contains(clean, Qt::CaseInsensitive)) roots << clean;
  };
  if (!configHome.isEmpty()) add(QDir(configHome).filePath(QStringLiteral("modules")));
  for (const QString& path : extra) add(path);
  return roots;
}

QStringList usableImportRoots(const QStringList& roots) {
  QStringList usable;
  for (const QString& root : roots) {
    bool ok = true;
    for (const QString& plugin : findPluginFiles(root)) {
      const PluginVerdict verdict = checkPluginFile(plugin);
      if (verdict.ok) continue;
      ok = false;
      qWarning().noquote() << "wabar: plugin refused:" << QDir::toNativeSeparators(plugin) << "-"
                           << verdict.reason << "; the folder" << QDir::toNativeSeparators(root)
                           << "is not used";
    }
    if (ok) usable << root;
  }
  return usable;
}

}  // namespace wabar::host
