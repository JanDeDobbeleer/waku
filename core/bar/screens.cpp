#include "core/bar/screens.h"

#include <algorithm>
#include <set>

namespace yasb::bar {

namespace {

bool isAll(const std::vector<std::string>& s, const char* token) {
  return s.size() == 1 && s[0] == token;
}

}  // namespace

std::vector<BarScreenAssignment> resolveBarScreens(const std::vector<BarScreens>& bars,
                                                   const std::vector<std::string>& available,
                                                   const std::string& primary,
                                                   std::vector<std::string>* warn) {
  const auto has = [&](const std::string& n) {
    return std::find(available.begin(), available.end(), n) != available.end();
  };
  const auto resolve = [&](const std::string& n) { return n == "primary" ? primary : n; };
  const auto warnMissing = [&](const std::string& n) {
    if (warn) warn->push_back("Screen '" + n + "' from config not found among connected screens.");
  };

  std::set<std::string> assigned;
  for (const auto& bar : bars) {
    if (isAll(bar.screens, "*") || isAll(bar.screens, "**")) continue;
    for (const auto& s : bar.screens) {
      const std::string name = resolve(s);
      if (!has(name)) {
        warnMissing(name);
        continue;
      }
      assigned.insert(name);
    }
  }

  std::vector<BarScreenAssignment> out;
  for (const auto& bar : bars) {
    if (isAll(bar.screens, "*")) {
      for (const auto& screen : available) {
        if (!assigned.count(screen)) out.push_back({bar.barName, screen});
      }
    } else if (isAll(bar.screens, "**")) {
      for (const auto& screen : available) out.push_back({bar.barName, screen});
    } else {
      for (const auto& s : bar.screens) {
        const std::string name = resolve(s);
        if (!has(name)) {
          warnMissing(name);
          continue;
        }
        out.push_back({bar.barName, name});
      }
    }
  }
  return out;
}

QScreen* screenByName(const QList<QScreen*>& screens, const QString& name) {
  for (QScreen* s : screens) {
    if (s->name() == name) return s;
  }
  for (QScreen* s : screens) {
    if (s->name().contains(name)) return s;
  }
  return nullptr;
}

}  // namespace yasb::bar
