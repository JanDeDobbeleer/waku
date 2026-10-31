#pragma once

#include <QString>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

// Non-throwing parsers for GlazeWM IPC replies (port of GlazewmClient._process_workspaces,
// _process_binding_modes in glazewm/client.py). The windows of a workspace are only counted:
// nothing reads them, so the parse does not copy them.
namespace wabar::workspaces::glazewm {

using Json = nlohmann::json;

struct Workspace {
  QString name;
  QString displayName;
  bool focus = false;  // global focus: only one workspace has it
  bool isDisplayed = false;
  int numWindows = 0;  // every child of the workspace, windows or not (Python len(children))
};

struct Monitor {
  QString name;
  long long hwnd = 0;
  std::vector<Workspace> workspaces;
};

struct BindingMode {
  std::optional<QString> name;
  std::optional<QString> displayName;
};

enum class TilingDirection { Horizontal, Vertical };

// `data` is the "data" object of a `query monitors` reply. A missing "monitors" key is an empty
// list; null or a non-array value yields nullopt (the message is ignored). Monitors without a
// non-zero integer handle are skipped.
std::optional<std::vector<Monitor>> parseMonitors(const Json& data);

// `data` is the "data" object of a `query binding-modes` reply. Only the first mode counts; an
// empty list means no active mode. Null or a non-array "bindingModes" yields nullopt.
std::optional<BindingMode> parseBindingModes(const Json& data);

// "vertical" is Vertical; a missing or unknown value is Horizontal.
TilingDirection parseTilingDirection(const Json& data);

}  // namespace wabar::workspaces::glazewm
