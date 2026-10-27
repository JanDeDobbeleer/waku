#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QWidget>
#include <set>
#include <string>
#include <vector>

#include "core/config/loader.h"
#include "registry.h"

namespace yasb::widgets {

// Port of widget_builder.WidgetBuilder: turns the widget names a bar places into widgets, skipping
// (and remembering, warn-once) names that are undefined, invalid or of an unregistered type.
class WidgetBuilder {
 public:
  using Columns = QMap<QString, QList<QString>>;  // "left" | "center" | "right" -> names
  using Widgets = QMap<QString, QList<QWidget*>>;

  WidgetBuilder(const config::LoadResult& config, const WidgetRegistry& registry = WidgetRegistry::instance());

  Widgets buildWidgets(const Columns& columns, const QString& screenName, const QString& barId);
  BaseWidget* buildWidget(const std::string& name, const QString& screenName, const QString& barId);

  const std::set<std::string>& invalidNames() const { return invalid_; }
  const std::set<std::string>& invalidOptions() const { return invalidOptions_; }
  const std::set<std::string>& unknownTypes() const { return unknownTypes_; }
  bool hasErrors() const { return !invalid_.empty() || !invalidOptions_.empty() || !unknownTypes_.empty(); }

  // One line per problem, for logs or an alert dialog.
  std::vector<std::string> errorReport() const;

 private:
  const config::LoadResult& config_;
  const WidgetRegistry& registry_;
  std::set<std::string> invalid_;
  std::set<std::string> invalidOptions_;
  std::set<std::string> unknownTypes_;
};

}  // namespace yasb::widgets
