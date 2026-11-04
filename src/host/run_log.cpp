#include "host/run_log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRect>
#include <QScreen>
#include <QSysInfo>
#include <QThread>
#include <QUuid>
#include <cstdio>
#include <exception>
#include <memory>

namespace waku::host {
namespace {

struct State {
  RunLogOptions options;
  QFile file;
  QElapsedTimer clock;
  QString run;
  quint64 seq = 0;
  QtMessageHandler previous = nullptr;
};

QMutex& mutex() {
  static QMutex m;
  return m;
}

// Owned by the mutex above. Null when the log is not running.
std::unique_ptr<State>& state() {
  static std::unique_ptr<State> s;
  return s;
}

// A log call made while a record is written (a failing write, say) must not write again.
thread_local bool g_writing = false;

QString isoNow() {
  const QDateTime now = QDateTime::currentDateTime();
  return now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODateWithMs);
}

QString levelName(QtMsgType type) {
  switch (type) {
    case QtDebugMsg:
      return QStringLiteral("debug");
    case QtInfoMsg:
      return QStringLiteral("info");
    case QtWarningMsg:
      return QStringLiteral("warning");
    case QtCriticalMsg:
      return QStringLiteral("critical");
    case QtFatalMsg:
      return QStringLiteral("fatal");
  }
  return QStringLiteral("info");
}

// Renames path -> path.1 -> ... -> path.<keep>; the oldest goes. Caller holds the mutex and has
// closed the file.
void rotateFiles(const QString& path, int keep) {
  if (keep < 1) {
    QFile::remove(path);
    return;
  }
  QFile::remove(QStringLiteral("%1.%2").arg(path).arg(keep));
  for (int i = keep - 1; i >= 1; --i) {
    QFile::rename(QStringLiteral("%1.%2").arg(path).arg(i),
                  QStringLiteral("%1.%2").arg(path).arg(i + 1));
  }
  QFile::rename(path, path + QStringLiteral(".1"));
}

// Writes one record. Caller holds the mutex and has checked that the state exists.
void writeRecord(State& s, QJsonObject record) {
  if (g_writing) return;
  g_writing = true;
  record.insert(QStringLiteral("t"), isoNow());
  record.insert(QStringLiteral("up"), static_cast<double>(s.clock.elapsed()));
  record.insert(QStringLiteral("seq"), static_cast<double>(++s.seq));
  record.insert(QStringLiteral("run"), s.run);
  record.insert(QStringLiteral("tid"),
                static_cast<double>(reinterpret_cast<quintptr>(QThread::currentThreadId())));
  QByteArray line = QJsonDocument(record).toJson(QJsonDocument::Compact);
  line.append('\n');
  if (s.options.maxBytes > 0 && s.file.size() + line.size() > s.options.maxBytes &&
      s.file.size() > 0) {
    s.file.close();
    rotateFiles(s.options.path, s.options.keep);
    static_cast<void>(
        s.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered));
  }
  if (s.file.isOpen()) s.file.write(line);
  g_writing = false;
}

QString rectText(const QRect& r) {
  return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

QJsonArray screenSnapshot() {
  QJsonArray screens;
  auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
  if (!app) return screens;
  const QScreen* primary = QGuiApplication::primaryScreen();
  for (const QScreen* screen : QGuiApplication::screens()) {
    QJsonObject entry;
    entry.insert(QStringLiteral("name"), screen->name());
    entry.insert(QStringLiteral("primary"), screen == primary);
    entry.insert(QStringLiteral("geometry"), rectText(screen->geometry()));
    entry.insert(QStringLiteral("available"), rectText(screen->availableGeometry()));
    entry.insert(QStringLiteral("dpr"), screen->devicePixelRatio());
    entry.insert(QStringLiteral("logicalDpi"), screen->logicalDotsPerInch());
    entry.insert(QStringLiteral("physicalDpi"), screen->physicalDotsPerInch());
    entry.insert(QStringLiteral("refreshHz"), screen->refreshRate());
    screens.append(entry);
  }
  return screens;
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& text) {
  QtMessageHandler previous = nullptr;
  {
    QMutexLocker lock(&mutex());
    if (State* s = state().get()) {
      previous = s->previous;
      QJsonObject record;
      record.insert(QStringLiteral("k"), QStringLiteral("log"));
      record.insert(QStringLiteral("lvl"), levelName(type));
      if (context.category && qstrcmp(context.category, "default") != 0) {
        record.insert(QStringLiteral("cat"), QString::fromLatin1(context.category));
      }
      record.insert(QStringLiteral("msg"), text);
      writeRecord(*s, record);
    }
  }
  // The previous handler is Qt's own (stderr) unless something installed one before the log.
  if (previous) {
    previous(type, context, text);
  } else {
    // Qt's default handler is not reachable once replaced; mirror it on stderr.
    const QByteArray bytes = text.toLocal8Bit();
    fprintf(stderr, "%s\n", bytes.constData());
    fflush(stderr);
  }
}

// True when the last run recorded in `path` has a run.start and no run.stop after it. Reads the
// last 16 KiB only.
bool previousRunUnclean(const QString& path, QString* previousRun) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() == 0) return false;
  const qint64 tail = 16 * 1024;
  if (file.size() > tail) file.seek(file.size() - tail);
  const QList<QByteArray> lines = file.readAll().split('\n');
  bool started = false;
  for (const QByteArray& line : lines) {
    QJsonParseError error;
    const QJsonObject record = QJsonDocument::fromJson(line, &error).object();
    if (error.error != QJsonParseError::NoError) continue;  // a cut first line
    if (record.value(QStringLiteral("k")).toString() != QLatin1String("ev")) continue;
    const QString name = record.value(QStringLiteral("ev")).toString();
    if (name == QLatin1String("run.start")) {
      started = true;
      *previousRun = record.value(QStringLiteral("run")).toString();
    } else if (name == QLatin1String("run.stop")) {
      started = false;
    }
  }
  return started;
}

QJsonObject toObject(const QVariantMap& fields) { return QJsonObject::fromVariantMap(fields); }

}  // namespace

bool RunLog::start(const RunLogOptions& options, const QStringList& arguments,
                   const QString& configHome) {
  QMutexLocker lock(&mutex());
  if (state()) return false;
  auto s = std::make_unique<State>();
  s->options = options;
  const QFileInfo info(options.path);
  if (options.path.isEmpty() || !QDir().mkpath(info.absolutePath())) {
    qWarning().noquote() << "waku: cannot create the folder of the log file" << options.path;
    return false;
  }
  QString previousRun;
  const bool unclean = previousRunUnclean(options.path, &previousRun);
  s->file.setFileName(options.path);
  if (!s->file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered)) {
    qWarning().noquote() << "waku: cannot open the log file" << options.path << "-"
                         << s->file.errorString();
    return false;
  }
  s->clock.start();
  s->run = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  State& ref = *s;
  state() = std::move(s);
  ref.previous = qInstallMessageHandler(handler);

  QJsonObject header;
  header.insert(QStringLiteral("k"), QStringLiteral("ev"));
  header.insert(QStringLiteral("ev"), QStringLiteral("run.start"));
  header.insert(QStringLiteral("argv"), QJsonArray::fromStringList(arguments));
  header.insert(QStringLiteral("configHome"), configHome);
  header.insert(QStringLiteral("app"), QCoreApplication::applicationName());
  header.insert(QStringLiteral("version"), QCoreApplication::applicationVersion());
  header.insert(QStringLiteral("exe"), QCoreApplication::applicationFilePath());
  header.insert(QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid()));
  header.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
  header.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
  header.insert(QStringLiteral("kernel"), QSysInfo::kernelVersion());
  header.insert(QStringLiteral("cpu"), QSysInfo::currentCpuArchitecture());
  if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    header.insert(QStringLiteral("platform"), QGuiApplication::platformName());
  }
  header.insert(QStringLiteral("screens"), screenSnapshot());
  writeRecord(ref, header);
  if (unclean) {
    QJsonObject record;
    record.insert(QStringLiteral("k"), QStringLiteral("ev"));
    record.insert(QStringLiteral("ev"), QStringLiteral("run.previous_unclean"));
    record.insert(QStringLiteral("previousRun"), previousRun);
    writeRecord(ref, record);
  }
  return true;
}

void RunLog::stop(int exitCode) {
  QMutexLocker lock(&mutex());
  State* s = state().get();
  if (!s) return;
  QJsonObject record;
  record.insert(QStringLiteral("k"), QStringLiteral("ev"));
  record.insert(QStringLiteral("ev"), QStringLiteral("run.stop"));
  record.insert(QStringLiteral("exit"), exitCode);
  writeRecord(*s, record);
  qInstallMessageHandler(s->previous);
  s->file.close();
  state().reset();
}

bool RunLog::active() {
  QMutexLocker lock(&mutex());
  return state() != nullptr;
}

void RunLog::event(const QString& name, const QVariantMap& fields) {
  QMutexLocker lock(&mutex());
  State* s = state().get();
  if (!s) return;
  QJsonObject record = toObject(fields);
  record.insert(QStringLiteral("k"), QStringLiteral("ev"));
  record.insert(QStringLiteral("ev"), name);
  writeRecord(*s, record);
}

void RunLog::watchScreens() {
  auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
  if (!app) return;
  const auto changed = [](const QString& why, const QString& screen) {
    if (!active()) return;
    QVariantMap fields;
    fields.insert(QStringLiteral("why"), why);
    if (!screen.isEmpty()) fields.insert(QStringLiteral("screen"), screen);
    fields.insert(QStringLiteral("screens"), screenSnapshot().toVariantList());
    event(QStringLiteral("screen.changed"), fields);
  };
  const auto watchScreen = [changed](QScreen* screen) {
    const QString name = screen->name();
    QObject::connect(screen, &QScreen::geometryChanged, screen,
                     [changed, name] { changed(QStringLiteral("geometryChanged"), name); });
    QObject::connect(screen, &QScreen::availableGeometryChanged, screen, [changed, name] {
      changed(QStringLiteral("availableGeometryChanged"), name);
    });
    QObject::connect(screen, &QScreen::logicalDotsPerInchChanged, screen, [changed, name] {
      changed(QStringLiteral("logicalDotsPerInchChanged"), name);
    });
    QObject::connect(screen, &QScreen::physicalDotsPerInchChanged, screen, [changed, name] {
      changed(QStringLiteral("physicalDotsPerInchChanged"), name);
    });
    QObject::connect(screen, &QScreen::refreshRateChanged, screen,
                     [changed, name] { changed(QStringLiteral("refreshRateChanged"), name); });
  };
  QObject::connect(app, &QGuiApplication::screenAdded, app, [changed, watchScreen](QScreen* s) {
    watchScreen(s);
    changed(QStringLiteral("screenAdded"), s->name());
  });
  QObject::connect(app, &QGuiApplication::screenRemoved, app,
                   [changed](QScreen* s) { changed(QStringLiteral("screenRemoved"), s->name()); });
  QObject::connect(app, &QGuiApplication::primaryScreenChanged, app, [changed](QScreen* s) {
    changed(QStringLiteral("primaryScreenChanged"), s ? s->name() : QString());
  });
  for (QScreen* screen : QGuiApplication::screens()) watchScreen(screen);
}

}  // namespace waku::host
