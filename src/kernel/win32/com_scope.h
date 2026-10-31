#pragma once

namespace wabar::kernel {

enum class ComApartment { MultiThreaded, SingleThreaded };

// Joins the calling thread to COM for the lifetime of the object and balances what it did.
//
// CoInitializeEx returns S_OK (first initialisation on this thread) or S_FALSE (already in that
// apartment); both are counted and undone by the destructor. RPC_E_CHANGED_MODE means the thread
// already runs the other apartment model: that apartment is kept as it is, nothing is undone, and
// COM calls still work. The process-wide MTA usage (`src/app/wabar/main.cpp`) keeps the MTA alive
// between scopes, so a scope on a pool thread does not tear the apartment down.
//
// `disableOle1Dde` adds COINIT_DISABLE_OLE1DDE, which ShellExecute wants. Not copyable; one thread.
class ComScope {
 public:
  explicit ComScope(ComApartment apartment, bool disableOle1Dde = false);
  ~ComScope();
  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

  // True when CoInitializeEx succeeded, so the destructor calls CoUninitialize.
  bool initialised() const { return initialised_; }
  // The CoInitializeEx result (an HRESULT).
  long result() const { return result_; }

 private:
  long result_;
  bool initialised_;
};

}  // namespace wabar::kernel
