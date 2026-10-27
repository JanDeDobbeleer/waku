#include "app/cli_server.h"

#include <sddl.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

#include "app/log.h"

namespace yasb::app {
namespace {

constexpr DWORD kBufferSize = 64 * 1024;
constexpr DWORD kPipeInstances = 10;
// A message over the cap is still read to its end (so the client is not left blocked) up to this
// many bytes; beyond that the connection is dropped.
constexpr DWORD kMaxDrainBytes = 1024 * 1024;

// "D:P(A;;GA;;;<user SID>)": protected DACL, generic-all for the process token's user only.
PSECURITY_DESCRIPTOR currentUserSecurityDescriptor() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<unsigned char> buffer(size);
  const bool haveUser =
      size != 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
  CloseHandle(token);
  if (!haveUser) return nullptr;
  wchar_t* sid = nullptr;
  if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid))
    return nullptr;
  const std::wstring sddl = std::wstring(L"D:P(A;;GA;;;") + sid + L")";
  LocalFree(sid);
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                            &descriptor, nullptr)) {
    return nullptr;
  }
  return descriptor;
}

HANDLE createPipe(const wchar_t* name, PSECURITY_DESCRIPTOR security) {
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), security, FALSE};
  return CreateNamedPipeW(
      name, PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
      kPipeInstances, kBufferSize, kBufferSize, 0, &attributes);
}

enum class ReadStatus { Ok, TooLarge, Failed };

// Reads one whole message. ERROR_MORE_DATA means the message continues; anything past
// kMaxPipeMessageBytes is drained and discarded and the result is TooLarge. `size` is the full
// message size seen so far; `error` is set when the result is Failed.
ReadStatus readPipeMessage(HANDLE pipe, QByteArray& out, DWORD& size, DWORD& error) {
  out.clear();
  size = 0;
  error = ERROR_SUCCESS;
  std::vector<char> chunk(kBufferSize);
  for (;;) {
    DWORD read = 0;
    const BOOL ok = ReadFile(pipe, chunk.data(), kBufferSize, &read, nullptr);
    if (!ok && GetLastError() != ERROR_MORE_DATA) {
      error = GetLastError();
      return ReadStatus::Failed;
    }
    size += read;
    if (size <= kMaxPipeMessageBytes) out.append(chunk.data(), static_cast<qsizetype>(read));
    if (ok) return size <= kMaxPipeMessageBytes ? ReadStatus::Ok : ReadStatus::TooLarge;
    if (size > kMaxDrainBytes) {
      error = ERROR_MORE_DATA;
      return ReadStatus::Failed;
    }
  }
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

CliServer::~CliServer() {
  stop();
  if (security_) LocalFree(security_);
}

void CliServer::start(CommandHandler handler) {
  if (!commandThreads_.empty() || logThread_.joinable()) return;
  if (!security_) security_ = currentUserSecurityDescriptor();
  if (!security_) {
    qCritical() << "CLI pipes not started: could not build the current-user security descriptor:"
                << GetLastError();
    return;
  }
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
    HANDLE pipe = createPipe(commandPipeName_.c_str(), security_);
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
    QByteArray message;
    DWORD size = 0;
    DWORD error = ERROR_SUCCESS;
    const ReadStatus status = readPipeMessage(pipe, message, size, error);
    if (status == ReadStatus::TooLarge) {
      qWarning() << "Command-pipe message of" << size << "bytes exceeds the" << kMaxPipeMessageBytes
                 << "byte limit; rejected";
      sendPipeMessage(pipe, commandReply(QString()).toUtf8());
    } else if (status == ReadStatus::Ok && !message.isEmpty()) {
      const QString command = QString::fromUtf8(message).trimmed();
      const QByteArray response = commandReply(command).toUtf8();
      sendPipeMessage(pipe, response);
      if (response == "ACK" && handler_) handler_(command);
    } else if (!stopping_.load()) {
      qWarning() << "Command-pipe read failed:" << error;
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
    HANDLE pipe = createPipe(logPipeName_.c_str(), security_);
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
      QByteArray bytes;
      DWORD size = 0;
      DWORD error = ERROR_SUCCESS;
      const ReadStatus status = readPipeMessage(pipe, bytes, size, error);
      if (status == ReadStatus::TooLarge) {
        qWarning() << "Log-pipe message of" << size << "bytes exceeds the" << kMaxPipeMessageBytes
                   << "byte limit; ignored";
        continue;
      }
      if (status == ReadStatus::Failed || bytes.isEmpty()) break;
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
