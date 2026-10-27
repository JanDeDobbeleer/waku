#pragma once

#include <windows.h>

#include <QString>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace yasb::app {

inline constexpr wchar_t kCommandPipeName[] = L"\\\\.\\pipe\\yasb_pipe_cli";
inline constexpr wchar_t kLogPipeName[] = L"\\\\.\\pipe\\yasb_pipe_log";

QString commandReply(const QString& command);
QByteArray logDataFrame(const QString& data);
QByteArray logPongMessage();

// Both pipes keep Python YASB's names and wire protocol, but unlike Python they only admit the
// current user (DACL with the token user SID alone) and reject remote clients. Commands and log
// requests larger than kMaxPipeMessageBytes are rejected.
inline constexpr unsigned long kMaxPipeMessageBytes = 64 * 1024;

class CliServer {
 public:
  using CommandHandler = std::function<void(const QString&)>;

  explicit CliServer(std::wstring commandPipeName = kCommandPipeName,
                     std::wstring logPipeName = kLogPipeName);
  ~CliServer();
  CliServer(const CliServer&) = delete;
  CliServer& operator=(const CliServer&) = delete;

  // Fails closed: if the restricted security descriptor cannot be built, no pipe is created.
  void start(CommandHandler handler);
  void stop();

 private:
  void commandLoop();
  void logLoop();
  std::atomic<bool> stopping_{false};
  CommandHandler handler_;
  std::wstring commandPipeName_;
  std::wstring logPipeName_;
  PSECURITY_DESCRIPTOR security_ = nullptr;  // LocalAlloc'ed, current user only
  std::vector<std::thread> commandThreads_;
  std::thread logThread_;
};

}  // namespace yasb::app
