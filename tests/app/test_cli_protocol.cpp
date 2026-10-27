#include <gtest/gtest.h>
// clang-format off
#include <windows.h>
#include <aclapi.h>
// clang-format on

#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "app/cli_server.h"

namespace {

std::wstring uniquePipeName(const wchar_t* suffix) {
  return L"\\\\.\\pipe\\yasb_native_test_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
         std::to_wstring(GetTickCount64()) + suffix;
}

HANDLE connectPipe(const std::wstring& name) {
  const ULONGLONG deadline = GetTickCount64() + 3000;
  while (GetTickCount64() < deadline) {
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              0, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) return pipe;
    if (GetLastError() == ERROR_PIPE_BUSY)
      WaitNamedPipeW(name.c_str(), 100);
    else
      Sleep(20);
  }
  return INVALID_HANDLE_VALUE;
}

bool readMessage(HANDLE pipe, std::string& data) {
  const ULONGLONG deadline = GetTickCount64() + 3000;
  DWORD available = 0;
  while (GetTickCount64() < deadline) {
    if (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) break;
    Sleep(10);
  }
  if (available == 0) return false;
  char buffer[65536]{};
  DWORD read = 0;
  if (!ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr)) return false;
  data.assign(buffer, read);
  return true;
}

bool writeMessage(HANDLE pipe, const std::string& data) {
  DWORD written = 0;
  return WriteFile(pipe, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
         written == static_cast<DWORD>(data.size());
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
  QString received;
  yasb::app::CliServer server(commandName, logName);
  server.start([&](const QString& command) {
    std::lock_guard lock(mutex);
    received = command;
  });

  HANDLE pipe = connectPipe(commandName);
  ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
  ASSERT_TRUE(writeMessage(pipe, "hide-bar --screen DISPLAY1"));
  std::string response;
  ASSERT_TRUE(readMessage(pipe, response));
  EXPECT_EQ(response, "ACK");
  CloseHandle(pipe);
  for (int i = 0; i < 100; ++i) {
    {
      std::lock_guard lock(mutex);
      if (!received.isEmpty()) break;
    }
    Sleep(10);
  }
  {
    std::lock_guard lock(mutex);
    EXPECT_EQ(received, QStringLiteral("hide-bar --screen DISPLAY1"));
  }

  pipe = connectPipe(commandName);
  ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
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
  ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
  std::string frame;
  ASSERT_TRUE(readMessage(pipe, frame));
  ASSERT_FALSE(frame.empty());
  EXPECT_EQ(frame.back(), '\0');
  const auto data = nlohmann::json::parse(frame.data(), frame.data() + frame.size() - 1);
  EXPECT_EQ(data.at("type"), "DATA");

  ASSERT_TRUE(writeMessage(pipe, R"({"type":"PING"})"));
  ASSERT_TRUE(readMessage(pipe, frame));
  const auto pong = nlohmann::json::parse(frame.data(), frame.data() + frame.size());
  EXPECT_EQ(pong.at("type"), "PONG");
  CloseHandle(pipe);
}

TEST(CliProtocol, OversizedCommandIsRejectedAndNotDispatched) {
  const auto commandName = uniquePipeName(L"_cli");
  const auto logName = uniquePipeName(L"_log");
  std::mutex mutex;
  int dispatched = 0;
  yasb::app::CliServer server(commandName, logName);
  server.start([&](const QString&) {
    std::lock_guard lock(mutex);
    ++dispatched;
  });

  HANDLE pipe = connectPipe(commandName);
  ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
  const std::string oversized = "reload " + std::string(yasb::app::kMaxPipeMessageBytes, 'x');
  ASSERT_TRUE(writeMessage(pipe, oversized));
  std::string response;
  ASSERT_TRUE(readMessage(pipe, response));
  EXPECT_EQ(response, "CLI Unknown Command");
  CloseHandle(pipe);

  pipe = connectPipe(commandName);
  ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
  ASSERT_TRUE(writeMessage(pipe, "reload"));
  ASSERT_TRUE(readMessage(pipe, response));
  EXPECT_EQ(response, "ACK");
  CloseHandle(pipe);
  for (int i = 0; i < 100; ++i) {
    {
      std::lock_guard lock(mutex);
      if (dispatched > 0) break;
    }
    Sleep(10);
  }
  std::lock_guard lock(mutex);
  EXPECT_EQ(dispatched, 1);
}

TEST(CliProtocol, PipesAdmitOnlyTheCurrentUser) {
  const auto commandName = uniquePipeName(L"_cli");
  const auto logName = uniquePipeName(L"_log");
  yasb::app::CliServer server(commandName, logName);
  server.start([](const QString&) {});

  for (const auto& name : {commandName, logName}) {
    HANDLE pipe = connectPipe(name);
    ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
    expectCurrentUserOnly(pipe);
    CloseHandle(pipe);
  }
}
