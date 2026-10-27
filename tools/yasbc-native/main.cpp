#include <windows.h>

#include <cstdio>
#include <string>

namespace {

constexpr wchar_t kCommandPipeName[] = L"\\\\.\\pipe\\yasb_pipe_cli";

int sendCommand(const std::string& command) {
  if (!WaitNamedPipeW(kCommandPipeName, 3000)) {
    std::fprintf(stderr, "Could not connect to yasb-native (named pipe unavailable).\n");
    return 1;
  }
  HANDLE pipe = CreateFileW(kCommandPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    std::fprintf(stderr, "Could not open command pipe: %lu\n", GetLastError());
    return 1;
  }
  DWORD written = 0;
  if (!WriteFile(pipe, command.data(), static_cast<DWORD>(command.size()), &written, nullptr) ||
      written != static_cast<DWORD>(command.size())) {
    std::fprintf(stderr, "Could not send command: %lu\n", GetLastError());
    CloseHandle(pipe);
    return 1;
  }
  char response[65536]{};
  DWORD read = 0;
  const bool ok = ReadFile(pipe, response, sizeof(response), &read, nullptr) && read > 0;
  CloseHandle(pipe);
  if (!ok) {
    std::fprintf(stderr, "Could not read command response: %lu\n", GetLastError());
    return 1;
  }
  std::fwrite(response, 1, read, stdout);
  std::fputc('\n', stdout);
  return std::string(response, read) == "ACK" ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(
        stderr, "Usage: yasbc-native <reload|stop|show-bar|hide-bar|toggle-bar> [--screen NAME]\n");
    return 2;
  }
  std::string command;
  for (int i = 1; i < argc; ++i) {
    if (!command.empty()) command.push_back(' ');
    command += argv[i];
  }
  return sendCommand(command);
}
