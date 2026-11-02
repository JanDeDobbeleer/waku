#include "host/startup_trace.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QtGlobal>

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
  if (!t.enabled) return;
  qInfo().noquote() << QStringLiteral("waku trace: %1 ms").arg(t.clock.elapsed()) << step;
}

}  // namespace waku::host
