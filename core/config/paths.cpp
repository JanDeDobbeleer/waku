#include "core/config/paths.h"

#include <windows.h>

#include <vector>

namespace yasb::config {

namespace {

std::string toUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                       nullptr, nullptr);
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), size, nullptr,
                      nullptr);
  return out;
}

std::wstring toWide(std::string_view s) {
  if (s.empty()) return {};
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), size);
  return out;
}

std::optional<std::filesystem::path> pathFromEnv(const char* name) {
  auto value = environmentVariable(name);
  if (!value || value->empty()) return std::nullopt;
  return std::filesystem::path(toWide(*value));
}

}  // namespace

std::optional<std::string> environmentVariable(std::string_view name) {
  const std::wstring wideName = toWide(name);
  const DWORD needed = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
  if (needed == 0) {
    // 0 is both "undefined" and "defined but empty"
    if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
    return std::string();
  }
  std::wstring buffer(needed, L'\0');
  const DWORD written = GetEnvironmentVariableW(wideName.c_str(), buffer.data(), needed);
  buffer.resize(written);
  return toUtf8(buffer);
}

std::filesystem::path configDir() {
  if (auto home = pathFromEnv("YASB_CONFIG_HOME")) return *home;
  std::filesystem::path userHome = pathFromEnv("USERPROFILE").value_or(std::filesystem::path());
  return userHome / ".config" / "yasb";
}

std::filesystem::path configPath() { return configDir() / "config.yaml"; }

std::filesystem::path stylesheetPath() { return configDir() / "styles.css"; }

std::filesystem::path dataDir() {
  return pathFromEnv("LOCALAPPDATA").value_or(std::filesystem::path()) / "YASB";
}

}  // namespace yasb::config
