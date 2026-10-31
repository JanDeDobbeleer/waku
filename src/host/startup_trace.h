#pragma once

namespace wabar::host {

// Wall-clock trace of the start-up and of each reload, for profiling. With WABAR_TRACE=1 in the
// environment, traceStep() logs one line "wabar trace: <ms since start> ms <step>"; otherwise it
// does nothing. The clock starts at the first call of startTrace() (main() calls it first thing)
// or of traceStep().
//
// Threading: GUI thread only.
void startTrace();
bool traceEnabled();
void traceStep(const char* step);

}  // namespace wabar::host
