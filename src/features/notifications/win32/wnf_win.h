#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace waku::notifications {

// One subscription to an undocumented WNF state (ntdll!RtlSubscribeWnfStateChangeNotification,
// docs/architecture.md allowlist), shared by the notification count and DndWatcher.
//
// Lifetime: WNF calls back on a system thread, and nothing documents that
// RtlUnsubscribeWnfStateChangeNotification waits for a callback in flight. So the context handed
// to WNF is an integer token, never a pointer. The callback looks the token up in a process-wide
// registry and runs the handler under that subscription's mutex. unsubscribe() removes the token
// and then takes the same mutex, so when it returns no handler is running and none will start.
// Keep handlers short: unsubscribe() (and the destructor) wait for one in flight.
class WnfSubscription {
 public:
  using Handler = std::function<void(const void* buffer, unsigned long size)>;

  // The ntdll entry points. Tests pass fakes; the default resolves them from ntdll.
  using Callback = long(__stdcall*)(std::uint64_t stateName, unsigned long changeStamp,
                                    void* typeId, void* context, const void* buffer,
                                    unsigned long bufferSize);
  struct Api {
    long(__stdcall* subscribe)(void** subscription, std::uint64_t stateName,
                               unsigned long changeStamp, Callback callback, void* context,
                               void* typeId, unsigned long serializationGroup,
                               unsigned long unknown) = nullptr;
    long(__stdcall* unsubscribe)(void* subscription) = nullptr;
  };
  // Both pointers null when ntdll lacks either symbol.
  static Api systemApi();

  WnfSubscription() = default;
  explicit WnfSubscription(Api api) : api_(api) {}
  ~WnfSubscription();
  WnfSubscription(const WnfSubscription&) = delete;
  WnfSubscription& operator=(const WnfSubscription&) = delete;

  // True when the API is present. subscribe() fails without it.
  bool available() const { return api_.subscribe && api_.unsubscribe; }
  // Returns the WNF status (0 on success); -1 when the API is missing or already subscribed. The
  // handler may run before this returns.
  long subscribe(std::uint64_t stateName, Handler handler);
  // Safe to call twice. Blocks while a handler runs.
  void unsubscribe();
  bool active() const { return slot_ != nullptr; }

 private:
  struct Slot;
  static long __stdcall dispatch(std::uint64_t stateName, unsigned long changeStamp, void* typeId,
                                 void* context, const void* buffer, unsigned long bufferSize);

  Api api_ = systemApi();
  std::shared_ptr<Slot> slot_;
};

}  // namespace waku::notifications
