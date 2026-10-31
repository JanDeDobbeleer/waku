#pragma once

class QThreadPool;

namespace wabar::network {

// The pool the radio and VPN backends run their calls on. RasDial blocks a thread for the whole
// dial (tens of seconds), RasHangUp waits up to 4 s and a radio switch waits for Windows; on the
// global pool such a call would hold one of the threads that thumbnails, icons, the colour scheme
// and every status poll share. Two threads of its own, so a dial and a refresh still run side by
// side.
//
// Threads: created on the first call (GUI thread), parented to qApp, so it is deleted with the
// application, which waits for its running tasks as it does for the global pool. Its tasks post
// their results to qApp and drop them when the backend is gone.
QThreadPool* networkPool();

}  // namespace wabar::network
