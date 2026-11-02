#pragma once

#include <QString>

namespace waku::control {

// A named mutex in the session namespace ("Local\<name>") that says whether another bar already
// owns a control pipe name; the pipe alone cannot tell, because several servers may listen on one
// pipe name. The handle is opaque (a HANDLE) so that no Windows header leaks into control.h.
//
// Returns the handle, or nullptr when the mutex cannot be created or another instance holds it.
void* acquireInstanceMutex(const QString& name);

// Closes a handle from acquireInstanceMutex. Null is fine.
void releaseInstanceMutex(void* mutex);

}  // namespace waku::control
