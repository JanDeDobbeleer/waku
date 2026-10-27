#include <gtest/gtest.h>
// clang-format off
#include <windows.h>
#include <aclapi.h>
// clang-format on

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "app/cli_server.h"

namespace {

std::wstring uniquePipeName(const wchar_t* suffix) {
  return L"\\\\.\\pipe\\yasb_native_test_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
         std::to_wstring(GetTickCount64()) + suffix;
}

HANDLE connectPipe(const std::wstring& name) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  DWORD lastError = ERROR_SUCCESS;
  std::mutex retryMutex;
  std::condition_variable retryCondition;
  while (std::chrono::steady_clock::now() < deadline) {
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) return pipe;
    lastError = GetLastError();
    if (lastError == ERROR_PIPE_BUSY) {
      WaitNamedPipeW(name.c_str(), 100);
      continue;
    }
    std::unique_lock lock(retryMutex);
    retryCondition.wait_for(lock, std::chrono::milliseconds(10));
  }
  SetLastError(lastError);
  return INVALID_HANDLE_VALUE;
}

bool readMessage(HANDLE pipe, std::string& data) {
  char buffer[65536]{};
  OVERLAPPED operation{};
  operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!operation.hEvent) return false;
  DWORD read = 0;
  BOOL complete = ReadFile(pipe, buffer, sizeof(buffer), &read, &operation);
  if (!complete && GetLastError() == ERROR_IO_PENDING) {
    const DWORD wait = WaitForSingleObject(operation.hEvent, 3000);
    if (wait != WAIT_OBJECT_0) {
      CancelIoEx(pipe, &operation);
      GetOverlappedResult(pipe, &operation, &read, TRUE);
      CloseHandle(operation.hEvent);
      return false;
    }
    complete = GetOverlappedResult(pipe, &operation, &read, FALSE);
  }
  CloseHandle(operation.hEvent);
  if (!complete) return false;
  data.assign(buffer, read);
  return true;
}

bool writeMessage(HANDLE pipe, const std::string& data) {
  OVERLAPPED operation{};
  operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!operation.hEvent) return false;
  DWORD written = 0;
  BOOL complete =
      WriteFile(pipe, data.data(), static_cast<DWORD>(data.size()), &written, &operation);
  if (!complete && GetLastError() == ERROR_IO_PENDING) {
    const DWORD wait = WaitForSingleObject(operation.hEvent, 3000);
    if (wait != WAIT_OBJECT_0) {
      CancelIoEx(pipe, &operation);
      GetOverlappedResult(pipe, &operation, &written, TRUE);
      CloseHandle(operation.hEvent);
      return false;
    }
    complete = GetOverlappedResult(pipe, &operation, &written, FALSE);
  }
  CloseHandle(operation.hEvent);
  return complete && written == static_cast<DWORD>(data.size());
}

std::vector<unsigned char> currentUser() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<unsigned char> buffer(size);
  if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) buffer.clear();
  CloseHandle(token);
  return buffer;
}

// The DACL of the pipe behind `pipe` must hold exactly one allow ACE, for the current user.
void expectCurrentUserOnly(HANDLE pipe) {
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  ASSERT_EQ(GetSecurityInfo(pipe, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                            &dacl, nullptr, &descriptor),
            static_cast<DWORD>(ERROR_SUCCESS));
  ASSERT_NE(dacl, nullptr);
  EXPECT_EQ(dacl->AceCount, 1);
  void* ace = nullptr;
  ASSERT_TRUE(GetAce(dacl, 0, &ace));
  auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
  EXPECT_EQ(allowed->Header.AceType, ACCESS_ALLOWED_ACE_TYPE);
  const auto user = currentUser();
  ASSERT_FALSE(user.empty());
  EXPECT_TRUE(EqualSid(reinterpret_cast<PSID>(&allowed->SidStart),
                       reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid));
  LocalFree(descriptor);
}

}  // namespace

TEST(CliProtocol, AcceptsKnownCommandsAndRejectsUnknownOnes) {
  for (const QString& command :
       {QStringLiteral("reload"), QStringLiteral("stop"), QStringLiteral("show-bar"),
        QStringLiteral("hide-bar --screen DISPLAY1"), QStringLiteral("toggle-bar -s DISPLAY1")}) {
    EXPECT_EQ(yasb::app::commandReply(command), QStringLiteral("ACK"));
  }
  EXPECT_EQ(yasb::app::commandReply(QStringLiteral("unknown")),
            QStringLiteral("CLI Unknown Command"));
  EXPECT_EQ(yasb::app::commandReply(QString()), QStringLiteral("CLI Unknown Command"));
}

TEST(CliProtocol, EncodesNulTerminatedJsonDataAndPlainPong) {
  const QByteArray data = yasb::app::logDataFrame(QStringLiteral("connected"));
  ASSERT_FALSE(data.isEmpty());
  EXPECT_EQ(data.back(), '\0');
  const auto json = nlohmann::json::parse(data.constData(), data.constData() + data.size() - 1);
  EXPECT_EQ(json.at("type"), "DATA");
  EXPECT_EQ(json.at("data"), "connected");

  const QByteArray pong = yasb::app::logPongMessage();
  EXPECT_NE(pong.back(), '\0');
  const auto pongJson = nlohmann::json::parse(pong.constData(), pong.constData() + pong.size());
  EXPECT_EQ(pongJson.at("type"), "PONG");
}

TEST(CliProtocol, CommandPipeAcknowledgesAndDispatchesPlainText) {
  const auto commandName = uniquePipeName(L"_cli");
  const auto logName = uniquePipeName(L"_log");
  std::mutex mutex;
  std::condition_variable condition;
  QString received;
  yasb::app::CliServer server(commandName, logName);
  server.start([&](const QString& command) {
    std::lock_guard lock(mutex);
    received = command;
    condition.notify_all();
  });

  HANDLE pipe = connectPipe(commandName);
  ASSERT_NE(pipe, INVALID_HANDLE_VALUE) << "timed out connecting to command pipe";
  ASSERT_TRUE(writeMessage(pipe, "hide-bar --screen DISPLAY1"));
  std::string response;
  ASSERT_TRUE(readMessage(pipe, response));
  EXPECT_EQ(response, "ACK");
  CloseHandle(pipe);
  {
    std::unique_lock lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(3), [&] {
      return !received.isEmpty();
    })) << "command pipe acknowledged but did not dispatch the command within 3 seconds";
    EXPECT_EQ(received, QStringLiteral("hide-bar --screen DISPLAY1"));
  }

  pipe = connectPipe(commandName);
  ASSERT_NE(pipe, INVALID_HANDLE_VALUE) << "timed out reconnecting to command pipe";
  ASSERT_TRUE(writeMessage(pipe, "invalid-command"));
  ASSERT_TRUE(readMessage(pipe, response));
  EXPECT_EQ(response, "CLI Unknown Command");
  CloseHandle(pipe);
}

TEST(CliProtocol, LogPipeStreamsDataAndAnswersPing) {
  const auto commandName = uniquePipeName(L"_cli");
  const auto logName = uniquePipeName(L"_log");
  yasb::app::CliServer server(commandName, logName);
  server.start([](const QString&) {});

  HANDLE pipe = connectPipe(logName);
  ASSERT_NE(pipe, INVALID_HANDLE_VALUE) << "timed out connecting to log pipe";
  std::string frame;
  ASSERT_TRUE(readMessage(pipe, frame)) << "timed out waiting for initial log frame";
  ASSERT_FALSE(frame.empty());
  EXPECT_EQ(frame.back(), '\0');
  const auto data = nlohmann::json::parse(frame.data(), frame.data() + frame.size() - 1);
  EXPECT_EQ(data.at("type"), "DATA");

  ASSERT_TRUE(writeMessage(pipe, R"({"type":"PING"})"));
  ASSERT_TRUE(readMessage(pipe, frame)) << "timed out waiting for PONG";
  const auto pong = nlohmann::json::parse(frame.data(), frame.data() + frame.size());
  EXPECT_EQ(pong.at("type"), "PONG");
  CloseHandle(pipe);
}

TEST(CliProtocol, OversizedCommandIsRejectedAndNotDispatched) {
  const auto commandName = uniquePipeName(L"_cli");
  const auto logName = uniquePipeName(L"_log");
  std::mutex mutex;
  std::condition_variable condition;
  int dispatched = 0;
  yasb::app::CliServer server(commandName, logName);
  server.start([&](const QString&) {
    std::lock_guard lock(mutex);
    ++dispatched;
    condition.notify_all();
  });

  HANDLE pipe = connectPipe(commandName);
  ASSERT_NE(pipe, INVALID_HANDLE_VALUE) << "timed out connecting to command pipe";
  const std::string oversized = "reload " + std::string(yasb::app::kMaxPipeMessageBytes, 'x');
  ASSERT_TRUE(writeMessage(pipe, oversized));
  std::string response;
  ASSERT_TRUE(readMessage(pipe, response)) << "timed out waiting for oversize-command response";
  EXPECT_EQ(response, "CLI Unknown Command");
  CloseHandle(pipe);

  pipe = connectPipe(commandName);
  ASSERT_NE(pipe, INVALID_HANDLE_VALUE) << "timed out reconnecting to command pipe";
  ASSERT_TRUE(writeMessage(pipe, "reload"));
  ASSERT_TRUE(readMessage(pipe, response)) << "timed out waiting for valid-command response";
  EXPECT_EQ(response, "ACK");
  CloseHandle(pipe);
  std::unique_lock lock(mutex);
  ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(3), [&] { return dispatched > 0; }))
      << "valid command was not dispatched within 3 seconds";
  EXPECT_EQ(dispatched, 1);
}

TEST(CliProtocol, PipesAdmitOnlyTheCurrentUser) {
  const auto commandName = uniquePipeName(L"_cli");
  const auto logName = uniquePipeName(L"_log");
  yasb::app::CliServer server(commandName, logName);
  server.start([](const QString&) {});

  for (const auto& name : {commandName, logName}) {
    HANDLE pipe = connectPipe(name);
    ASSERT_NE(pipe, INVALID_HANDLE_VALUE)
        << "timed out connecting to " << QString::fromWCharArray(name.c_str()).toStdString();
    expectCurrentUserOnly(pipe);
    CloseHandle(pipe);
  }
}
