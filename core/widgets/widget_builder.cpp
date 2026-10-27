#include "widget_builder.h"

#include <QDebug>

namespace yasb::widgets {

WidgetBuilder::WidgetBuilder(const config::LoadResult& config, const WidgetRegistry& registry)
    : config_(config), registry_(registry) {}

BaseWidget* WidgetBuilder::buildWidget(const std::string& name, const QString& screenName, const QString& barId) {
  const config::WidgetResult* entry = config_.widget(name);
  const auto skip = [&](std::set<std::string>& bucket, const char* why) -> BaseWidget* {
    if (bucket.insert(name).second) qWarning().noquote() << "Widget" << QString::fromStdString(name) << why;
    return nullptr;
  };
  if (!entry || entry->status == config::WidgetStatus::Undefined || entry->status == config::WidgetStatus::Error) {
    return skip(invalid_, "is not defined in the config");
  }
  switch (entry->status) {
    case config::WidgetStatus::InvalidOptions:
      return skip(invalidOptions_, "has invalid options");
    case config::WidgetStatus::UnknownType:
    case config::WidgetStatus::MissingType:
      return skip(unknownTypes_, "has a missing or unknown type");
    default:
      break;
  }
  if (!registry_.contains(entry->type)) return skip(unknownTypes_, "has a type that is not registered");
  return registry_.create(config_, name, screenName, barId);
}

WidgetBuilder::Widgets WidgetBuilder::buildWidgets(const Columns& columns, const QString& screenName,
                                                   const QString& barId) {
  Widgets result;
  for (auto it = columns.cbegin(); it != columns.cend(); ++it) {
    QList<QWidget*>& out = result[it.key()];
    for (const QString& name : it.value()) {
      if (BaseWidget* w = buildWidget(name.toStdString(), screenName, barId)) out.append(w);
    }
  }
  return result;
}

std::vector<std::string> WidgetBuilder::errorReport() const {
  std::vector<std::string> lines;
  for (const auto& n : invalid_) lines.push_back("Widget '" + n + "' is placed but not defined");
  for (const auto& n : invalidOptions_) lines.push_back("Widget '" + n + "' has invalid options");
  for (const auto& n : unknownTypes_) lines.push_back("Widget '" + n + "' has a missing or unknown type");
  return lines;
}

}  // namespace yasb::widgets
