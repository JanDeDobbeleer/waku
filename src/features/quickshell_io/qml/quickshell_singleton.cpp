#include "features/quickshell_io/qml/quickshell_singleton.h"

#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QMetaType>
#include <QVersionNumber>

#include "kernel/qt/app_data.h"

namespace wabar::quickshell_io {

namespace {

constexpr int kEmulatedMajor = 0;
constexpr int kEmulatedMinor = 3;

}  // namespace

QuickshellSingleton::Dirs QuickshellSingleton::defaultDirs() {
  Dirs dirs;
  const QString fromEnv = qEnvironmentVariable("WABAR_CONFIG_HOME");
  dirs.shell = QDir::cleanPath(
      fromEnv.isEmpty() ? QDir::homePath() + QStringLiteral("/.config/wabar") : fromEnv);
  const QString base = kernel::localAppDataBase() + QStringLiteral("/wabar");
  dirs.data = QDir::cleanPath(base + QStringLiteral("/data"));
  dirs.state = QDir::cleanPath(base + QStringLiteral("/state"));
  dirs.cache = QDir::cleanPath(base + QStringLiteral("/cache"));
  return dirs;
}

QuickshellSingleton::QuickshellSingleton(QObject* parent)
    : QuickshellSingleton(defaultDirs(), startDetachedSpec, parent) {}

QuickshellSingleton::QuickshellSingleton(Dirs dirs, Launcher launcher, QObject* parent)
    : QObject(parent), dirs_(std::move(dirs)), launcher_(std::move(launcher)) {
  connect(&clipboard_, &ClipboardText::textChanged, this,
          &QuickshellSingleton::clipboardTextChanged);
}

qint64 QuickshellSingleton::processId() const { return QCoreApplication::applicationPid(); }

void QuickshellSingleton::setShellDir(const QString& dir) {
  const QString clean = QDir::cleanPath(dir);
  if (clean == dirs_.shell) return;
  dirs_.shell = clean;
  emit shellDirChanged();
}

void QuickshellSingleton::setWatchFiles(bool watch) {
  if (watch == watchFiles_) return;
  watchFiles_ = watch;
  emit watchFilesChanged();
}

QString QuickshellSingleton::workingDirectory() const { return QDir::currentPath(); }

void QuickshellSingleton::setWorkingDirectory(const QString& directory) {
  if (directory.isEmpty() || QDir::currentPath() == QDir::cleanPath(directory)) return;
  if (QDir::setCurrent(directory)) emit workingDirectoryChanged();
}

QVariant QuickshellSingleton::env(const QString& name) const {
  if (!qEnvironmentVariableIsSet(name.toLocal8Bit().constData())) return QVariant();
  return qEnvironmentVariable(name.toLocal8Bit().constData());
}

void QuickshellSingleton::execDetached(const QVariant& context) {
  const auto spec = commandSpecFrom(context);
  if (!spec) {
    qWarning("Quickshell.execDetached: expected a command list or an object with a command");
    return;
  }
  if (!launcher_(*spec)) {
    qWarning().noquote() << "Quickshell.execDetached: could not start" << spec->command;
  }
}

QString QuickshellSingleton::join(const QString& base, const QString& path) {
  return QDir::cleanPath(QDir(base).filePath(path));
}

QString QuickshellSingleton::shellPath(const QString& path) const {
  return join(dirs_.shell, path);
}
QString QuickshellSingleton::dataPath(const QString& path) const { return join(dirs_.data, path); }
QString QuickshellSingleton::statePath(const QString& path) const {
  return join(dirs_.state, path);
}
QString QuickshellSingleton::cachePath(const QString& path) const {
  return join(dirs_.cache, path);
}

bool QuickshellSingleton::hasThemeIcon(const QString& icon) const {
  return QIcon::hasThemeIcon(icon);
}

QString QuickshellSingleton::iconPath(const QString& icon, const QVariant& checkOrFallback) const {
  if (icon.isEmpty()) return QString();
  if (QDir::isAbsolutePath(icon) || icon.contains(QStringLiteral("://"))) return icon;
  if (QIcon::hasThemeIcon(icon)) return QStringLiteral("image://icon/") + icon;
  if (checkOrFallback.metaType().id() == QMetaType::Bool) {
    if (checkOrFallback.toBool()) return QString();
  } else if (checkOrFallback.metaType().id() == QMetaType::QString &&
             !checkOrFallback.toString().isEmpty()) {
    return checkOrFallback.toString();
  }
  return icon;
}

bool QuickshellSingleton::hasVersion(int major, int minor, const QVariant&) const {
  return major < kEmulatedMajor || (major == kEmulatedMajor && minor <= kEmulatedMinor);
}

bool QuickshellSingleton::hasQtVersion(int major, int minor) const {
  const QVersionNumber current = QVersionNumber::fromString(QString::fromLatin1(qVersion()));
  return QVersionNumber(current.majorVersion(), current.minorVersion()) >=
         QVersionNumber(major, minor);
}

void QuickshellSingleton::reload(bool hard) { emit reloadRequested(hard); }

ElapsedTimer::ElapsedTimer(QObject* parent) : QObject(parent) { timer_.start(); }

double ElapsedTimer::elapsedNs() const { return static_cast<double>(timer_.nsecsElapsed()); }
double ElapsedTimer::elapsedMs() const { return elapsedNs() / 1e6; }
double ElapsedTimer::elapsed() const { return elapsedNs() / 1e9; }

double ElapsedTimer::restartNs() {
  const double ns = elapsedNs();
  timer_.restart();
  return ns;
}
double ElapsedTimer::restartMs() { return restartNs() / 1e6; }
double ElapsedTimer::restart() { return restartNs() / 1e9; }

}  // namespace wabar::quickshell_io
