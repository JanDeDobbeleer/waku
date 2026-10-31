#include "features/workspaces/glazewm_state.h"

#include <string>

namespace wabar::workspaces::glazewm {
namespace {

const Json* member(const Json& node, const char* key) {
  if (!node.is_object()) return nullptr;
  const auto it = node.find(key);
  return it == node.end() ? nullptr : &*it;
}

QString text(const Json& node, const char* key) {
  const Json* value = member(node, key);
  if (!value || !value->is_string()) return {};
  return QString::fromStdString(value->get_ref<const std::string&>());
}

bool flag(const Json& node, const char* key) {
  const Json* value = member(node, key);
  return value && value->is_boolean() && value->get<bool>();
}

long long integerOr0(const Json& node, const char* key) {
  const Json* value = member(node, key);
  if (!value || !value->is_number_integer()) return 0;
  return value->get<long long>();
}

bool isType(const Json& node, const char* type) {
  const Json* value = member(node, "type");
  return value && value->is_string() && value->get_ref<const std::string&>() == type;
}

const Json* childArray(const Json& node) {
  const Json* children = member(node, "children");
  return children && children->is_array() ? children : nullptr;
}

Workspace readWorkspace(const Json& node) {
  Workspace workspace;
  workspace.name = text(node, "name");
  workspace.displayName = text(node, "displayName");
  workspace.isDisplayed = flag(node, "isDisplayed");
  workspace.focus = flag(node, "hasFocus");
  const Json* children = childArray(node);
  workspace.numWindows = children ? static_cast<int>(children->size()) : 0;
  return workspace;
}

}  // namespace

std::optional<std::vector<Monitor>> parseMonitors(const Json& data) {
  std::vector<Monitor> monitors;
  const Json* list = member(data, "monitors");
  if (!list) return monitors;
  if (!list->is_array()) return std::nullopt;
  for (const Json& node : *list) {
    if (!node.is_object()) continue;
    const long long handle = integerOr0(node, "handle");
    if (handle == 0) continue;
    Monitor monitor;
    monitor.name = text(node, "hardwareId");
    if (monitor.name.isEmpty()) monitor.name = QStringLiteral("Unknown_%1").arg(handle);
    monitor.hwnd = handle;
    if (const Json* children = childArray(node)) {
      for (const Json& child : *children) {
        if (child.is_object() && isType(child, "workspace")) {
          monitor.workspaces.push_back(readWorkspace(child));
        }
      }
    }
    monitors.push_back(std::move(monitor));
  }
  return monitors;
}

std::optional<BindingMode> parseBindingModes(const Json& data) {
  BindingMode mode;
  const Json* list = member(data, "bindingModes");
  if (!list) return mode;
  if (!list->is_array()) return std::nullopt;
  if (list->empty() || !list->front().is_object()) return mode;
  const Json& first = list->front();
  const Json* name = member(first, "name");
  if (name && name->is_string()) mode.name = text(first, "name");
  const Json* display = member(first, "displayName");
  if (display && display->is_string()) mode.displayName = text(first, "displayName");
  return mode;
}

TilingDirection parseTilingDirection(const Json& data) {
  const Json* value = member(data, "tilingDirection");
  if (value && value->is_string() && value->get_ref<const std::string&>() == "vertical") {
    return TilingDirection::Vertical;
  }
  return TilingDirection::Horizontal;
}

}  // namespace wabar::workspaces::glazewm
