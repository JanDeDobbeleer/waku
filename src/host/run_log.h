#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtGlobal>

namespace waku::host {

struct RunLogOptions {
  // Absolute path of the log file. Its folder is created.
  QString path;
  // The file rotates to <path>.1 when it grows past this size (checked before each line).
  qint64 maxBytes = 5 * 1024 * 1024;
  // Rotated files kept: <path>.1 (newest) to <path>.<keep>.
  int keep = 3;
};

// The run log: a diagnostics file for finding the cause of odd behaviour after the fact, written
// as JSON Lines (one compact JSON object per line, UTF-8) so a person, a script or an agent can
// read it without a parser for a free-form format. Off unless `waku --log-file <path>` starts it.
// The format and the events are in docs/logging.md.
//
// Every Qt log message (qDebug, qInfo, qWarning, qCritical, QML console.*) becomes a `log` record
// and still goes on to the previous message handler, so stderr is unchanged. Named events
// (`run.start`, `run.stop`, `screen.changed`, `shell.reload`) are `ev` records with fields. Each
// record is flushed when it is written, so a crash loses nothing.
//
// A run that ended without `run.stop` (a crash, a kill, a power cut) is reported by the next run
// as `run.previous_unclean`.
//
// Threading: any thread may log; one mutex guards the file. start(), stop() and watchScreens()
// are for the GUI thread.
class RunLog {
 public:
  // Opens the file, installs the message handler and writes `run.start` (arguments, config home,
  // Qt, OS, CPU and a snapshot of every screen). False, with one line on the previous handler,
  // when the file cannot be opened; the run goes on without a log. A second start() is refused.
  static bool start(const RunLogOptions& options, const QStringList& arguments,
                    const QString& configHome);
  // Writes `run.stop` with `exitCode`, restores the previous message handler and closes the file.
  static void stop(int exitCode);
  static bool active();

  // One `ev` record. `fields` are added to it; the keys t, up, seq, run, k, ev and tid are the
  // log's own and win over a field of the same name. Does nothing when not active.
  static void event(const QString& name, const QVariantMap& fields = {});

  // Logs `screen.changed` with the cause and a snapshot of every screen on each Qt screen signal:
  // screens added or removed, the primary screen, and per screen geometry, available geometry,
  // device pixel ratio and DPI. Does nothing without a QGuiApplication; not active, it still
  // connects but each event is dropped.
  static void watchScreens();
};

}  // namespace waku::host
