#pragma once

#include <QString>

#include "app/wakukeys/keys_args.h"

namespace wakukeys {

// Runs what the command line asked for: the daemon (the event loop of the program) or one control
// verb sent to the daemon that runs. Exit codes: 0 ok, 1 error (a bad first file, already
// running), 2 daemon not running, 3 usage.
int runKeys(const KeysArgs& args);

// The control pipe of the daemon of the signed-in user.
QString keysPipeName();

}  // namespace wakukeys
