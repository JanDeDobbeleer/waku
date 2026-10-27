"""Generates the typed widget option structs in this folder from schema/schema.json.

    python core/config/options/generate_options.py

One header per widget plus common.h for the structs shared between widgets. The headers are
committed; rerun the script when schema.json is regenerated from the Python models.
"""

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCHEMA = HERE.parents[2] / "schema" / "schema.json"

WIDGETS = {
    "control_center": "ControlCenterConfig",
    "komorebi_workspaces": "KomorebiWorkspacesConfig",
    "komorebi_active_layout": "ActiveLayoutConfig",
    "taskbar": "TaskbarConfig",
    "clock": "ClockConfig",
    "media": "MediaWidgetConfig",
    "systray": "SystrayWidgetConfig",
    "wallpapers": "WallpapersConfig",
    "github": "GithubConfig",
    "notifications": "NotificationsConfig",
    "cpu": "CpuConfig",
    "memory": "MemoryConfig",
    "bluetooth": "BluetoothConfig",
    "wifi": "WifiConfig",
    "battery": "BatteryConfig",
}

CPP_KEYWORDS = {
    "alignas", "alignof", "and", "auto", "bool", "break", "case", "catch", "char", "class", "const",
    "continue", "default", "delete", "do", "double", "else", "enum", "explicit", "export", "extern",
    "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "namespace", "new", "not",
    "operator", "or", "private", "protected", "public", "register", "return", "short", "signed", "sizeof",
    "static", "struct", "switch", "template", "this", "throw", "true", "try", "typedef", "typename",
    "union", "unsigned", "using", "virtual", "void", "volatile", "while", "xor",
}

COMMON_PREAMBLE = """#pragma once

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace yasb::config::options {

// Missing keys keep the member's default; null clears an optional.
template <class T>
void readField(const nlohmann::json& j, const char* key, T& out) {
  if (auto it = j.find(key); it != j.end()) it->get_to(out);
}

template <class T>
void readField(const nlohmann::json& j, const char* key, std::optional<T>& out) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) {
    out.reset();
  } else {
    out = it->template get<T>();
  }
}
"""


class Generator:
    def __init__(self, schema):
        self.defs = schema["$defs"]

    def ref_name(self, node):
        return node["$ref"].rsplit("/", 1)[1]

    def is_struct(self, name):
        d = self.defs[name]
        return d.get("type") == "object" and "properties" in d

    def ctype(self, node):
        if "$ref" in node:
            name = self.ref_name(node)
            return name if self.is_struct(name) else self.ctype(self.defs[name])
        if "anyOf" in node:
            members = node["anyOf"]
            non_null = [m for m in members if m.get("type") != "null"]
            if len(members) == 2 and len(non_null) == 1:
                return f"std::optional<{self.ctype(non_null[0])}>"
            return "nlohmann::json"
        if "enum" in node or "const" in node:
            sample = (node.get("enum") or [node.get("const")])[0]
            return "std::string" if isinstance(sample, str) else "nlohmann::json"
        t = node.get("type")
        if t == "string":
            return "std::string"
        if t == "integer":
            return "int"
        if t == "number":
            return "double"
        if t == "boolean":
            return "bool"
        if t == "array":
            return f"std::vector<{self.ctype(node['items'])}>" if "items" in node else "nlohmann::json"
        if t == "object":
            ap = node.get("additionalProperties")
            if isinstance(ap, dict):
                return f"std::map<std::string, {self.ctype(ap)}>"
        return "nlohmann::json"

    def deps(self, name):
        """Struct defs that fields of `name` refer to, in field order."""
        found = []

        def visit(node):
            if "$ref" in node:
                target = self.ref_name(node)
                if self.is_struct(target):
                    found.append(target)
                else:
                    visit(self.defs[target])
            for key in ("anyOf",):
                for member in node.get(key, []):
                    visit(member)
            if "items" in node:
                visit(node["items"])
            if isinstance(node.get("additionalProperties"), dict):
                visit(node["additionalProperties"])

        for prop in self.defs[name]["properties"].values():
            visit(prop)
        return found

    def closure(self, root):
        order, seen = [], set()

        def dfs(name):
            if name in seen:
                return
            seen.add(name)
            for dep in self.deps(name):
                dfs(dep)
            order.append(name)

        dfs(root)
        return order

    def emit_struct(self, name):
        lines = [f"struct {name} {{"]
        reads = []
        for key, prop in self.defs[name]["properties"].items():
            member = key + "_" if key in CPP_KEYWORDS else key
            ctype = self.ctype(prop)
            init = ""
            default = prop.get("default")
            if not ctype.startswith("std::optional"):
                if ctype == "bool" and isinstance(default, bool):
                    init = f" = {'true' if default else 'false'}"
                elif ctype in ("int", "double") and isinstance(default, (int, float)) and not isinstance(default, bool):
                    init = f" = {default!r}"
                elif ctype in ("int", "double"):
                    init = " = 0"
                elif ctype == "bool":
                    init = " = false"
            lines.append(f"  {ctype} {member}{init};")
            reads.append(f'  readField(j, "{key}", s.{member});')
        lines.append("};")
        lines.append("")
        lines.append(f"inline void from_json(const nlohmann::json& j, {name}& s) {{")
        lines.extend(reads)
        lines.append("}")
        return "\n".join(lines)


def main():
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    gen = Generator(schema)
    closures = {w: gen.closure(root) for w, root in WIDGETS.items()}
    counts = {}
    for names in closures.values():
        for n in names:
            counts[n] = counts.get(n, 0) + 1
    shared = {n for n, c in counts.items() if c > 1}

    banner = "// Generated by generate_options.py from schema/schema.json. Do not edit.\n"
    common_order = []
    for names in closures.values():
        for n in names:
            if n in shared and n not in common_order:
                common_order.append(n)
    # dependency order across widgets: shared structs only depend on other shared structs
    common_sorted = []
    for n in common_order:
        for dep in gen.closure(n):
            if dep not in common_sorted:
                common_sorted.append(dep)
    common = banner + COMMON_PREAMBLE + "\n" + "\n\n".join(gen.emit_struct(n) for n in common_sorted)
    common += "\n\n}  // namespace yasb::config::options\n"
    (HERE / "common.h").write_text(common, encoding="utf-8", newline="\r\n")

    for widget, names in closures.items():
        own = [n for n in names if n not in shared]
        text = banner + '#pragma once\n\n#include "core/config/options/common.h"\n\n'
        text += "namespace yasb::config::options {\n\n"
        text += "\n\n".join(gen.emit_struct(n) for n in own)
        text += "\n\n}  // namespace yasb::config::options\n"
        (HERE / f"{widget}.h").write_text(text, encoding="utf-8", newline="\r\n")

    all_h = banner + "#pragma once\n\n" + "".join(f'#include "core/config/options/{w}.h"\n' for w in sorted(WIDGETS))
    (HERE / "all.h").write_text(all_h, encoding="utf-8", newline="\r\n")
    print(f"{len(WIDGETS)} widgets, {len(shared)} shared structs", file=sys.stderr)


if __name__ == "__main__":
    main()
