#pragma once

#include <QString>
#include <cstdint>
#include <optional>

#include "features/taskbar/window_system.h"

namespace waku::taskbar {

// The App User Model ID of a window: the window's own property first, then the one its process
// runs under (Store apps). nullopt when neither exists.
std::optional<QString> aumidForWindow(WindowHandle hwnd, std::uint32_t pid);

// The folder an Explorer window shows, as Python's collect_window_context reads it from
// Shell.Application: a file-system path, or the raw LocationURL for shell folders. nullopt when
// the window is not found or the shell does not answer. Windows without a LocationURL fall back to
// Document.Folder.Self.Path when that is a CLSID path (the Recycle Bin).
std::optional<QString> explorerLocation(WindowHandle hwnd);

// os.path.normcase(Path.resolve()): absolute, symlinks resolved when the file exists, lower case,
// backslashes.
QString normalizePath(const QString& path);

// Path variants for versioned apps: {normalized, stable} where stable drops an Electron "app-x.y"
// or a Scoop version directory. One entry when the path is not versioned (normalizedTargets).
std::vector<QString> normalizedTargets(const QString& exePath);

// PinManager._unique_id: "aumid:<id>", "explorer:<folder>" or "path:<stable exe>[|host args]" for
// mmc.exe / rundll32.exe. nullopt when the window cannot be identified.
std::optional<QString> appUniqueId(WindowHandle hwnd, const QString& exePath, std::uint32_t pid);

// The arguments a process was started with after its exe (NtQueryInformationProcess class 60, see
// docs/architecture.md); nullopt when unreadable.
std::optional<QString> hostArguments(const QString& exePath, std::uint32_t pid);

// The part of a command line after the program: split off the quoted or first token.
QString commandLineArguments(const QString& commandLine);

}  // namespace waku::taskbar
