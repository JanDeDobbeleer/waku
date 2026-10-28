#include "widgets/komorebi/komorebi_state.h"

namespace yasb::widgets::komorebi {

const Json* child(const Json* node, std::string_view key) {
  if (!node || !node->is_object()) return nullptr;
  const auto it = node->find(key);
  return it == node->end() ? nullptr : &*it;
}

const Json* elements(const Json* node, std::string_view key) {
  const Json* list = child(child(node, key), "elements");
  return list && list->is_array() ? list : nullptr;
}

bool truthy(const Json* value) {
  if (!value || value->is_null()) return false;
  if (value->is_boolean()) return value->get<bool>();
  if (value->is_number_integer()) return value->get<long long>() != 0;
  if (value->is_number()) return value->get<double>() != 0.0;
  if (value->is_string()) return !value->get_ref<const std::string&>().empty();
  if (value->is_array() || value->is_object()) return !value->empty();
  return true;
}

std::optional<long long> integer(const Json* value) {
  if (!value || !value->is_number_integer()) return std::nullopt;
  return value->get<long long>();
}

const Json* screenById(const Json& state, long long monitorId, int* index) {
  const Json* screens = elements(&state, "monitors");
  if (!screens) return nullptr;
  int i = 0;
  for (const Json& screen : *screens) {
    if (integer(child(&screen, "id")) == monitorId) {
      if (index) *index = i;
      return &screen;
    }
    ++i;
  }
  return nullptr;
}

const Json* workspaceList(const Json* screen) { return elements(screen, "workspaces"); }

int workspaceCount(const Json* screen) {
  const Json* list = workspaceList(screen);
  return list ? static_cast<int>(list->size()) : 0;
}

const Json* workspaceAt(const Json* screen, int index) {
  const Json* list = workspaceList(screen);
  if (!list || index < 0 || index >= static_cast<int>(list->size())) return nullptr;
  return &list->at(static_cast<size_t>(index));
}

std::optional<int> focusedWorkspaceIndex(const Json* screen) {
  const auto focused = integer(child(child(screen, "workspaces"), "focused"));
  if (!focused || !workspaceAt(screen, static_cast<int>(*focused))) return std::nullopt;
  return static_cast<int>(*focused);
}

namespace {

int containerWindows(const Json& container) {
  const Json* windows = elements(&container, "windows");
  return windows ? static_cast<int>(windows->size()) : 0;
}

}  // namespace

bool hasWindows(const Json* workspace) {
  if (const Json* floating = child(workspace, "floating_windows")) {
    if (floating->is_object()) {
      if (truthy(child(floating, "elements"))) return true;
    } else if (truthy(floating)) {
      return true;
    }
  }
  if (const Json* containers = elements(workspace, "containers")) {
    for (const Json& container : *containers) {
      if (truthy(child(child(&container, "windows"), "elements"))) return true;
    }
  }
  if (const Json* monocle = child(workspace, "monocle_container"); monocle && monocle->is_object())
    return true;
  if (const Json* maximized = child(workspace, "maximized_window");
      maximized && maximized->is_object())
    return true;
  return false;
}

int windowCount(const Json* workspace, bool hideFloating) {
  int count = 0;
  if (const Json* containers = elements(workspace, "containers")) {
    for (const Json& container : *containers) count += containerWindows(container);
  }
  if (const Json* monocle = child(workspace, "monocle_container");
      monocle && monocle->is_object() && !monocle->empty()) {
    count += containerWindows(*monocle);
  }
  if (!hideFloating) {
    if (const Json* floating = elements(workspace, "floating_windows"))
      count += static_cast<int>(floating->size());
  }
  return count;
}

}  // namespace yasb::widgets::komorebi
