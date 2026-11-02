#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <vector>

// Non-throwing reads of `komorebic state` JSON (port of the KomorebiClient getters).
namespace waku::workspaces::komorebi {

using Json = nlohmann::json;

// `node[key]` when node is an object that has `key`, else nullptr.
const Json* child(const Json* node, std::string_view key);

// `node[key]["elements"]` when it is an array, else nullptr.
const Json* elements(const Json* node, std::string_view key);

// Python truthiness of a JSON value (null, false, 0, "", [] and {} are falsy).
bool truthy(const Json* value);

std::optional<long long> integer(const Json* value);

// Screen whose "id" equals `monitorId` (get_screen_by_hwnd); `index` receives its position.
const Json* screenById(const Json& state, long long monitorId, int* index = nullptr);

// screen["workspaces"]["elements"] or nullptr.
const Json* workspaceList(const Json* screen);
int workspaceCount(const Json* screen);
const Json* workspaceAt(const Json* screen, int index);

// get_focused_workspace: index of screen["workspaces"]["focused"] when that workspace exists.
std::optional<int> focusedWorkspaceIndex(const Json* screen);

// get_num_windows: true when the workspace has any floating, tiled, monocle or maximized window.
bool hasWindows(const Json* workspace);

// len(_get_all_windows_in_workspace): container windows, monocle windows and (unless hidden)
// floating windows.
int windowCount(const Json* workspace, bool hideFloating);

// _get_all_windows_in_workspace as hwnds, in Python's order: container windows, the monocle
// container's windows, then (unless hidden) floating windows. Windows without a numeric hwnd are
// skipped.
std::vector<long long> windowHwnds(const Json* workspace, bool hideFloating);

}  // namespace waku::workspaces::komorebi
