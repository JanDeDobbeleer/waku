#include "host/startup_trace.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QVariantMap>
#include <QtGlobal>

#include "host/run_log.h"

namespace waku::host {

namespace {

struct Trace {
  Trace() : enabled(qEnvironmentVariableIntValue("WAKU_TRACE") != 0) { clock.start(); }
  bool enabled;
  QElapsedTimer clock;
};

Trace& trace() {
  static Trace t;
  return t;
}

}  // namespace

void startTrace() { trace(); }

bool traceEnabled() { return trace().enabled; }

void traceStep(const char* step) {
  const Trace& t = trace();
  // Always into the run log when it is on (each line is flushed): after a crash the last
  // `reload.step` shows how far the reload got.
  if (RunLog::active()) {
    RunLog::event(QStringLiteral("reload.step"),
                  {{QStringLiteral("step"), QString::fromLatin1(step)},
                   {QStringLiteral("ms"), static_cast<qlonglong>(t.clock.elapsed())}});
  }
  if (!t.enabled) return;
  qInfo().noquote() << QStringLiteral("waku trace: %1 ms").arg(t.clock.elapsed()) << step;
}

}  // namespace waku::host
