# C++ review, 2026-10-31

Branch `perf/lazy-surfaces` at `9f9dcd5`. Scope: every `.cpp`/`.h` under `src/` (449 files, 66k lines),
read against `AGENTS.md` and `docs/architecture.md`. Read-only: nothing was built or run. Line
numbers are from this commit. Findings from five parallel reviewers (host+control+kernel;
tray+taskbar+apps; bluetooth+audio+network+brightness+control_center; media+notifications+
color_scheme+wallpapers; workspaces+quickshell_io+github+system_stats+shell+clock+desktop_state+app)
were merged, cross-checked and deduplicated by the orchestrator, who read the reload path
(`shell_host.cpp`, `shell_window.cpp`, `lazy_loader.cpp`, `mask_poller.cpp`, `thumbnail_store.cpp`,
`thumbnails.cpp`, `main.cpp`) first hand.

## Verdict

The code is well organised and most lifetimes are right: nearly every pointer that crosses a
reload boundary is a `QPointer`, the two `weak_ptr` hubs (media, notifications) are sound, the
tray pipe's overlapped I/O is cancelled and drained correctly, and the `bbe1771` ownership leak has
no sibling. The heap corruptions are nonetheless explainable from the source. The single most
direct path is `LazyLoader::destroyItem()` deleting a `QsWindow` and its `QQuickWindow`
synchronously from inside that window's own event delivery (C1). Around it sit a family of
check-then-use races in which a worker thread tests a `QPointer` and then posts to the raw pointer
(github, bluetooth, audio, color_scheme; C4), two WNF callbacks that hold raw pointers to objects a
reload destroys (C5), WinHTTP handles closed under a synchronous receive (C6), image providers that
mutate unguarded maps when a shell sets `asynchronous: true` (C7), and per-call COM apartment
teardown that unloads shell extensions on pool threads (C8). The reload itself lets a whole
generation of old windows live through `clearSingletons()`/`clearComponentCache()` (C3), which is
the window in which all of the above fire. Memory: the 625 MB is not explained by any one container
in C++. The candidates, in order, are five visible full-screen transparent windows each with its
own render thread, D3D11 device, swap chain and depth-stencil (C27, to be measured first), retained
wallpaper transition pixmaps (C28, 66-132 MB), two 64 MB thumbnail LRUs after a reload plus a
second PNG thumbnail cache (C29), and two generations of compiled QML (C31). Startup: of the 2.2 s
the shell costs, the throwaway validation engine compiles the whole shell once or twice before the
real engine does (C35), three recursive walks of the config home and a plugin scan (C36), and a
set of singleton constructors that do COM, registry and disk work on the GUI thread (C37) account
for an estimated 0.7-1.5 s. Fix C1-C8 first, then measure C27 before touching anything else in the
memory list.

## Findings

Format: ID, category, severity, effort. Severity: critical (a concrete heap write after free),
high (a concrete use after free, stall or large resource with a clear trigger), medium, low.
Effort: S (under a day), M (days), L (a week or more).

### Crash risk and heap corruption

#### C1 crash-risk, critical, S
`src/host/qml/lazy_loader.cpp:172` (`destroyItem`), `:41`, `:49-50`, `:60-61`, reaching
`src/host/qml/shell_window.cpp:84`.
Problem: `destroyItem()` does `delete item`. The item is usually a `QsWindow`; `~QsWindow` runs
`window_.reset()`, which deletes the `QQuickWindow` synchronously. The documented pattern
(`launcher.active = false` from a handler inside the loaded window, or from `onHidden`,
`onVisibleChanged`, `onFirstShown`) runs this while `QQuickDeliveryAgentPrivate::deliverPointerEvent`,
`QsWindow::setVisible` (`:191-193`, `emit visibleChanged()` then `updateVisibility()` on `this`) or
`QsWindow::updateVisibility` (`:363-368`, `emit firstShown()` then `host_->windowShown(this, ...)`)
are still on the stack. After the handler returns, Qt and `QsWindow` write into the freed window
and delivery agent: a write after free into heap metadata, the signature of `0xc0000374` and
`FAST_FAIL_UNEXPECTED_HEAP_EXCEPTION`. Qt's own `Loader` uses `deleteLater()` for this reason
(`QQuickLoaderPrivate::clear`: "our item may have triggered the Loader to load a different item").
Fix: in `destroyItem()` detach then defer: `item_ = nullptr; object->setParent(nullptr); if (auto*
w = qobject_cast<QsWindow*>(object)) w->setPresentable(false); object->deleteLater();`. Clear the
item's context (`QQmlContextData::get(qmlContext(object))->clearContextRecursively()` is what Qt
does) so bindings stop before deletion. Same in `~LazyLoader` (`:41`).
Gain: removes the most direct heap-corruption path in the repository.

#### C2 crash-risk, high, S
`src/host/qml/shell_window.cpp:84` with unguarded getters at `:127`, `:258-259`, `:266`,
`:294-305`.
Problem: `std::unique_ptr::reset()` nulls `window_` before `~QQuickWindow` runs. `~QQuickWindow`
destroys the chrome and content items; `~QQuickItem` calls `setParentItem(nullptr)` on each child,
which emits `parentChanged`/`windowChanged` and re-evaluates bindings. Any binding that reads
`shellWindow.width`, `height`, `contentItem` or `backingWindowVisible` during that teardown calls a
getter that dereferences a null `window_`. A null dereference, not heap corruption, but a crash on
the reload path with the same lack of a dump.
Fix: `ProxiedWindow* w = window_.release(); windowDying_ = w; delete w; windowDying_ = nullptr;`
and make the getters read `window_ ? window_.get() : windowDying_`, or null-check every getter.
Also clear `ProxiedWindow::proxy_` before the delete so `keyPressEvent` (`:44-52`) cannot reach a
dying proxy.

#### C3 crash-risk, high, S
`src/host/qml/shell_host.cpp:465-473` (`resetEngine`) against `:161-169` (`~Loaded`).
Problem: `loaded_.reset()` deletes the root synchronously, but every window made by `Variants`
(an `Instantiator`, `src/host/qml/Variants.qml:19`) is released with `deleteLater()` by
`QQmlDelegateModel`. Those windows, their items, bindings and `QQuickWindow`s live through
`clearSingletons()`, `clearComponentCache()`, `applyTheme()`, `build()` and `install()`, binding
against deleted `Theme`, `ScreenList`, `Quickshell` and every feature singleton, and die one event
loop pass later while the new shell is presenting. The Qt documentation for both calls requires
that no object created from the components is alive. This is the only moment two generations
coexist, and every other crash candidate in this section fires inside it. The comments at
`shell_host.cpp:427-428` ("the old shell stays alive while the new one is built") and
`src/host/qml/shell_screen.cpp:34-35` describe the opposite of what `:430` does.
Fix: after `loaded_.reset()` call `QCoreApplication::sendPostedEvents(nullptr,
QEvent::DeferredDelete)` (the documented way to flush deferred deletes from inside a slot), then
`clearSingletons()`. Rewrite the two comments. Re-check that `reload()` is never entered from a
frame of an object being flushed (`Quickshell.reload()` is queued, `wakuc` runs from the socket
slot: both fine).

#### C4 crash-risk, high, S per site
A worker thread evaluates a `QPointer` and then hands the raw pointer to
`QMetaObject::invokeMethod`. `QPointer::isNull()` is an atomic read of the weak block, not a lock:
between the check and `postEvent`, the GUI thread can finish `~QObject`; `postEvent` then reads
`receiver->d_func()->threadData` and writes `postedEvents` into freed memory. The window is
microseconds, but a hot reload deletes every one of these objects while their workers are busy.
Sites:
- `src/features/github/qml/github_service.cpp:52-63` (`fetch`; a fetch takes up to 15 s
  per request, so a reload while GitHub is slow lands inside it).
- `src/features/bluetooth/win32/bluetooth_win.cpp:1357-1361`, `:1385-1394`,
  `:1466-1489`, `:1496-1505` (WinRT `Radio.StateChanged`, `DeviceWatcher.Added/Updated/Removed`,
  `ConnectionStatusChanged` on WinRT pool threads; revoking a token does not stop a delegate already
  running, and `watcher_.Stop()` at `:1437` is asynchronous; the unfiltered watcher of C32 keeps
  thousands of `Updated` callbacks in flight at reload time).
- `src/features/audio/win32/audio_endpoints_win.cpp:133-144`, `:274-284`;
  `src/features/audio/win32/audio_streams_win.cpp:200-212` (Core Audio callback threads;
  mitigated by the blocking `Unregister*` calls in `stop()`, except when an `Unregister` fails with
  `AUDCLNT_E_DEVICE_INVALIDATED`, which is exactly when events fire).
- `src/features/color_scheme/qml/color_scheme_service.cpp:158-174` (`fromImage` pool task,
  50-300 ms after shell start, so a quick second reload lands inside it).
- `src/features/taskbar/taskbar_icons.cpp:81-96` (safe only because `~TaskbarIcons` calls
  `waitForDone()` first; document or convert).
Fix: post to `QCoreApplication::instance()` and dereference the `QPointer` only inside the
GUI-thread lambda. The repository already does this correctly in
`src/features/network/win32/radio_win.cpp:105-113`, `vpn_win.cpp:140-148`,
`src/kernel/qt/polled_source.h:73-81` and
`src/features/wallpapers/qml/wallpaper_transition.cpp:137-141`; or use the mutex-guarded
owner pointer of `src/features/apps/qml/apps_service.cpp:55-59, 186-201`. Put the helper
in the kernel task thread of C58 so the next worker gets it for free.
Gain: removes four reload-time use-after-free windows.

#### C5 crash-risk, high, M
`src/features/notifications/win32/notifications_system.cpp:186-196`, `:213-225`,
`:258-262`; `src/features/notifications/win32/dnd_win.cpp:102-113`, `:147-152`.
Problem: both WNF callbacks run on a system thread with a raw `this`. `stop()` calls
`RtlUnsubscribeWnfStateChangeNotification`, then frees the object. The code and
`docs/architecture.md:345` claim the unsubscribe waits for a running callback; nothing verifies
it, and ntdll exports a separate `RtlUnsubscribeWnfNotificationWaitForCompletion`, which only makes
sense if the plain call does not wait. A callback in flight at reload then does
`std::lock_guard` on a freed `std::mutex` and `invokeMethod` on a freed `QObject`
(`notifications_system.cpp`), or runs `dndStatus()` (an out-of-process COM call, tens of ms) and
then posts to a freed `DndWatcher` (`dnd_win.cpp`). `ControlCenter` destroys its `DndWatcher` on
every reload; Focus Assist changes whenever a full-screen app starts.
Fix: a process-wide `static std::mutex` plus a set of live subscribers (or a `shared_ptr` guard
passed as the WNF context) that the callback locks and looks up before touching anything, and that
`stop()` erases under the same lock, so `stop()` returns only after a running callback has left.
Alternatively resolve `RtlUnsubscribeWnfNotificationWaitForCompletion`, add it to the allowlist
and use it. One `kernel/win32/wnf.h` for both (see C65). Correct `architecture.md:345-346`,
`notifications_system.cpp:7-8`, `dnd.h:19-20`.

#### C6 crash-risk, high, S
`src/features/workspaces/win32/glazewm_socket.cpp:45-55` with `:238`, and `:57-73` against
`:118-138`.
Problem: `stop()` (GUI thread, from `~GlazewmService` on every reload) calls `closeAll()` before
`thread_.join()`, so `WinHttpCloseHandle` runs on the session, connect, request and websocket
handles while the worker is blocked in the synchronous `WinHttpWebSocketReceive` (receive timeout
set to infinite at `:225-226`). WinHTTP documents that a handle must not be closed while another
thread uses it in a synchronous call. With GlazeWM running this races on every reload.
`sendText` copies `handles_.webSocket` under `mutex_`, releases the lock and then sends; `closeSlot`
can close that handle in between and the reconnect loop can reuse the value.
Fix: keep the receive timeout finite (the loop already handles `ERROR_WINHTTP_TIMEOUT` at `:239`);
`stop()` sets `stopping_`, signals the event and joins; the worker closes the handles after its
loop. Hold `mutex_` around `WinHttpWebSocketSend`. Proper fix: WinHTTP async mode.

#### C7 crash-risk, high (conditional on the shell), M
`src/features/taskbar/qml/taskbar_service.cpp:330-341` with `:187` and
`src/features/taskbar/taskbar_icons.cpp:71-98`;
`src/features/tray/qml/tray_image_provider.cpp:8-19` with
`src/features/tray/qml/tray_icon_model.cpp:119-122` against `:79-81`, `:101-103`.
Problem: both providers are `QQuickImageProvider::Image`. Their headers (`taskbar_service.h:149`,
`tray_image_provider.h:12-13`) say they run on the GUI thread. That holds only while the requesting
`Image` has `asynchronous: false`; with `asynchronous: true` (which
`src/host/quickshell/widgets/IconImage.qml:18,44` forwards, and which Quickshell shells set
on tray and launcher icons) Qt calls `requestImage` on the pixmap reader thread. There
`WindowIconProvider::requestImage` calls `iconImage()`, which inserts into `iconCache_`
(`std::map::operator[]`), starts loads (`loaders_`, `pending_`, `pool_.start`,
`QTimer::singleShot(2000, this, ...)`) and reads `pinStore_`, all unlocked, while the GUI thread
erases the same maps in `onRemoved` (`:160-161`). Concurrent `std::map` insert and erase corrupts
the tree. `TrayImageProvider` copies a `QImage` out of `rows_` while `apply`/`remove` reallocate
it. Across a reload the singleton is deleted while a request may be in flight; the `QPointer`
check is not atomic with the use.
Fix: give each provider a `std::shared_ptr` to a mutex-guarded `QHash<key, QImage>` that the
singleton writes on the GUI thread (the `IconIndex`/`ThumbnailStore` pattern); never start loads
from the provider (the model's `compute()` already does). Verify first: grep the config home for
`asynchronous: true`.

#### C8 crash-risk, high, S
`src/kernel/win32/shell_icons.cpp:16-27`, `:132`;
`src/features/apps/qml/app_icon_provider.cpp:79-81`;
`src/features/taskbar/win32/window_system_win.cpp:261`;
`src/features/taskbar/win32/pin_support_win.cpp:232`;
`src/features/apps/win32/start_menu_win.cpp:46`;
`src/features/apps/win32/apps_folder_win.cpp:37`.
Problem: every icon or `.lnk` read does `CoInitializeEx(MTA)` ... `CoUninitialize()` per call on
pool threads (four appicon threads, one taskbar thread, the global-pool scan thread, which exits
after 30 s idle). When the last MTA thread uninitialises, COM tears the MTA down: in-process shell
servers loaded by `IShellItemImageFactory`/`IShellLink` (icon handlers, third-party shell
extensions, WIC codecs) receive `DLL_PROCESS_DETACH` and are reloaded on the next call, possibly
while another pool thread is mid-call. Third-party extensions freeing on detach are a classic
source of heap corruption with no usable dump. It is also slow: a cold apartment per shortcut.
Fix: `CoIncrementMTAUsage` once per process (in `main.cpp` or a kernel `comKeepAlive()`); the
per-call scopes become `S_FALSE` no-ops. Fold into the single `ComScope` of C57.
Gain: a plausible corruption source gone; the Start Menu scan tens to hundreds of ms faster.

#### C9 crash-risk, medium, S
`src/features/tray/hook/trayhook.cpp:184-199`, callers `:216`, `:264`.
Problem: inside Explorer, the DLL writes with a stack `OVERLAPPED`; on the 500 ms timeout it calls
`CancelIo`, `CloseHandle` and returns, and the caller frees `buffer`. Cancellation completes
asynchronously, so the IRP can still write `Internal/InternalHigh` into the dead stack frame and
read the freed buffer. Trigger: the host reads slower than 500 ms (GUI busy, reload in progress)
while a 256x256 icon (263 KB) squeezes through 32 KB pipe buffers (`tray_hook.h:29`). This
corrupts Explorer, not waku, but it happens at the same moment.
Fix: after `CancelIo` call `GetOverlappedResult(h, &ov, &n, TRUE)` before `CloseHandle`; raise the
pipe buffers to 512 KB.

#### C10 crash-risk, medium, S
`src/host/qml/persistent_properties.cpp:17-34`, `:46-56`.
Problem: the process-wide store keeps `QVariant`s that, for `property var`, wrap `QJSValue`s
(persistent V4 values rooted in the engine heap). They are freed in the engine's `destroyed` lambda,
after `~QJSEngine` destroyed the V4 engine, so each `QJSValue` destructor touches freed memory at
shutdown. Between reloads they pin old JS objects as GC roots.
Fix: store `value.value<QJSValue>().toVariant()` and skip anything still a `QJSValue` or `QObject*`.

#### C11 crash-risk, medium, S
`src/host/qml/ipc_handler.cpp:229-231`, `:238-243`.
Problem: for "untyped" parameters a `QVariant*` is passed, but `isUntyped()` also matches
`QMetaType::fromType<QJSValue>()`. If Qt reports a QML `var` parameter or return as `QJSValue`,
the callee reinterprets a `QVariant` as a `QJSValue` and destructs it: a bogus free.
Fix: build every buffer with `QVariant(type)` and pass `.data()`; special-case only
`QMetaType::QVariant`.

#### C12 crash-risk, medium, M
`src/features/taskbar/qml/taskbar_service.cpp:141-153`;
`src/features/taskbar/win32/app_identity_win.cpp:125-168`.
Problem: `onAdded` calls `appUniqueId` on the GUI thread; for Explorer windows that is
`CoCreateInstance(ShellWindows)` plus out-of-process calls. An STA pumps messages during the RPC,
so a SHELLHOOK message or WinEvent is dispatched re-entrantly: a nested `onAdded(hwnd)` pushes the
window, the outer frame pushes it again (`find` ran before the call); `onRemoved` erases one
(`:156-159`), leaving a ghost button. The call also blocks the bar while Explorer restarts (no
RPC timeout).
Fix: resolve `uniqueId` on the icon worker and apply with a queued call; re-check `find(hwnd)`
after any pumping call.

#### C13 crash-risk, medium, S
`src/features/wallpapers/qml/wallpapers_service.cpp:249-252`, `:274-276`;
`src/features/wallpapers/qml/wallpaper_transition.cpp:148-153`, `:238-247`.
Problem: the `finished` lambda runs synchronously inside the transition's own signal emission
(`skip()` emits from `onImagesLoaded`; the animation's `finished` emits too) and calls
`commitWallpaper` -> `refresh()` -> `filesChanged`/`currentChanged`. A handler that re-enters
`setWallpaper` (a second widget, `wakuc`, a QML `onCurrentChanged`) hits `transition_.reset()`,
deleting the emitter mid-emission and the `QQuickWindow` from inside its own
`QVariantAnimation::finished` slot.
Fix: `transition_.release()->deleteLater()` and connect `finished` with `Qt::QueuedConnection`.

#### C14 crash-risk, medium, M
`src/features/workspaces/win32/komorebi_pipe.cpp:31-53` (from `:150`, `:200`).
Problem: `QProcess` is created, started and waited on from a plain `std::thread`. On Windows
`QProcess` needs an event dispatcher (it warns "Can only be used with threads started with
QThread"), and the adopted thread's `QThreadData` is torn down later by Qt's adopted-thread watcher.
Output is read only after `waitForFinished`, so a child that writes more than the pipe buffer
deadlocks until the timeout.
Fix: `CreateProcessW` with anonymous pipes in `win32/`, waiting on `{process, stopEvent_}`, or a
`QThread` with an event loop.

#### C15 crash-risk, medium, M
`src/host/qml/shell_host.cpp:374-394`.
Problem: a dock or undock fires `screenAdded/Removed`, `primaryScreenChanged` and
`logicalDotsPerInchChanged` several times over 1-3 s; with a 600 ms debounce that is two or three
full reloads back to back, each tearing down and recreating five swap chains while Qt relocates
windows off the removed screen. With C3 this is the most plausible "crashed around a reload"
trigger the user did not initiate.
Fix: 1.5-2 s debounce; for DPI and geometry changes call `ScreenList::setScreens` plus
`applyGeometry` instead of a full rebuild.

#### C16 crash-risk, low, S
`src/host/qml/shell_host.cpp:513` (`install`), `:164-167` (`~Loaded`), `:546-556`
(`release`).
Problem: `install()` range-fors `loaded_->entries` (a `std::list`) while `present()` shows
windows; a `shown`/`firstShown` handler that destroys another window (`LazyLoader.active = false`,
today synchronous by C1) reaches `release()`, which erases a node; erasing the current node breaks
the range-for. Safe after C1 (deferred), fragile before.
Fix: iterate a snapshot of `QPointer<QsWindow>`.

#### C17 crash-risk, low, S
`src/features/wallpapers/qml/wallpaper_transition.cpp:130-146`, `:264-273`;
`src/features/wallpapers/win32/workerw.cpp:153-187`.
Problem: the decoder `QThread` is never waited for; a thread still decoding a 4K PNG at exit
outlives `QCoreApplication`. The overlay HWND is a `WS_CHILD` of Explorer's WorkerW for about 2 s
per change; an Explorer restart in that window kills the HWND under Qt.
Fix: keep the thread and `wait()` in `cancel()`/the destructor (or use the pool with a guard);
`SetParent(hwnd, nullptr)` or `IsWindow` in `destroyWindow()` before deleting.

#### C18 crash-risk, low, S
Raw pointers that are correct today only because of the current destruction order:
`src/features/workspaces/qml/workspaces_service.h:65-66` (sibling singletons destroyed in
unspecified order by `clearSingletons`); `src/features/tray/qml/tray_pins.h:50`, `:126`
(`Systray.icons`); `src/features/media/qml/mpris.h:144` (`MprisPlayer::service_` released
before the player children die); `src/features/brightness/win32/brightness_win.cpp:451-453`,
`:585-589`, `:744` (`post()` with raw `this` from the worker and `std::async` jobs; relies on
`stop()` at `:261-270`); `src/host/qml/window_thumbnail.cpp:35` (`release()` emits from
the destructor); `src/host/app_bar.h:27` (raw `QScreen*` in `appBarRequest`, compared at
`shell_host.cpp:685-687` across screen removal).
Fix: `QPointer` or `weak_ptr`, null checks, no emits from destructors.

### Thread safety and reload stalls

#### C19 thread-safety, high, S
`src/features/bluetooth/win32/bluetooth_win.cpp:1567-1575` with `:1272-1282`,
`:1061-1142`, `:571-623`, `:890-896`.
Problem: the destructor runs `invokeMethod(worker_, shutdown, BlockingQueuedConnection)`. If the
worker is inside `pairDevice` the GUI thread blocks up to 60 s: `waitForPairing` checks
`pairCancel_` and `started_`, but the destructor sets neither. The same for `refresh()` phase 2
(`listLeDevices` + `enrichDevices`, 10 s or more with many LE devices) and for `start()`, which runs
a full refresh synchronously, so a reload just after the singleton was created freezes the GUI for
the whole first enumeration. A user who kills the bar then leaves no dump.
Fix: `worker_->cancelPairing()` in the destructor; make `started_` atomic, clear it before the
blocking call and test it inside the `enrichDevices` and `listLeDevices` loops.
Gain: reload stall bounded to one WinRT call.

#### C20 thread-safety, medium, S
`src/features/network/win32/wlan_win.cpp:411-418`, `:334-362`, `:233-271`;
`src/features/network/qml/network_service.cpp:265-272`.
Problem: shutdown blocks while `connectTo` sits in `ConnectAsync().get()` (up to about 30 s).
`scanning_` is reset only by the ACM notification; if neither `scan_complete` nor `scan_fail`
arrives, `scan()` returns at once forever and `Network::scanning_` stays true.
Fix: `wait_for` in steps against an atomic stop flag set by the destructor; a 10 s single-shot
scan timeout on the worker.

#### C21 thread-safety, medium, M
`src/features/workspaces/win32/komorebi_pipe.cpp:103-115`;
`src/features/workspaces/qml/komorebi_client.cpp:94`; `komorebi.h:62`.
Problem: `~Komorebi` -> `stop()` -> `thread_.join()` on the GUI thread while the worker may be in
`runKomorebic("subscribe-pipe")` with a 10 s timeout or `state` with 2 s; then `unsubscribe-pipe`
runs synchronously on the GUI thread with a 2 s timeout. A reload can freeze the bar for 10 s.
The sample shell creates this singleton even without komorebi installed (C39).
Fix: abortable spawn (C14), `unsubscribe-pipe` detached through `startDetachedHidden`, join from
a pool task.

#### C22 thread-safety, medium, S
`src/features/apps/qml/app_icon_provider.cpp:101`;
`src/features/taskbar/taskbar_icons.cpp:18`; `src/host/qml/thumbnails.cpp:109`,
`:145-148`; `src/host/qml/thumbnails.h:20-22`.
Problem: the destructors call `waitForDone()` without `clear()`, so every queued runnable runs
first: a launcher page that just asked for 100 icons, N window icons at up to 400 ms of
`SendMessageTimeout` each, or a 4K PNG decode (no scaled read for PNG,
`thumbnail_store.cpp:113-129`, 0.3-1 s), all on the GUI thread during `clearSingletons()`. The
`thumbnails.h` comment says `requestImageResponse` runs on the GUI thread; Qt calls it on the
pixmap reader thread, so when the provider is replaced after a reload (`thumbnails.cpp:154`) the
old provider's `waitForDone()` can run on the reader thread and stall every image load.
Fix: `pool_.clear(); pool_.waitForDone();` in all three, a cancel flag checked by `LoadTask` and
`WarmTask`, correct the comment. Fold into the shared provider base of C59.
Gain: reload stalls of seconds down to one task.

#### C23 thread-safety, medium, S-M
`src/features/tray/win32/tray_hook.cpp:196`, `:66-69`, `:250`, `:320-338`, `:370-391`,
`:140-143`; `src/features/tray/win32/tray_win.cpp:111-112`;
`src/features/tray/hook/trayhook.cpp:387-409`.
Problem: `stop()` joins while the worker may be inside `inject()` (60 x 50 ms looking for
`Shell_TrayWnd`) or `explorerPid()` (10 x 50 ms): up to 3.5 s GUI freeze on reload or exit while
Explorer restarts. After `stop()` releases the watchdog mutex the DLL still needs Explorer's tray
thread to process `WM_WAKU_UNHOOK` (up to 5 s). The new `SystrayHook` races it: `isHookLoaded` is
still true, so `ensureInjected` returns without injecting and `connectPipe` waits with no timeout
for a client that never connects (tray empty until the next reload); or `CreateNamedPipeW` with
`nMaxInstances = 1` fails with `ERROR_PIPE_BUSY` while the DLL's client handle is open ("another
tray host is running").
Fix: `waitStop()`-based loops; bound the connect wait (5 s) and loop back to `ensureInjected`;
create the pipe in the worker with retry or `PIPE_UNLIMITED_INSTANCES`.

#### C24 thread-safety, medium, S
`src/features/network/win32/vpn_win.cpp:76`, `:91-94`;
`src/features/network/win32/connectivity_win.cpp:85`;
`src/features/network/win32/radio_win.cpp:62`; `src/kernel/qt/polled_source.h:65`.
Problem: all network work runs on the global `QThreadPool`: `RasDialW` blocks for the whole dial
(tens of seconds), `hangUp` sleeps up to 4 s, `getaddrinfo` can block on DNS for seconds when a VPN
comes up. The pool is shared with thumbnails, apps, color_scheme and github; one blocked task
starves them, and `sampleAsync` does not overlap, so the status freezes meanwhile.
Fix: a dedicated one or two thread pool for network; `GetAdaptersAddresses` or the connection
profile instead of `getaddrinfo`.

#### C25 thread-safety, low-medium, S-M
`src/features/notifications/win32/toast_activation_win.cpp:76-110`,
`notifications_system.cpp:186-196`; `src/features/media/win32/media_session_winrt.cpp:54-68`,
`:80-89`, `:155-176`, `:181-199`, `:277`.
Problem: toast activation walks every Start Menu `.lnk` and then `CoCreateInstance(CLSCTX_LOCAL_SERVER)`
+ `Activate` (seconds while the app launches); `stop()` joins on the GUI thread at reload, so a
reload during an activation freezes the bar. Same shape for `TryGetMediaPropertiesAsync().get()`
and the thumbnail download with a frozen player. In the media worker `stop()` is permanent
(`stopping` never resets, so a `start()` after the `Q_INVOKABLE stop()` spawns a thread that exits
at once) and `unbind()` can throw for a dead session, aborting that `refresh`.
Fix: a stop flag checked between steps, or detach at reload and join at exit (the
`shared_ptr<Queue>` design allows it); reset `stopping` in `start()`; try/catch around `unbind`.

#### C26 thread-safety, medium, S
`src/features/media/qml/media_service.cpp:22-28`; `src/features/media/qml/mpris.cpp:165`.
Problem: no `kernel::liveDesktop()` check: the GSMTC worker, `RequestAsync()` and the source-app
resolver start on the offscreen platform and in every test, unlike notifications
(`notification_service.cpp:19-24`). This breaks the live-desktop rule of `AGENTS.md`.
Fix: a `systemSource()` that returns null off the live desktop; `Media.available` false.

### Memory

#### C27 memory, high, S (knobs) / M (measurement and shells)
`src/host/qml/shell_window.cpp:56-60` (a `QQuickWindow` per `QsWindow`);
`src/app/waku/main.cpp:36` (`setDefaultAlphaBuffer(true)`; no graphics configuration,
no render loop choice).
Problem: five visible full-screen transparent windows. With Qt's default threaded render loop on
Windows each window owns its own render thread and its own `QRhi` (a D3D11 device, pipeline cache,
staging buffers, glyph cache atlases), plus a flip swap chain with two BGRA8 buffers and a
depth-stencil buffer at physical resolution (`QQuickGraphicsConfiguration::depthBufferFor2D`
defaults to true). At 3840x2400 one buffer is 36.9 MB, so 100-150 MB per window and 500-700 MB for
five. On a UMA GPU these are system memory; whether the private working set counts them is the
first thing to measure (see the profiling plan). Independent of that, five D3D11 devices and five
glyph caches are pure waste for a bar. `ClippingRectangle` (C30) adds two layer textures per
instance on top.
Fix (knobs, all in `main.cpp` and `ProxiedWindow`): `QQuickGraphicsConfiguration cfg;
cfg.setDepthBufferFor2D(false); window->setGraphicsConfiguration(cfg)` (or depth 0 on the default
`QSurfaceFormat`); `qputenv("QSG_RENDER_LOOP", "basic")` unless set (one `QRhi` shared by every
window, one render thread fewer per window; measure animation smoothness after);
`setPersistentGraphics(false)` and `setPersistentSceneGraph(false)` on `ProxiedWindow` so a hidden
overlay frees its swap chain and scene graph. Document `LazyLoader` + `visible: false` as the
overlay pattern; a window that is shown but fully transparent costs the same as a visible one.
Gain: depth buffer off saves 37 MB per window at 4K; basic loop removes four devices and glyph
caches (estimated 20-60 MB plus four threads); a hidden overlay saves about 100 MB each.

#### C28 memory, high, S (retention) / M (peak)
`src/features/wallpapers/qml/wallpaper_transition.h:131-132`;
`src/features/wallpapers/qml/wallpapers_service.h:166`;
`src/features/wallpapers/qml/wallpaper_transition.cpp:99-105`, `:130-143`, `:162-163`,
`:213-214`.
Problem: after a transition the `WallpaperTransition` stays in `transition_` until the next change,
and `destroyWindow()` frees the window but not `scaledNew_`/`scaledOld_`: two laid-out screen-size
pixmaps per monitor, 2 x 33 MB per monitor at 4K, held indefinitely (66 MB single, 132 MB dual).
Peak during one change: two full decodes (66 MB) captured by value in the posted functor and again
as parameters, two `QPixmap::fromImage` copies (66 MB), two smooth-scaled layouts per monitor
(66 MB per monitor), plus the 4K `QQuickPaintedItem` backing image and texture (33 MB each):
230-300 MB for one monitor. A spike like that leaves the private working set high if the allocator
keeps the pages.
Fix: clear the three vectors in `destroyWindow()` and `deleteLater()` the transition after the
linger; decode with `QImageReader::setScaledSize` to the virtual-desktop size on the worker; move
instead of copy; drop the `QPixmap` stage (paint `QImage`s or build `QSGTexture`s).
Gain: 66-132 MB retained; 130-200 MB peak; 100-300 ms less GUI stall per change.

#### C29 memory, medium, S (store) / M (gallery)
`src/host/qml/thumbnails.cpp:150-156`; `src/host/thumbnail_store.h:34`
(`kMemoryBytes` 64 MB); `src/features/wallpapers/thumbnail_cache.cpp:41-57`;
`src/features/wallpapers/qml/wallpapers_service.cpp:449-479`.
Problem: each `Thumbnails::create` makes a new 64 MB LRU and a new provider; the previous provider
holds the previous store until the engine drops it, so two 64 MB caches coexist after a reload,
plus `QQuickPixmapCache` copies of what is on screen and the GPU textures. The wallpapers feature
has a second, older thumbnail system: PNG only, no memory LRU, and its `prune()` is called from no
product code (only `tests/test_wallpaper_manager.cpp:183`), so the 100 MB cap is dead and the
folder grows forever; every gallery open re-decodes the PNGs on the one image-reader thread.
Fix: share one `ThumbnailStore` process-wide through a `std::weak_ptr` (the hub pattern) and drop
the memory cap to 16-24 MB (the WebP disk cache is cheap to re-read); delete
`wallpapers::ThumbnailCache` and `WallpaperThumbnailProvider` and let the gallery use
`image://thumb/<path>?w=`.
Gain: up to 64-100 MB after a reload; the gallery's first open seconds faster.

#### C30 memory and perf, medium, S
`src/host/quickshell/widgets/ClippingRectangle.qml:39-72`;
`src/host/qml/WindowChrome.qml:15-23`.
Problem: every `ClippingRectangle` allocates two layer textures of its own size (the clipped
content layer and the mask layer) plus the `MultiEffect` pass. A shell that wraps panels or a
full-screen container in one pays full-screen BGRA8 textures per instance (37 MB each at 4K).
`WindowChrome` draws a full-window `Rectangle` under every window even when its colour is
transparent: a full-screen quad through the fragment shader per frame on five windows.
Fix: document the cost in `ClippingRectangle` (already partly there) and offer a cheaper path when
only `radius` is set (`Rectangle { clip: true }` with `layer` off is wrong for rounded corners,
but a single `layer.effect` on the content with the mask drawn by a shader avoids the second
layer); `visible: color.a > 0` on the chrome background.

#### C31 memory, medium, S
`src/host/qml/shell_host.cpp:143-159` (`GenerationInterceptor`), `:470-471`.
Problem: the generation query makes every config-home QML file a new URL and therefore a new
composite type per reload. `clearComponentCache()` runs while the old windows (C3) still reference
generation N-1, so that generation's compilation units, property caches and `QQmlMetaType` URL
types stay resident until the next reload: two full generations of type data between reloads.
Fix: after C3 (deferred deletes flushed before the clear) this collapses to one generation; also
queue `engine_->trimComponentCache()` one pass after `install()`. Confirm with the reload loop of
the profiling plan: the working set must plateau after the second reload.
Gain: a few to tens of MB per reload, depending on shell size.

#### C32 memory and perf, medium, M
`src/features/bluetooth/win32/bluetooth_win.cpp:1384`, `:1397-1421`.
Problem: `DeviceInformation::CreateWatcher()` with no selector watches every device interface in
the PnP tree (1,500-4,000 objects): a full enumeration at every creation (1-3 s of WinRT thread
CPU), an in-process snapshot of all entries kept for diffing (several MB), and an `Updated`
callback for every property change of any device in the system, each allocating and lower-casing
a `QString`. This is what keeps WinRT threads busy during a reload (C4).
Fix: `CreateWatcher(BluetoothAdapter::GetDeviceSelector())` for the adapter and
`BluetoothDevice::GetDeviceSelectorFromPairingState(true)` plus the LE counterpart for devices.
Gain: 5-20 MB, most of the background callback CPU, faster first snapshot.

#### C33 memory and perf, medium, M
`src/features/system_stats/qml/stat_sources.cpp:8`, `:20`, `:42`, `:54`, `:76`;
`src/features/system_stats/win32/pdh.cpp:57-69`, `:103-114`;
`src/features/system_stats/win32/gpu.cpp:47-59`, `:63-78`, `:95-108`, `:112-115`,
`:150-172`.
Problem: every QML `Cpu {}`/`Gpu {}`/`Memory {}` owns its own data source, so a three-monitor shell
holds three PDH CPU queries (N per-core counters each plus a thermal query) and three GPU wildcard
queries, each sampled every 2 s. The GPU source opens a second `GPU Engine(*)` wildcard query every
20 samples, allocates a buffer of one item per process x engine twice per sample, builds a
`std::string` per instance to regroup, and opens and closes every adapter
(`D3DKMTOpenAdapterFromLuid`) per sample to read a temperature.
Fix: one process-wide source per kind through a `weak_ptr` cache; keep the engine key extraction on
`wchar_t`; open adapters once.
Gain: 1-4 MB per extra widget and a few ms of pool CPU per 2 s.

#### C34 memory, low, S each
- `src/features/tray/tray_icon_registry.h:64-67`, `tray_state.cpp:100`,
  `tray_icon_model.h:61-66`: each tray icon's raw RGBA (up to 256 KB) is stored twice; the registry
  copy is never used for pixels. Keep a hash for the "pixels changed" test (`tray_icon_model.cpp:86-88`).
  Typical 3-5 MB, up to 15 MB.
- `src/features/taskbar/qml/taskbar_service.cpp:186-212`, `taskbar_icons.h:48-51`: the same
  32x32 icon cached twice and round-tripped `QImage` -> `QPixmap` -> `QImage`.
- `src/features/media/qml/media_service.cpp:16-20`, `:49-56`, `mpris.cpp:58-61`: album art
  as bytes in the session state, in `Media`, in each `MprisPlayer`, plus a base64 `QString` per
  holder (2.7x in UTF-16). An `image://mediaart/` provider removes the copies.
- `src/features/quickshell_io/qml/data_stream_parsers.cpp:13-16`, `:28-38`: `StdioCollector`
  with `waitForEnd: false` appends to an unbounded buffer and re-decodes the whole buffer on every
  chunk (quadratic); a chatty long-lived process grows without bound. Decode incrementally
  (`QStringDecoder`) and cap.
- `src/features/quickshell_io/qml/file_view.h:156`, `file_view.cpp:322-330`: a
  `QFileSystemWatcher` (and on Windows a watcher thread per 63 handles) per `FileView`; one shared
  watcher (two users with `ShellHost`, kernel candidate).
- `src/features/workspaces/komorebi.h:77-78`, `komorebi_pipe.cpp:216`, `:124-127`: a 512 KB
  zero-filled chunk per connection and a 64 MB message cap; 64 KB with `ERROR_MORE_DATA` suffices.
- `src/features/color_scheme/image_source.cpp:21-40`,
  `color_scheme_service.cpp:191-202`: PNG/BMP/WebP are decoded at full size (33 MB at 4K) and then
  scaled; `luminance()` does it synchronously on the GUI thread (150-400 ms on first run) and
  `seedOfFile` decodes the same file a second time. Compute both from one decode in the pool task.

### Performance: startup

#### C35 perf, high, S
`src/host/qml/shell_host.cpp:264-296` (`validate`), called at `:424-426`; `:298-301`
(`validateTheme`, with `create`); `:303-320` (`applyTheme`).
Problem: every reload and the first start builds a throwaway `QQmlEngine` (import database scan of
every import path, V4 engine) and compiles the entire shell tree; `validateTheme` does it again
with `create`; then `applyTheme` compiles theme.qml a third time and `build()` compiles the shell a
second time in the real engine. On first start there is no old shell to protect: a failed build
already shows the fallback. The probe also leaves 20-60 MB of freed blocks that fragment the heap.
Fix: skip `validate()` and `validateTheme()` when `!loaded_`; on hot reload keep them, or keep one
long-lived probe engine and `clearComponentCache()` it between checks.
Gain: estimated 0.3-0.8 s of the 2.5 s start; about 0.5 s per reload.

#### C36 perf, medium, S
`src/host/plugin_guard.cpp:137-153`, `:165-178`, `:193-208`;
`src/host/qml/shell_host.cpp:709-744`, `:728-732`; `src/app/waku/main.cpp:93-98`.
Problem: three recursive walks of the config home at start (`usableImportRoots`,
`configPluginFailures` to depth 8 reading every qmldir and every plugin DLL's metadata,
`scanQmlFiles`, then `refreshWatches` which removes and re-adds every path), one plugin walk per
reload, and a full stat walk on every `directoryChanged`/`fileChanged` event: a shell that saves a
state file in its config home triggers a full rescan on the GUI thread per write. The watcher is
registered on every QML file and every directory.
Fix: one walk that yields both the stamp map and the qmldir list; cache plugin verdicts by
(path, mtime, size); watch directories only (the stamp map already says which file changed);
run `watch()` one pass after the first frame.
Gain: tens of ms per event; 50-150 ms at start with a large shell.

#### C37 perf, medium, S-M per feature
Singleton constructors that run synchronously inside the first `build()` and every reload:
- `src/features/control_center/qml/control_center_service.cpp:67-80`, `:107-114`,
  `:204-221`; `dnd_win.cpp:57-71`; `user_info_win.cpp:78-85`; `brightness_win.cpp:658-663`:
  `dndStatus()` (CoInitialize plus an out-of-process `CoCreateInstance`, 10-100 ms, more when the
  server must start), `GetUserNameExW(NameDisplay)` (hundreds of ms cold), the avatar registry walk
  with a `QFileInfo` per value, `powerPlans()` per scheme, `startPolicyWatcher()` waiting for its
  window thread; `toggleDnd`/`cycleDnd` re-query `dndStatus()` although `dnd_` is current.
- `src/features/bluetooth/win32/bluetooth_win.cpp:1636`: `hasBluetoothAdapter()` is an RPC
  into bthserv on the GUI thread.
- `src/features/github/qml/github_service.cpp:81-101`: `loadToken()` (`appDataDir()` ->
  `mkpath` + first-time migration + file read).
- `src/features/apps/qml/apps_service.cpp:52`, `:272-278`;
  `apps_folder_win.cpp:35-66`; `catalog.cpp:106-118`; `apps_model.cpp:16-22`: every reload does a
  full rescan (every `.lnk` through `IShellLink`, full `shell:AppsFolder`, 0.5-3 s of COM on the
  pool), re-registers a `QFileSystemWatcher` on every Start Menu subfolder, and resets every
  launcher list twice per scan.
- `src/features/wallpapers/qml/wallpapers_service.cpp:212`, `:298`;
  `wallpaper_manager.cpp:116`; `wallpaper_files.cpp:13-31`, `:59-69`: one wallpaper change lists the
  folder recursively two or three times on the GUI thread with a `StrCmpLogicalW` sort and a stat
  per file; `WallpaperSettings::activate()` lists at start.
- `src/features/taskbar/qml/taskbar_service.cpp` `enumerateExisting` + `appUniqueId` per
  window (C12, C42): 0.15-0.5 s.
Fix: do the reads on a pool task and publish through `changed`; hold the app catalogue in a
process-wide `weak_ptr` hub so a reload reuses it; list the wallpaper folder once per change behind
a watcher; read the token in the first `refresh()`.
Gain: 150-400 ms off first paint and off every reload.

#### C38 perf, medium, S
`src/host/win32/taskbar_control.cpp:105-113`, `:127`, `:136-138`;
`src/host/taskbar_control.cpp:144-151`.
Problem: with `--hide-taskbar`, `waitForFullWorkArea` busy-waits `Sleep(20)` x 25 (0.5 s) on the
GUI thread before the first reload, and a 1 s `WM_TIMER` then does `ABM_GETSTATE` (a
`SendMessage` into Explorer; a hung Explorer blocks our GUI thread), `FindWindowW` and
`EnumWindows` for the life of the run.
Fix: react to `TaskbarCreated` and `WM_SETTINGCHANGE` with `SPI_SETWORKAREA` (broadcast when the
work area changes) instead of the timer; replace the sleep with a timer-driven recheck, or
register app bars first and re-query from the recheck.
Gain: up to 0.5 s at start when the flag is used; one wake per second removed.

#### C39 perf, medium, S-M
`src/features/workspaces/qml/workspaces_service.cpp:76-80`;
`komorebi_pipe.cpp:17-29`, `:145-166`; `glazewm_socket.cpp:148-180`.
Problem: `Workspaces::create` instantiates both `Komorebi` and `GlazeWM`, so every shell with a
`Workspaces` widget starts two listener threads that run forever on a machine running neither: a
Toolhelp process snapshot every 5 s and a WinHTTP connect to `localhost:6123` every 4 s.
Fix: probe once (process present, port listening) before starting a listener, back off
exponentially, or let the shell name its window manager.

#### C40 perf, low, S each
- `src/features/clock/qml/Clock.qml:57-127`: every `Clock` instantiates its
  `PopupWindow` with the full `Calendar` grid (42 day cells) eagerly, per bar. Wrap in `LazyLoader`.
- `src/kernel/qt/app_data.cpp:65-77`: `appDataDir()` runs `mkpath` under a mutex on every
  call (13 call sites, several per sample). `std::call_once`.
- `src/host/qml/shell_host.cpp:199`: an event filter on `QCoreApplication` sees every
  event in the process to catch presses on shell windows. Install it on the backing windows when
  they are presented.
- `src/host/qml/shell_host.cpp:183`: the chrome component compile in the constructor is
  needed for type registration; fine, but note it in the startup trace.

### Performance: steady state on the GUI thread

#### C41 perf, medium, S (cache) / M (raw input)
`src/host/mask_poller.cpp:47-74`, `:92`, `:122-139`; `src/host/qml/region.cpp:38-65`;
`src/host/qml/shell_window.cpp:144-153`, `:178`.
Problem: while the cursor moves anywhere on the desktop the hub ticks at 120 Hz and every
registered poller calls `mask_()`, which rebuilds the whole `RegionNode` tree (`QList`
allocations per child, `mapRectToScene` per `item` region) on every tick: five masked windows at
120 Hz are 600 tree builds per second of garbage. `refresh()` builds the tree only to test
`has_value()`, and `setMask` triggers it twice. When the cursor rests the 33 ms timer still wakes
the GUI thread 30 times a second forever. `frameSwapped` is emitted on the render thread, so
`markDirty` is a queued cross-thread event per frame per masked window.
Fix: cache the `RegionNode` in `MaskPoller` and rebuild only when `dirty_` or the geometry changed;
a cheap `hasMask` source; connect `afterAnimating` (GUI thread, once per frame) instead of
`frameSwapped`. Then replace the poll with raw input: `RegisterRawInputDevices(RIDEV_INPUTSINK)` on
the taskbar watcher window gives `WM_INPUT` on every mouse move with no timer at rest and no
system-wide hook latency; keep the 1 s safety recheck.
Gain: measurable CPU while the mouse moves (600 allocations/s removed); zero wakeups at rest.

#### C42 perf, medium, S-M
`src/features/taskbar/window_tracker.cpp:162-167`, `:312-331`, `:360-378`, `:200-237`,
`:380-395`; `window_tracker_hooks_win.cpp:78`; `window_system_win.cpp:174-206`, `:113-129`;
`taskbar_service.cpp:152`, `:162`, `:174`, `:181`, `:227`, `:307`; `taskbar_model.cpp:167-247`;
`taskbar_icons.cpp:100-109`; `pin_support_win.cpp:455-483`, `:504-513`, `:520-527`, `:589-623`.
Problem: the `EVENT_OBJECT_SHOW/HIDE` hook is system-wide and fires for every HWND (tooltips,
popups, child controls); each event allocates a `QSingleShotTimer` and runs the full `query()`
(`GetWindowText`, DWM, `OpenProcess`, `QueryFullProcessImageName`) before the cheap
`GetAncestor(GA_ROOT) == hwnd` test. `updateWindow` queries four times per event;
`isGhostUwpApp` does `CoCreateInstance(VirtualDesktopManager)` per call; `debounceUpdate`
allocates a `QTimer` per title change. `changed()` is emitted per tracker event with no coalescing,
and every `TaskbarModel` (one per widget per screen) rebuilds all rows; `enumerateExisting` at
start is O(N^2). A window that never answers `WM_GETICON` is retried every 2 s forever (2 x 200 ms
`SendMessageTimeout` each) on the single loader thread. `resolveMetadata` on a pin click walks the
entire Start Menu up to three times (1-3 s GUI freeze).
Fix: pre-filter in `handleWinEvent` (root, visible, not `WS_EX_TOOLWINDOW`); query once and pass
`WindowInfo`; cache the manager; one coalescing timer; a zero-timer flag in `TaskbarService`;
exponential back-off for icons; resolve pin metadata on the worker or from `AppsService`.
Gain: about ten times fewer queries on the GUI thread; fewer `dataChanged` bursts.

#### C43 perf, medium, S-M
`src/features/tray/qml/tray_icon_model.cpp:13-24`, `:70-96`;
`src/features/tray/icon_scale.cpp:135-172`; `src/features/tray/qml/systray.cpp:176-183`;
`tray_win.cpp:47-50`; `tray_hook.cpp:293`.
Problem: Lanczos-3 resample plus sharpen runs on the GUI thread for every `NIM_MODIFY`; animated
icons (Teams, Slack, VPN activity) modify several times a second with up to 256 px bitmaps (1-3 ms
each plus a 256 KB compare). Every pipe reconnect broadcasts `TaskbarCreated` to all windows, so
every tray app re-adds its icon: a burst of `CopyIcon` + `GetDIBits` + 500 ms-bounded pipe writes on
Explorer's UI thread (C9), which can time out and disconnect again.
Fix: scale on the hook worker in `handleMessage` and ship the 32 px image; skip the broadcast when
the registry is populated and only the DLL reconnected (same Explorer PID).

#### C44 perf, medium, M
`src/features/audio/win32/audio_streams_win.cpp:265-322`, `:235-263`;
`src/features/audio/qml/audio_streams.cpp:101-119`;
`src/features/audio/win32/audio_endpoints_win.cpp:164`, `:257-260`, `:95-98`, `:294-311`.
Problem: `rebuild()` unregisters and re-registers every session's `IAudioSessionEvents` on any
structural event; `OnStateChanged` fires whenever any session goes active or inactive (every
notification sound), so this runs constantly on the GUI thread, about six COM round trips per
session, and `sync()` re-reads name, icon, process image, volume, mute and state per entry. With no
default endpoint every property read (five per `changed`) calls `resolveEndpoint()`, an RPC;
`OnDeviceStateChanged` passes fixed `eRender/eConsole`, so `AudioInput` reacts to render-device
changes and both singletons react to non-default devices.
Fix: diff by session instance id and register only new sessions; cache name, icon and image; on
kind 0 re-read only volume and mute; remember "no endpoint" until the next device event; filter
by `flow_` and the default id.
Gain: 20-60 COM calls per event down to 2-5.

#### C45 perf, medium, S-M
`src/features/network/qml/network_service.cpp:71-76`, `:78-87`, `:139-147`;
`wlan_win.cpp:165-191`, `:461-467`; `radio_win.cpp:27-33`; `vpn_win.cpp:43-64`;
`connectivity_win.cpp:76-94`; `throughput_win.cpp:29-47`.
Problem: every poll (2 s default) launches three pool tasks: `GetConnectionProfiles` +
`getaddrinfo(hostname)` with `WSAStartup`/`WSACleanup` each time, plus `currentSignalQuality()`
which opens and closes a WLAN handle, calls `WlanQueryInterface` and never reads the result, then
`WlanGetAvailableNetworkList` and `WlanGetProfile` plus a regex to get a quality that is already in
`WLAN_CONNECTION_ATTRIBUTES`; `Radio::GetRadiosAsync().get()`, a WinRT device enumeration;
`RasEnumEntriesW` + `RasEnumConnectionsW`, which parse the phonebook files. The throughput timer
copies the full interface table (`GetIfTable2`, 50-150 rows with virtual adapters) every second and
rebuilds a `std::map`, QString names and a `QVariantList`.
Fix: read quality from the `WlanQueryInterface` result; keep one session; poll radio and VPN at a
tenth of the rate or on events (`Radio.StateChanged`, `RasConnectionNotificationW`);
`GetIfTable2Ex(MibIfTableNormal)` and reuse names.
Gain: 5-15 ms CPU and hundreds of allocations per poll.

#### C46 perf, medium, S
`src/features/bluetooth/win32/bluetooth_win.cpp:1582-1585`, `:1220-1224`, `:378-429`;
`src/features/bluetooth/qml/bluetooth_service.cpp:43`, `:49`, `:112`, `:135`.
Problem: `RealBluetoothBackend::refresh()` calls `onRadioChanged`, which sets `refreshLePending_`,
so every service-initiated refresh (the 60 s safety timer, every connect, disconnect and pairing
end, every start-up retry at 1/3/8/20 s) is a full one: `FromIdAsync` per LE device,
`FromBluetoothAddressAsync` per device, three `CM_Get_Device_ID_List` over whole enumerators per
connected device for the battery. Seconds of worker CPU per minute while a widget is bound.
Fix: a `refreshPaired()` that leaves `refreshLePending_` alone; relist LE only on
`DeviceAdded/Removed`.

#### C47 perf, medium, M
`src/features/workspaces/qml/komorebi_client.cpp:45-76`, `:107`;
`workspaces_service.cpp:88-110`; `Workspaces.qml:29-33`, `:86-89`; `glazewm_state.cpp:41-60`, `:70`.
Problem: every komorebi event (each focus change) builds a `QVariantList` of maps,
`Workspaces::rebuild` converts it into a second `QVariantList`, QML deep-copies that into JS arrays
for every bar, and `Repeater { model: root.monitor.workspaces }` sees a new array each time and
destroys and recreates all delegates (so `Behavior on color` never animates). GlazeWM parses every
window of every `query monitors` reply into `std::vector<Window>` and never reads it.
Fix: per-monitor change signals and a `QAbstractListModel` (or a diff into a `ListModel`); drop
the intermediate copy and the dead window parsing.

#### C48 perf, medium, S
`src/features/media/media_session.cpp:103-135`;
`src/features/media/win32/media_session_winrt.cpp:116-117`;
`src/features/notifications/win32/notifications_system.cpp:363-365`;
`toast_xml.cpp:224-260`.
Problem: `applySessions` emits `mediaDataChanged` once per session inside the loop and again after
it; each emission runs `Mpris::sync()` (every player's `update()`, up to 19 signals each) and
`Media::onServiceChanged()` (art compare plus 12 bindings). While playing, the 100 ms tick drives
the same path, so all 12 `Media` bindings re-evaluate ten times a second because they share one
`changed`. `refresh(false); refresh(true);` at thread start reads every session twice including
two thumbnail downloads per session. The toast sort comparator makes two WinRT calls per
comparison; `downloadedImage` enumerates the `wpnidm` registry key per web image.
Fix: emit once after the loop; a separate `positionChanged`; `sync()` skips non-position work on
ticks; one `refresh(true)`; decorate-sort and cache the lookups.

#### C49 perf, medium, M
`src/features/wallpapers/win32/desktop_wallpaper_win.cpp:54-71`, `:73-106`;
`wallpaper_transition.cpp:58-88`, `:162-163`, `:213-214`; `WallpaperGallery.qml:146`;
`wallpapers_service.cpp:206-230`.
Problem: `IDesktopWallpaper::SetWallpaper` runs on the GUI thread and Windows transcodes the image
synchronously (100-500 ms at 4K) exactly when the overlay should look seamless; `readCurrent()`
creates the COM object per `refresh()`. Two `QPixmap::fromImage` and two smooth 4K scalings on
the GUI thread when the animation starts (100-300 ms), then a 4K `QQuickPaintedItem` is repainted
with antialiased clip paths and re-uploaded as a 33 MB texture every frame for 1.2 s. `files` is a
`QVariantList` rebuilt on every `refresh()`, so the gallery destroys and recreates all delegates
and re-requests every visible thumbnail.
Fix: set the wallpaper on a pool task (STA per task); scale on the worker; render with two `Image`
items and a shader so only uniforms change per frame; a `QAbstractListModel` with per-row
`dataChanged` for `current`.

#### C50 perf, low, S each
- `src/features/control_center/qml/control_center_service.cpp:94-98`, `:244-249`,
  `:151-161`: a slider move emits `changed` twice and `changed` is the NOTIFY of 15 properties, so
  QML rebuilds `powerPlans`, `powerModes` and `brightnessSources` (locking the brightness mutex per
  monitor) twice per tick. Per-group signals.
- `src/features/desktop_state/qml/desktop_state.cpp:19-24`, `:69-96`;
  `desktop_probe_win.cpp:32-51`: the gating works, but QML bindings keep the connection alive, so
  the 200 ms tick runs until the next reload, doing `LCIDToLocaleName` + 2 x `GetLocaleInfoEx` + 3
  `QString` allocations per tick. Cache by `HKL`; poll idle seconds at 1 s.
- `src/host/qml/persistent_properties.cpp:89-95`, `:68-71`: a full snapshot per notify of
  any declared property; an animated value churns at frame rate. Coalesce with a zero-timer.
- `src/host/qml/script_model.cpp:67-101`: `setValues` is quadratic and copies every
  `QVariant` twice; bad for a 1000-entry launcher list. Index keys in a `QHash`.
- `src/features/github/github_api.cpp:36-54`: a fresh `QNetworkAccessManager` (own HTTP
  thread, TLS context) and a nested `QEventLoop` per `send()`, 2-3 per poll, full TLS handshake
  each time. One manager per transport.
- `src/features/control_center/win32/touch_keyboard_win.cpp:43-60`:
  `WaitForInputIdle(..., 5000)` on the GUI thread when TabTip must be launched.
- `src/features/quickshell_io/qml/qs_process.cpp:12-20`;
  `src/features/shell/qml/child_process.cpp:17-23`: destructors `waitForFinished(500)` on
  the GUI thread per process object at reload. Reparent to `qApp`, `finished` -> `deleteLater`.
- `src/features/control_center/win32/power_win.cpp:42-45`: `LoadLibraryW("powrprof.dll")`
  for a module already linked; `GetModuleHandleW`.

### Style and consistency against AGENTS.md

#### C51 style, low, S
`QLatin1String(` appears 219 times in product code (no `u""_s`, no `m_` members, no include that
is not rooted at `src/`, no const `json::operator[]` were found). Heaviest files:
`src/app/wakuc/main.cpp:126-178`, `src/features/github/github_api.cpp:80-497`,
`github_format.cpp:59-86`, `src/features/notifications/toast_xml.cpp` (12),
`toast_activation_win.cpp` (8), `src/host/qml/control_handler.cpp` (19),
`surface_control.cpp` (8), `src/host/hotkeys.cpp` (6), `bluetooth_win.cpp:482-1478`,
`control_center_service.cpp:98-305`, `wlan_win.cpp:114`, `dnd_win.cpp:76`,
`taskbar/window_tracker.cpp:98-100`, `app_identity_win.cpp`, `pin_support_win.cpp`,
`wallpaper_layout.cpp`, `wallpaper_transition.cpp:23-25`, `wallpapers_service.cpp:402-404`.
Most are `==`/`contains` comparisons, which `AGENTS.md` allows as "an API that needs a Latin-1
view" only by a generous reading. Decide once: amend the rule to allow comparison against a
Latin-1 literal, or switch to `QStringLiteral` constants. Do not do it file by file.

#### C52 style, low, S
Mixed line endings (`git ls-files --eol` reports `i/mixed`) against `.editorconfig`:
`src/features/audio/tests/test_audio_streams.cpp`,
`src/features/audio/win32/audio_streams_win.cpp`,
`src/features/desktop_state/tests/test_desktop_state.cpp`,
`src/features/notifications/tests/test_notification_feed.cpp`,
`src/host/testing/widget_fixtures.h`, `src/host/tests/test_global_shortcut.cpp`,
`src/host/tests/test_region_mask.cpp`. Normalise in one commit listed in
`.git-blame-ignore-revs`; have `tools\check.ps1` fail on `i/mixed`.

#### C53 style, low, S each
- Stale lifetime comments that mislead the next reader: `shell_host.cpp:427-428`,
  `shell_screen.cpp:34-35` (C3), `thumbnails.h:20-22` (C22), `docs/architecture.md:345-346`,
  `notifications_system.cpp:7-8`, `dnd.h:19-20` (C5).
- `WarningCapture` (`shell_host.cpp:90-100`, used at `:443`) is process-wide, so "the shell created
  no windows: <message>" can quote a render-thread or pool warning. Filter on the GUI thread and
  the `qml`/`default` categories.
- Namespace `waku::quickshell` under the `host` unit: `host/quickshell/qml/quickshell_types.h:23`,
  `systemtray/qml/system_tray_item.h:9`, `upower/qml/upower_base.h:7`,
  `wayland/qml/wayland_types.h:14`. Document the exception or move to `waku::host`.
- `shell_host.cpp:3-5` includes private Qt headers for `rootType()`; add a `QT_VERSION_CHECK`
  guard so a Qt bump fails at compile time.
- `bluetooth_win.cpp:981-987`, `:1028-1030`: `catch (const std::exception&)` never matches
  `winrt::hresult_error`, so the HRESULT is lost; `scan()` swallows everything.
- `bluetooth_win.cpp:437`, `:451-457`, `:924-929`: `thread_local QString lastRadioError` as an
  out-parameter; return it in the result.
- `audio_endpoints.h:39`, `audio_stream_source.h:36`, `brightness.h:45`: `Q_INVOKABLE virtual void
  stop()` on non-QML interfaces with a comment about a retired registry.
- `workerw.cpp:147-151`: `qgetenv("APPDATA")` + `fromLocal8Bit` breaks for user names outside the
  ANSI code page; `qEnvironmentVariable`.
- `src/features/color_scheme/dynamic_scheme.cpp:33`: `breakpoints.size() - 1` underflows
  for an empty vector (all callers pass 9; add a guard).

#### C54 style, low (dead code), S
No product caller: `workspaces/komorebi_state.cpp:31-43` (`screenById`), `:110-133`
(`windowHwnds`), `komorebi_service.h:56`, `komorebi.h:75`; `network/wlan.h:57-64`, `:68`, `:71` and
`wlan_win.cpp:273-324` (`queryCurrent`, `disconnectCurrent`, `forget`, `setAutoConnect`,
`currentConnection`, `disconnected`; `WifiNetwork::icon`/`authAlg` never read); `bluetooth.h:128`,
`bluetooth_win.cpp:1294-1298`, `:1322`; `tray/tray_service.h:36-46` (`SystrayService`, test only);
`taskbar/taskbar_icons.h:39` (`pixmapFromBitmap`); `taskbar/pin_support.h:75-85` and
`window_system.h:61` (about 120 lines of Win32 plus fakes); `tray_pipe.cpp` never parses
`uTimeout` but `tray_state.cpp:120` copies it; `app_identity_win.cpp:229` unused `exePath`;
`window_system_win.cpp:174-179` unreachable `default`; `wallpaper_manager.cpp:16-40`, `:140-161`
(`CommandRunner`, `cmd.exe /c runAfter`: never configured and a shell-injection surface if wired);
`media_session.h:128`, `notifications.h:56`, `dnd.h:25` (`kServiceKey` from the retired Widgets
registry); `glazewm_state.cpp:41-60` (C47).

#### C55 docs, low, S
Rows missing from `docs/architecture.md`: the detached `std::thread` per `shellOpen`
(`kernel/win32/shell_launch.cpp:14-27`) in the threading table; `EndTask` via `GetProcAddress` and
the `ITaskList_Deleted` window property (`taskbar/win32/window_system_win.cpp:319-343`, `:106`) in
the allowlist; `g_tracker` (`window_tracker_hooks_win.cpp:16`) and the `containerIdFor` static
cache (`bluetooth_win.cpp:268-302`) in the singleton table;
`RtlUnsubscribeWnfNotificationWaitForCompletion` if C5 uses it.

#### C56 test, low, S
Fixed sleeps around debounce timers: `src/host/tests/test_shell_host.cpp:68` (900 ms),
`:563`, `:565` (700 ms); `shell_fixtures.h:197` `settle()` = `qWait(60)` used 26 times;
`test_quickshell_core.cpp:119-147` sets the process-global `SystemClock::setTimeSource` and resets
it at the end of the body, so an early `ASSERT` leaks the fake clock into later tests. Use
`qWaitFor` and an RAII resetter.

### Duplication

Each entry names the current users; "kernel" means it passes the three tests of `AGENTS.md` (two
users today, no domain knowledge, changes for outside reasons).

#### C57 duplication, medium, S (kernel)
COM apartment scope defined seven times: `kernel/win32/shell_icons.cpp:16-27`,
`apps/win32/start_menu_win.cpp:17`, `apps/win32/apps_folder_win.cpp:11`,
`taskbar/app_identity.h:13` (the only one that documents `RPC_E_CHANGED_MODE`),
`wallpapers/win32/desktop_wallpaper_win.cpp:17`, `notifications/win32/dnd_win.cpp:24`, inline in
`kernel/win32/shell_launch.cpp:18-25`; unbalanced `CoInitializeEx` at
`audio_endpoints_win.cpp:325`, `audio_streams_win.cpp:335`, `touch_keyboard_win.cpp:43`.
One `kernel/win32/com_scope.h` (STA/MTA parameter) and the one place for `CoIncrementMTAUsage`
(C8).

#### C58 duplication, medium, M (kernel)
The worker with a task queue: `brightness_win.cpp:384-435` (`std::thread` + `std::deque` + cv +
stop + coalescing flags), `media_session_winrt.cpp:54-68`, `:124-140` (`Queue` held by
`shared_ptr`, the safest shape), `notifications_system.cpp:284-342`, `:448-453` (same plus a
debounce), and bluetooth, which uses a `QThread` with slots and therefore needs the off-thread
`QPointer` handlers of C4. The stoppable socket worker is the same shape again:
`glazewm_socket.{h,cpp}`, `komorebi_pipe.cpp:88-119`, `tray_hook.cpp`. A `kernel/win32/task_thread.h`
(MTA `std::thread` draining a mutex-guarded deque; `post`, `postCoalesced(delay)`, `stop`/join,
`sleepUnlessStopped(ms)`, per-task exception capture, and the lifetime guard that evaluates a
`QPointer` on the GUI thread) is where C4, C5, C19 and C21 get fixed once.

#### C59 duplication, medium, S (kernel)
The pooled image provider: `host/qml/thumbnails.cpp:16-59`, `:104-136`,
`apps/qml/app_icon_provider.{h,cpp}`, `taskbar/taskbar_icons.cpp`. One base with a
`QThreadPool`, a `QRunnable` that emits into a `QQuickImageResponse` by queued connection, a cancel
flag and `clear()+waitForDone()` fixes C22 for all three.

#### C60 duplication, medium, S-M
Hand-rolled COM refcounting: `audio_endpoints_win.cpp:45-113` (`VolumeCallback`,
`DeviceCallback`), `audio_streams_win.cpp:30-52` (`ComObject<>`, same feature), `bluetooth_win.cpp:627-648`
(`ComPtr`, while the file already includes `winrt/base.h`); `takeCoString` twice
(`audio_endpoints_win.cpp:115-119`, `audio_streams_win.cpp:113-117`). `winrt::implements<Derived,
IAudioEndpointVolumeCallback>` and `winrt::com_ptr` are the helper; no kernel file needed.

#### C61 duplication, medium, S (kernel)
`toQString(hstring)`: `bluetooth_win.cpp:264` (no length, stops at an embedded NUL),
`media_session_winrt.cpp:39`, `notifications_system.cpp:59`; inline
`QString::fromWCharArray(x.c_str())` at `brightness_win.cpp:189`, `wlan_win.cpp:286`, `:342`,
`radio_win.cpp:90`. `kernel/win32/winrt_strings.h` with `toQString(const winrt::hstring&)` and
`toHString(const QString&)`.

#### C62 duplication, medium, S (kernel)
Atomic file write in six styles: `github_api.cpp:423-432`, `host/taskbar_control.cpp:70-76`,
`quickshell_io/file_access.cpp:40-46`, `apps/frecency.cpp` (QJson + `QSaveFile`),
`tray/tray_state.cpp` (nlohmann + `QSaveFile`), `color_scheme/image_stats_cache.cpp:56-102`
(QJson + `QSaveFile` + LRU, `save()` under the mutex on whichever thread sets a value),
`taskbar/pin_store.cpp:51-67` (QJson + non-atomic `QFile` truncate: a crash mid-write loses the pin
file). `kernel::writeFileAtomic(path, bytes)` and a `JsonFile` load-tolerant-of-corruption helper;
the LRU policy stays in color_scheme.

#### C63 duplication, medium, S
`wallpapers/win32/wallpaper_hotkey_win.cpp` (own native event filter, fixed id `0x7A11`) and
`wallpapers/wallpaper_hotkey.cpp` (`parseHotkey`) duplicate `host::Hotkeys` (`host/hotkeys.h:59-95`:
shared filter, ids from `0x2000`, shared combinations, `liveDesktop("hotkeys")`). Consequence today:
`--no-hotkeys` does not disable the wallpaper hotkey and two filters inspect every message.
Features may depend on `host`; use `host::Hotkeys::instance().add(...)` and delete the copies.

#### C64 duplication, low, S (kernel)
Observed-signal timer gating: `desktop_state.cpp:87-117`, `network_service.cpp:91-105`,
`bluetooth_service.cpp:138-158`: identical `connectNotify`/`disconnectNotify` -> queued
`updatePolling` -> `isSignalConnected` loop, including the "re-check from the event loop because Qt
calls `disconnectNotify` under a lock" lesson learned twice. A `kernel::ObservedSignalGate`.

#### C65 duplication, low, S-M
- Two `.lnk` readers with different results: `pin_support_win.cpp:589-623` (no environment
  expansion, default `GetPath`) and `start_menu_win.cpp:40-79` (`SLGP_RAWPATH` + expansion), two
  Start Menu root finders (`pin_support_win.cpp:62` from `%APPDATA%`/`%PROGRAMDATA%` versus
  `start_menu_win.cpp:94` from known folders), two `expandEnvironment` (`pin_support_win.cpp:42`,
  `start_menu_win.cpp:30`). One shortcut module owned by apps.
- HICON to pixels three times: `trayhook.cpp:82-176` (the DLL, must stay standalone),
  `tray_win.cpp:154-220` (RGBA), `kernel::iconBitmapFromHicon` (`shell_icons.cpp:46-83`, BGRA).
- Process starters: `quickshell_io/command_spec.cpp:63-72`, `shell/qml/shell_service.cpp:12-18`,
  `kernel/win32/shell_launch.cpp:34-44` (already the kernel one; give it env and cwd),
  `komorebi_pipe.cpp:31-53`; two QML process types (`ShellProcess`, `Process`) with overlapping
  semantics.
- `describe(QQmlError)`: `shell_host.cpp:38-46`, `shell_window.cpp:18-29`.
- `EnumWindows` + class name: `host/win32/taskbar_control.cpp:20-33`,
  `kernel/win32/app_activation.cpp:96-111`.
- Verb dispatch: six `splitCommand` + `removeFirst()` blocks in `control_handler.cpp:87-121`;
  `verb == QLatin1String(...)` chains in `surface_control.cpp:36-38`, `taskbar_command.cpp:11-12`,
  `control_handler.cpp:31-33`; two live registries with different containers
  (`ipc_handler.cpp:20-23` QList, `global_shortcut.cpp:12-15` std::vector).
- WNF typedefs twice (`notifications_system.cpp:46-51`, `dnd_win.cpp:93-98`): `kernel/win32/wnf.h`
  with the one allowlist row (C5).
- `roundOne` three times (`pdh.cpp:53`, `memory.cpp:49`, `battery.cpp:20`); PDH wildcard read twice
  (`gpu.cpp:63-78`, `thermal.cpp:19-40`); `timeText` twice (`notification_service.cpp:12-17`,
  `notification_feed.cpp:28-33`).

## Conflicts between findings (same files)

Assign one owner per group; the findings inside a group must land in sequence or in one change.

| Files | Findings |
|---|---|
| `host/qml/shell_host.cpp`, `shell_host.h` | C3, C15, C16, C31, C35, C36, C40, C53 |
| `host/qml/shell_window.{h,cpp}`, `lazy_loader.cpp` | C1, C2, C27 (`ProxiedWindow` knobs), C41 (`afterAnimating`) |
| `host/qml/thumbnails.{h,cpp}`, `host/thumbnail_store.{h,cpp}` | C22, C29, C59 |
| `host/win32/taskbar_control.cpp`, `host/taskbar_control.cpp` | C38, C62, C65 |
| `host/mask_poller.{h,cpp}`, `host/qml/region.cpp` | C41 only |
| `app/waku/main.cpp` | C8 (`CoIncrementMTAUsage`), C27 (render knobs), C36 (plugin scan) |
| `features/bluetooth/win32/bluetooth_win.cpp` | C4, C19, C32, C46, C53, C54, C60, C61 |
| `features/notifications/win32/notifications_system.cpp`, `dnd_win.cpp` | C5, C25, C48, C53, C57, C58, C61, C65 |
| `features/taskbar/qml/taskbar_service.cpp`, `taskbar_icons.cpp`, `window_tracker.cpp` | C4, C7, C12, C22, C34, C37, C42 |
| `features/taskbar/win32/pin_support_win.cpp`, `window_system_win.cpp`, `app_identity_win.cpp` | C8, C42, C54, C55, C65 |
| `features/tray/*` | C7, C9, C23, C34, C43, C54, C65 |
| `features/apps/*` | C8, C22, C37, C57, C59, C65 |
| `features/wallpapers/*` | C13, C17, C28, C29, C37, C49, C63 |
| `features/audio/win32/*` | C4, C44, C57, C60 |
| `features/network/*` | C20, C24, C45, C54, C64 |
| `features/workspaces/*` | C6, C14, C21, C39, C47, C54 |
| `features/media/*` | C18, C25, C26, C34, C48, C61 |
| `features/color_scheme/*` | C4, C34, C53, C62 |
| `features/control_center/*` | C37, C50, C53, C57 |
| `features/github/*` | C4, C37, C50, C51, C62 |
| `features/quickshell_io/*` | C34, C50, C62, C65 |
| `kernel/win32/*` (new files) | C57, C58, C59, C61, C62 create files that C4, C5, C8, C22 then use: create the kernel helpers first or let the feature fixes use local code and migrate in the duplication batch |

## Profiling plan

All commands are `pwsh`. Use a copy of the real config home so nothing writes to
`%USERPROFILE%\.config\waku`; the engine never sees the difference. Adjust `$exe` to the build
or `dist` folder in use. Run each phase from a fresh process; take three samples and keep the
median.

### Memory per phase

```powershell
$exe    = Join-Path $PWD 'dist\waku\waku.exe'
$wakuc = Join-Path $PWD 'dist\waku\wakuc.exe'
$cfg    = Join-Path $env:LOCALAPPDATA 'waku-profile-config'
$real   = Join-Path $env:USERPROFILE '.config\waku'
if (-not (Test-Path -LiteralPath $cfg)) { Copy-Item -LiteralPath $real -Destination $cfg -Recurse }
$out    = Join-Path $env:LOCALAPPDATA 'waku-profile'
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Measure-Waku([string]$label, [int]$procId) {
  $p = Get-Process -Id $procId
  $gpu = Get-Counter "\GPU Process Memory(pid_${procId}_*)\Dedicated Usage",
                     "\GPU Process Memory(pid_${procId}_*)\Shared Usage" -ErrorAction SilentlyContinue
  $ded = ($gpu.CounterSamples | Where-Object Path -like '*Dedicated*' | Measure-Object CookedValue -Sum).Sum
  $sha = ($gpu.CounterSamples | Where-Object Path -like '*Shared*'    | Measure-Object CookedValue -Sum).Sum
  $pws = (Get-Counter "\Process V2(waku:$procId)\Working Set - Private" -ErrorAction SilentlyContinue).CounterSamples[0].CookedValue
  [pscustomobject]@{
    Phase        = $label
    PrivateMB    = [math]::Round($p.PrivateMemorySize64 / 1MB)
    PrivateWS_MB = [math]::Round($pws / 1MB)
    WorkingSetMB = [math]::Round($p.WorkingSet64 / 1MB)
    GpuDedMB     = [math]::Round($ded / 1MB)
    GpuSharedMB  = [math]::Round($sha / 1MB)
    Threads      = $p.Threads.Count
    Handles      = $p.HandleCount
  }
}
```

Phase 1, engine only: `--safe` builds the built-in shell, no config, no plugins.

```powershell
$p = Start-Process -FilePath $exe -ArgumentList '--safe','--no-tray','--no-hotkeys','--config-home',$cfg -PassThru
Start-Sleep -Seconds 6
Measure-Waku 'safe' $p.Id | Format-Table
& $wakuc --config-home $cfg quit
```

Phase 2, shell loaded; phase 3, after each overlay is hidden (C27: if the private working set
drops by about 100 MB per full-screen window, the swap chains are the bulk); phase 4, after N
reloads (C31: the curve must plateau after the second reload; a straight line is a leak).

```powershell
$env:QSG_INFO = '1'   # logs the RHI backend, swap chain and render loop per window
$p = Start-Process -FilePath $exe -ArgumentList '--config-home',$cfg -PassThru -RedirectStandardError (Join-Path $out 'qsg.log')
Start-Sleep -Seconds 10
$rows = @(Measure-Waku 'shell loaded' $p.Id)
& $wakuc --config-home $cfg surface list
foreach ($name in @('launcher','osd','dashboard')) {   # the shell's named overlays
  & $wakuc --config-home $cfg surface hide $name | Out-Null
  Start-Sleep -Seconds 3
  $rows += Measure-Waku "hidden $name" $p.Id
}
1..10 | ForEach-Object {
  & $wakuc --config-home $cfg reload | Out-Null
  Start-Sleep -Seconds 4
  $rows += Measure-Waku "reload $_" $p.Id
}
$rows | Format-Table
$rows | Export-Csv -NoTypeInformation -Path (Join-Path $out 'memory-phases.csv')
```

Attribution: VMMap (Sysinternals) splits the working set into heap, private data, images, mapped
files and page tables; a large "Private Data" with little "Heap" points at the GPU driver and
QRhi staging allocations (C27), a large "Heap" at QImage copies and the QML engine (C28, C29, C31).
Save one snapshot per phase and compare in the GUI (File, Compare).

```powershell
$vmmap = 'C:\Tools\SysinternalsSuite\vmmap.exe'
& $vmmap -accepteula -p $p.Id (Join-Path $out 'shell-loaded.mmp')
```

Render loop and depth buffer A/B for C27 (no code change needed for the first two):

```powershell
$env:QSG_RENDER_LOOP = 'basic'      # one QRhi for every window
# second run: Remove-Item Env:QSG_RENDER_LOOP; then with the setDepthBufferFor2D(false) build
```

Heap growth inside the process (which allocations grow across reloads): the Qt heap is the CRT
heap, so `!heap -s` in WinDbg or a UMDH pair works. UMDH needs symbols and `gflags /i waku.exe
+ust`:

```powershell
& 'C:\Program Files (x86)\Windows Kits\10\Debuggers\arm64\gflags.exe' /i waku.exe +ust
$umdh = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\arm64\umdh.exe'
& $umdh -p:$($p.Id) -f:(Join-Path $out 'heap-1.txt')
1..5 | ForEach-Object { & $wakuc --config-home $cfg reload | Out-Null; Start-Sleep -Seconds 4 }
& $umdh -p:$($p.Id) -f:(Join-Path $out 'heap-2.txt')
& $umdh (Join-Path $out 'heap-1.txt') (Join-Path $out 'heap-2.txt') -f:(Join-Path $out 'heap-diff.txt')
& 'C:\Program Files (x86)\Windows Kits\10\Debuggers\arm64\gflags.exe' /i waku.exe -ust
```

### Startup and CPU

Wall clock of the phases, with one line per phase from the bar itself. The code change is small
and belongs in batch 1: `QElapsedTimer` started before `QGuiApplication`, `qInfo()` lines behind
`WAKU_TRACE=1` at: application built, engine built, host built, validate done, build done,
install done, first `frameSwapped` of each window, `watch()` done. Until then, the total:

```powershell
$t = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $exe -ArgumentList '--config-home',$cfg -PassThru
while (-not (& $wakuc --config-home $cfg status 2>$null)) { Start-Sleep -Milliseconds 50 }
"status answered after $($t.ElapsedMilliseconds) ms"
```

ETW trace of the first ten seconds (CPU sampling, file I/O, process starts); open the `.etl` in
WPA and look at the main thread's stack for `QQmlTypeLoader`, `QQmlComponent::create`,
`CoCreateInstance`, `QDirIterator`, `SHAppBarMessage`, `Sleep`:

```powershell
wpr -start GeneralProfile -start CPU -start FileIO -filemode
$p = Start-Process -FilePath $exe -ArgumentList '--config-home',$cfg -PassThru
Start-Sleep -Seconds 10
wpr -stop (Join-Path $out 'startup.etl')
& $wakuc --config-home $cfg quit
```

Qt's own view of the QML cost. The disk cache log says which files were compiled instead of
loaded; the import trace says how many qmldirs the two engines read (C35, C36):

```powershell
$env:QT_LOGGING_RULES = 'qt.qml.diskcache=true;qt.scenegraph.general=true;qt.scenegraph.time.*=true'
$env:QML_IMPORT_TRACE = '1'
$p = Start-Process -FilePath $exe -ArgumentList '--config-home',$cfg -PassThru -RedirectStandardError (Join-Path $out 'qml.log')
Start-Sleep -Seconds 10
& $wakuc --config-home $cfg quit
Remove-Item Env:QT_LOGGING_RULES, Env:QML_IMPORT_TRACE
Select-String -Path (Join-Path $out 'qml.log') -Pattern 'compiling|Cache' | Measure-Object
```

QML profiler (creation, binding and signal-handler times per file). Needs a build with
`QT_QML_DEBUG` defined for `waku` (the debug preset; add `target_compile_definitions(waku
PRIVATE $<$<CONFIG:Debug>:QT_QML_DEBUG>)` if it is not there):

```powershell
$dbg = Join-Path $PWD 'build\arm64-debug\src\app\waku\waku.exe'
$p = Start-Process -FilePath $dbg -ArgumentList '-qmljsdebugger=port:3768,block','--config-home',$cfg -PassThru
$qmlprofiler = Join-Path (Split-Path (Get-Command qmake -ErrorAction SilentlyContinue).Source -ErrorAction SilentlyContinue) 'qmlprofiler.exe'
& $qmlprofiler --attach 127.0.0.1 --port 3768 --output (Join-Path $out 'startup.qtd') --record-duration 15000
```

Steady state: with the bar idle and the mouse still, the GUI thread must sit at zero. Watch the
per-thread CPU in Process Explorer or:

```powershell
Get-Counter "\Process V2(waku:$($p.Id))\% Processor Time" -SampleInterval 1 -MaxSamples 30 |
  ForEach-Object { $_.CounterSamples[0].CookedValue }
```

Repeat while moving the mouse over the desktop (C41), while a tray icon animates (C43), while
music plays (C48), and with `--hide-taskbar` (C38). `QSG_RENDER_TIMING=1` prints per-frame
sync/render times per window when anything animates.

### Catching the next heap corruption

Both crashes left no dump because WER local dumps are off by default. Turn them on (admin shell,
once), then run normally; a fail-fast or `STATUS_HEAP_CORRUPTION` then writes a full dump:

```powershell
$key = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\waku.exe'
New-Item -Path $key -Force | Out-Null
New-ItemProperty -Path $key -Name DumpType   -PropertyType DWord        -Value 2 -Force | Out-Null
New-ItemProperty -Path $key -Name DumpCount  -PropertyType DWord        -Value 5 -Force | Out-Null
New-ItemProperty -Path $key -Name DumpFolder -PropertyType ExpandString -Value (Join-Path $env:LOCALAPPDATA 'CrashDumps') -Force | Out-Null
```

Make the corruption fault at the offending write instead of later. Normal page heap is light
enough for the laptop; full page heap doubles memory and is for a short reload loop only:

```powershell
$gflags = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\arm64\gflags.exe'
& $gflags /p /enable waku.exe            # light: guard fills, checked on free
# reload stress under it
$p = Start-Process -FilePath $exe -ArgumentList '--config-home',$cfg -PassThru
1..50 | ForEach-Object { & $wakuc --config-home $cfg reload | Out-Null; Start-Sleep -Seconds 2 }
& $wakuc --config-home $cfg quit
& $gflags /p /disable waku.exe
```

Application Verifier's heap checks add the call stack of the freeing site to the dump:
`appverif -enable Heaps -for waku.exe` (and `appverif -disable * -for waku.exe` after). For the
C1 path specifically, toggle a `LazyLoader`-driven overlay from inside itself under the debug
build with page heap on; the write after free then faults in `QQuickDeliveryAgentPrivate`.

The x64 cross build can run under AddressSanitizer (`/fsanitize=address` on the `waku_options`
target, tests under emulation); check whether the installed MSVC toolset supports it on ARM64
before planning on that host.

## Work plan

Batches of independent changes; agents inside a batch touch disjoint files (see the conflict
table). Every change ends with `tools\check.ps1` green and one conventional commit per logical
change. Land batch 0 before any measurement work, because the crashes are what stop the user from
running the profiling loops.

### Batch 0: crash fixes (one agent per row, parallel)

| Lane | Findings | Files |
|---|---|---|
| 0a | C1, C2, C3, C16, C15 | `host/qml/lazy_loader.cpp`, `shell_window.{h,cpp}`, `shell_host.{h,cpp}`, `shell_screen.cpp` (comments) |
| 0b | C4 (github), C50 (one manager) | `features/github/qml/github_service.cpp`, `github_api.cpp` |
| 0c | C4 (color_scheme) | `features/color_scheme/qml/color_scheme_service.cpp` |
| 0d | C4 (audio) | `features/audio/win32/audio_endpoints_win.cpp`, `audio_streams_win.cpp` |
| 0e | C4, C19, C53 (hresult catch) | `features/bluetooth/win32/bluetooth_win.cpp` |
| 0f | C5, C53 (comments), C55 (allowlist row) | `features/notifications/win32/notifications_system.cpp`, `dnd_win.cpp`, `dnd.h`, `docs/architecture.md` |
| 0g | C6, C14, C21 | `features/workspaces/win32/glazewm_socket.cpp`, `komorebi_pipe.cpp`, `komorebi.h` |
| 0h | C7, C9, C12, C22 (taskbar part) | `features/taskbar/qml/taskbar_service.{h,cpp}`, `taskbar_icons.cpp`, `features/tray/qml/tray_image_provider.*`, `tray_icon_model.*`, `hook/trayhook.cpp` |
| 0i | C8 (minimal: `CoIncrementMTAUsage` in `main.cpp`), C10, C11 | `app/waku/main.cpp`, `host/qml/persistent_properties.cpp`, `ipc_handler.cpp` |
| 0j | C13, C17 | `features/wallpapers/qml/wallpapers_service.cpp`, `wallpaper_transition.{h,cpp}`, `win32/workerw.cpp` |
| 0k | no code: WER LocalDumps, page heap, the 50-reload loop | |

### Batch 1: memory and startup (after batch 0; parallel)

| Lane | Findings | Files |
|---|---|---|
| 1a | C27 knobs, C35, C36, C40 (event filter), C31 (`trimComponentCache`), startup trace lines | `app/waku/main.cpp`, `host/qml/shell_host.cpp`, `shell_window.cpp` (`ProxiedWindow`), `host/plugin_guard.cpp` |
| 1b | C28, C29 (delete the wallpapers cache), C37 (wallpapers listing), C49 (model) | `features/wallpapers/*` |
| 1c | C29 (shared store, 16-24 MB), C22 (thumbnails part), C59 | `host/qml/thumbnails.{h,cpp}`, `host/thumbnail_store.{h,cpp}`, then `apps/qml/app_icon_provider.*` |
| 1d | C37 (control_center), C50 (control_center), C57 (local scope only) | `features/control_center/*` |
| 1e | C37 (apps hub, watcher roots), C34 (none) | `features/apps/*` |
| 1f | C32, C46 | `features/bluetooth/win32/bluetooth_win.cpp`, `qml/bluetooth_service.cpp` |
| 1g | C38 | `host/win32/taskbar_control.cpp`, `host/taskbar_control.cpp` |
| 1h | C39, C47 | `features/workspaces/qml/*`, `glazewm_state.cpp` |
| 1i | C33 | `features/system_stats/*` |
| 1j | C30, C40 (Clock) | `host/quickshell/widgets/ClippingRectangle.qml`, `host/qml/WindowChrome.qml`, `features/clock/qml/Clock.qml` |
| 1k | C26, C48, C25 (media part), C34 (art) | `features/media/*` |

Measure after batch 1 with the memory-phase script; decide on the raw-input poller (C41) and the
rest of batch 2 from the numbers.

### Batch 2: steady-state performance (parallel)

| Lane | Findings | Files |
|---|---|---|
| 2a | C41 | `host/mask_poller.{h,cpp}`, `host/qml/region.{h,cpp}`, `shell_window.cpp` (`afterAnimating`) |
| 2b | C42, C34 (taskbar cache) | `features/taskbar/*` |
| 2c | C43, C23, C34 (tray RGBA) | `features/tray/*` |
| 2d | C44 | `features/audio/*` |
| 2e | C45, C24, C20 | `features/network/*` |
| 2f | C25 (notifications part), C48 (toasts) | `features/notifications/*` |
| 2g | C50 (desktop_state, persistent_properties, script_model, touch keyboard, processes), C34 (StdioCollector, FileView) | the files named in C50 and C34 |

### Batch 3: duplication, style, dead code, docs (parallel; mechanical)

| Lane | Findings | Files |
|---|---|---|
| 3a | C57, C58, C61, C62 (create the kernel helpers; name the two users of each in the PR) | `kernel/win32/com_scope.h`, `task_thread.h`, `winrt_strings.h`, `kernel/qt/file_write.h`, `docs/architecture.md` kernel table |
| 3b | migrate features onto 3a: C8 full, C57 users, C61 users, C62 users (one commit per feature) | one feature per agent |
| 3c | C60, C65 (audio and taskbar/apps items) | `features/audio/win32/*`, `features/taskbar/win32/*`, `features/apps/win32/*` |
| 3d | C63, C64 | `features/wallpapers/*hotkey*`, `desktop_state`, `network`, `bluetooth` service gating |
| 3e | C51 (rule decision first), C52, C53 (remaining), C54, C55, C56 | repository-wide; C52 in its own commit added to `.git-blame-ignore-revs` |
