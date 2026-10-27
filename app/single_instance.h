#pragma once

#include <windows.h>

#include <string>

namespace yasb::app {

class SingleInstance {
 public:
  explicit SingleInstance(std::wstring name = L"yasb_reborn");
  ~SingleInstance();
  SingleInstance(const SingleInstance&) = delete;
  SingleInstance& operator=(const SingleInstance&) = delete;

  bool acquire(bool restartWait, unsigned timeoutMs = 10000);
  const std::string& error() const { return error_; }

 private:
  std::wstring name_;
  HANDLE handle_ = nullptr;
  std::string error_;
};

}  // namespace yasb::app
