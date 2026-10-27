#include "app/single_instance.h"

#include <chrono>
#include <thread>
#include <utility>

namespace yasb::app {

SingleInstance::SingleInstance(std::wstring name) : name_(std::move(name)) {}

SingleInstance::~SingleInstance() {
  if (handle_) CloseHandle(handle_);
}

bool SingleInstance::acquire(bool restartWait, unsigned timeoutMs) {
  if (handle_) return true;
  error_.clear();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  bool waited = false;
  for (;;) {
    HANDLE candidate = CreateMutexW(nullptr, FALSE, name_.c_str());
    if (!candidate) {
      error_ = "CreateMutexW failed: " + std::to_string(GetLastError());
      return false;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
      handle_ = candidate;
      return true;
    }
    CloseHandle(candidate);
    if (!restartWait || std::chrono::steady_clock::now() >= deadline) {
      error_ = waited ? "Timeout waiting for previous instance to exit"
                      : "Another instance is already running";
      return false;
    }
    waited = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
}

}  // namespace yasb::app
