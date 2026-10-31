#include "features/notifications/win32/wnf_win.h"

#include <Windows.h>

#include <map>
#include <mutex>

namespace wabar::notifications {

struct WnfSubscription::Slot {
  std::mutex mutex;  // held while the handler runs and while unsubscribe() retires the slot
  Handler handler;   // empty once retired
  std::uintptr_t token = 0;
  void* subscription = nullptr;
};

namespace {

// Process-wide registry of live subscriptions, keyed by the token handed to WNF as its context.
// A callback copies its slot out and releases this lock before it runs the handler, so a slow
// handler never blocks another subscription.
struct Registry {
  std::mutex mutex;
  std::uintptr_t lastToken = 0;
  std::map<std::uintptr_t, std::shared_ptr<void>> slots;
};

Registry& registry() {
  static Registry instance;
  return instance;
}

}  // namespace

WnfSubscription::Api WnfSubscription::systemApi() {
  Api api;
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) return api;
  api.subscribe = reinterpret_cast<decltype(api.subscribe)>(
      GetProcAddress(ntdll, "RtlSubscribeWnfStateChangeNotification"));
  api.unsubscribe = reinterpret_cast<decltype(api.unsubscribe)>(
      GetProcAddress(ntdll, "RtlUnsubscribeWnfStateChangeNotification"));
  if (!api.subscribe || !api.unsubscribe) return {};
  return api;
}

WnfSubscription::~WnfSubscription() { unsubscribe(); }

// WNF system thread. Never throws into the system.
long __stdcall WnfSubscription::dispatch(std::uint64_t, unsigned long, void*, void* context,
                                         const void* buffer, unsigned long bufferSize) {
  try {
    std::shared_ptr<Slot> slot;
    {
      Registry& reg = registry();
      std::lock_guard lock(reg.mutex);
      const auto found = reg.slots.find(reinterpret_cast<std::uintptr_t>(context));
      if (found == reg.slots.end()) return 0;
      slot = std::static_pointer_cast<Slot>(found->second);
    }
    // `slot` keeps the memory alive; the mutex orders this handler before unsubscribe() returns.
    std::lock_guard lock(slot->mutex);
    if (slot->handler) slot->handler(buffer, bufferSize);
  } catch (...) {
    // A handler or the allocator threw; drop this change.
  }
  return 0;
}

long WnfSubscription::subscribe(std::uint64_t stateName, Handler handler) {
  if (!available() || slot_) return -1;
  auto slot = std::make_shared<Slot>();
  slot->handler = std::move(handler);
  {
    Registry& reg = registry();
    std::lock_guard lock(reg.mutex);
    slot->token = ++reg.lastToken;
    reg.slots.emplace(slot->token, slot);
  }
  // Registered first: WNF may deliver the current state before subscribe returns.
  void* subscription = nullptr;
  const long status = api_.subscribe(&subscription, stateName, 0, &WnfSubscription::dispatch,
                                     reinterpret_cast<void*>(slot->token), nullptr, 0, 0);
  slot->subscription = subscription;
  slot_ = std::move(slot);
  if (status != 0) {
    slot_->subscription = nullptr;
    unsubscribe();
  }
  return status;
}

void WnfSubscription::unsubscribe() {
  if (!slot_) return;
  if (slot_->subscription) api_.unsubscribe(slot_->subscription);
  {
    Registry& reg = registry();
    std::lock_guard lock(reg.mutex);
    reg.slots.erase(slot_->token);
  }
  {
    // Waits for a handler that found the slot before it left the registry.
    std::lock_guard lock(slot_->mutex);
    slot_->handler = nullptr;
  }
  slot_.reset();
}

}  // namespace wabar::notifications
