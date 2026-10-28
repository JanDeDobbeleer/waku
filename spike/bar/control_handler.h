#pragma once

#include <functional>

#include "spike/control/control.h"

namespace tidy {

class BarHost;

// What a running bar answers on its control channel:
//   reload  reloads the config: "ok", or "error: <first message>" (the previous bars stay)
//   status  "running; bars=N; safe=yes|no; error=<none or the message>"
//   quit    "ok", then `quit` runs shortly after, so the answer is written first
//   other   "error: unknown command '...' (try: reload, status, quit)"
// `quit` defaults to QCoreApplication::quit.
ControlHandler makeControlHandler(BarHost& host, bool safe, std::function<void()> quit = {});

}  // namespace tidy
