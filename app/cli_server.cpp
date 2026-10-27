#include "app/cli_server.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <utility>

#include "app/log.h"

namespace yasb::app {
namespace {

constexpr DWORD kBufferSize = 64 * 1024;
constexpr DWORD kPipeInstances = 10;

HANDLE createPipe(const wchar_t* name) {
  return CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX,
                          PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, kPipeInstances,
                          kBufferSize, kBufferSize, 0, nullptr);
}

bool connectPipe(HANDLE pipe) {
  if (ConnectNamedPipe(pipe, nullptr)) return true;
  return GetLastError() == ERROR_PIPE_CONNECTED;
}

void disconnectPipe(HANDLE pipe) {
  DisconnectNamedPipe(pipe);
  CloseHandle(pipe);
}

void wakePipe(const wchar_t* name, int attempts) {
  for (int i = 0; i < attempts; ++i) {
    if (!WaitNamedPipeW(name, 100)) continue;
    HANDLE client =
        CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
  }
}

void sendPipeMessage(HANDLE pipe, const QByteArray& data, bool reportError = true) {
  DWORD written = 0;
  if (!WriteFile(pipe, data.constData(), static_cast<DWORD>(data.size()), &written, nullptr) ||
      written != static_cast<DWORD>(data.size())) {
    if (reportError) qWarning() << "Named-pipe write failed:" << GetLastError();
  }
}

}  // namespace

QString commandReply(const QString& command) {
  const QString trimmed = command.trimmed();
  const QString commandName =
      trimmed.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).toLower();
  static const QStringList supported = {QStringLiteral("stop"), QStringLiteral("reload"),
                                        QStringLiteral("show-bar"), QStringLiteral("hide-bar"),
                                        QStringLiteral("toggle-bar")};
  return supported.contains(commandName) ? QStringLiteral("ACK")
                                         : QStringLiteral("CLI Unknown Command");
}

QByteArray logDataFrame(const QString& data) {
  const QByteArray json =
      QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("DATA")},
                                {QStringLiteral("data"), data}})
          .toJson(QJsonDocument::Compact);
  return json + '\0';
}

QByteArray logPongMessage() {
  return QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("PONG")}})
      .toJson(QJsonDocument::Compact);
}

CliServer::CliServer(std::wstring commandPipeName, std::wstring logPipeName)
    : commandPipeName_(std::move(commandPipeName)), logPipeName_(std::move(logPipeName)) {}

CliServer::~CliServer() { stop(); }

void CliServer::start(CommandHandler handler) {
  if (!commandThreads_.empty() || logThread_.joinable()) return;
  stopping_.store(false);
  handler_ = std::move(handler);
  for (int i = 0; i < 4; ++i) commandThreads_.emplace_back([this] { commandLoop(); });
  logThread_ = std::thread([this] { logLoop(); });
}

void CliServer::stop() {
  if (commandThreads_.empty() && !logThread_.joinable()) return;
  stopping_.store(true);
  wakePipe(commandPipeName_.c_str(), 4);
  wakePipe(logPipeName_.c_str(), 1);
  for (auto& thread : commandThreads_) CancelSynchronousIo(thread.native_handle());
  if (logThread_.joinable()) CancelSynchronousIo(logThread_.native_handle());
  for (auto& thread : commandThreads_) {
    if (thread.joinable()) thread.join();
  }
  commandThreads_.clear();
  if (logThread_.joinable()) logThread_.join();
  setLogObserver({});
}

void CliServer::commandLoop() {
  while (!stopping_.load()) {
    HANDLE pipe = createPipe(commandPipeName_.c_str());
    if (pipe == INVALID_HANDLE_VALUE) {
      qCritical() << "Could not create command pipe:" << GetLastError();
      Sleep(250);
      continue;
    }
    if (stopping_.load()) {
      CloseHandle(pipe);
      break;
    }
    if (!connectPipe(pipe)) {
      const DWORD error = GetLastError();
      disconnectPipe(pipe);
      if (!stopping_.load()) qWarning() << "Command-pipe connection failed:" << error;
      continue;
    }
    char buffer[kBufferSize]{};
    DWORD read = 0;
    if (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
      const QString command = QString::fromUtf8(buffer, static_cast<qsizetype>(read)).trimmed();
      const QByteArray response = commandReply(command).toUtf8();
      sendPipeMessage(pipe, response);
      if (response == "ACK" && handler_) handler_(command);
    } else if (!stopping_.load()) {
      qWarning() << "Command-pipe read failed:" << GetLastError();
    }
    FlushFileBuffers(pipe);
    disconnectPipe(pipe);
  }
}

void CliServer::logLoop() {
  struct PipeState {
    std::mutex mutex;
    HANDLE handle = INVALID_HANDLE_VALUE;
  };
  const auto state = std::make_shared<PipeState>();
  setLogObserver([state](const QString& line) {
    std::lock_guard lock(state->mutex);
    if (state->handle != INVALID_HANDLE_VALUE)
      sendPipeMessage(state->handle, logDataFrame(line), false);
  });
  while (!stopping_.load()) {
    HANDLE pipe = createPipe(logPipeName_.c_str());
    if (pipe == INVALID_HANDLE_VALUE) {
      qCritical() << "Could not create log pipe:" << GetLastError();
      Sleep(250);
      continue;
    }
    if (stopping_.load()) {
      CloseHandle(pipe);
      break;
    }
    if (!connectPipe(pipe)) {
      disconnectPipe(pipe);
      continue;
    }
    {
      std::lock_guard lock(state->mutex);
      state->handle = pipe;
    }
    sendPipeMessage(pipe, logDataFrame(QStringLiteral("Log pipe connected")), false);
    while (!stopping_.load()) {
      char buffer[kBufferSize]{};
      DWORD read = 0;
      if (!ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
      const QByteArray bytes(buffer, static_cast<qsizetype>(read));
      const QList<QByteArray> frames = bytes.split('\0');
      for (const QByteArray& frame : frames) {
        const auto request = nlohmann::json::parse(
            frame.constData(), frame.constData() + frame.size(), nullptr, false);
        if (!request.is_discarded() && request.value("type", std::string()) == "PING") {
          sendPipeMessage(pipe, logPongMessage(), false);
        }
      }
    }
    {
      std::lock_guard lock(state->mutex);
      state->handle = INVALID_HANDLE_VALUE;
    }
    disconnectPipe(pipe);
  }
}

}  // namespace yasb::app
