#pragma once

#include <QList>
#include <QScreen>
#include <QString>
#include <string>
#include <utility>
#include <vector>

namespace yasb::bar {

struct BarScreens {
  std::string barName;
  std::vector<std::string> screens;  // configured: ["*"], ["**"] or names ("primary" allowed)
};

struct BarScreenAssignment {
  std::string barName;
  std::string screenName;
};

// Port of BarManager.initialize_bars screen selection: "*" means every screen no bar names
// explicitly, "**" every screen, otherwise the named ones. Unknown names are reported through
// `warn` (the caller logs) and skipped. Order follows the config.
std::vector<BarScreenAssignment> resolveBarScreens(const std::vector<BarScreens>& bars,
                                                   const std::vector<std::string>& availableScreens,
                                                   const std::string& primaryScreen,
                                                   std::vector<std::string>* warn = nullptr);

// Exact name first, then the first screen whose name contains `name`
// (utilities.get_screen_by_name).
QScreen* screenByName(const QList<QScreen*>& screens, const QString& name);

}  // namespace yasb::bar
