#include "app/log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QtLogging>
#include <memory>

namespace yasb::app {
namespace {

QMutex mutex;
std::unique_ptr<QFile> logFile;
std::function<void(const QString&)> observer;
bool debugEnabled = false;

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  const QString line = qFormatLogMessage(type, context, message);
  std::function<void(const QString&)> sink;
  {
    QMutexLocker lock(&mutex);
    if (logFile && (type != QtDebugMsg || debugEnabled)) {
      logFile->write(line.toUtf8());
      logFile->write("\r\n");
      logFile->flush();
    }
    sink = observer;
  }
  if (sink && (type != QtDebugMsg || debugEnabled)) sink(line);
  if (type == QtFatalMsg) abort();
}

}  // namespace

bool initializeLog(const QString& path, bool debug) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  auto file = std::make_unique<QFile>(path);
  if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return false;
  {
    QMutexLocker lock(&mutex);
    logFile = std::move(file);
    debugEnabled = debug;
  }
  qSetMessagePattern(QStringLiteral("%{time yyyy-MM-dd hh:mm:ss.zzz} [%{type}] %{message}"));
  qInstallMessageHandler(messageHandler);
  return true;
}

void setLogObserver(std::function<void(const QString&)> sink) {
  QMutexLocker lock(&mutex);
  observer = std::move(sink);
}

}  // namespace yasb::app
