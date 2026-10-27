#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace yasb::config {

// Environment variable by name (case-insensitive on Windows), UTF-8; empty optional if undefined.
std::optional<std::string> environmentVariable(std::string_view name);

// YASB_CONFIG_HOME, else ~/.config/yasb (settings.py DEFAULT_CONFIG_DIRECTORY).
std::filesystem::path configDir();
std::filesystem::path configPath();       // config.yaml
std::filesystem::path stylesheetPath();   // styles.css

// %LOCALAPPDATA%\YASB (core/utils/system.py app_data_path).
std::filesystem::path dataDir();

}  // namespace yasb::config
