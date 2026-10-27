#include "core/widgets/registry.h"

#include <QDebug>

#include "core/config/loader.h"

namespace yasb::widgets {

WidgetRegistry& WidgetRegistry::instance() {
  static WidgetRegistry registry;
  return registry;
}

void WidgetRegistry::add(const std::string& type, WidgetFactory factory) {
  factories_[type] = std::move(factory);
}

BaseWidget* WidgetRegistry::create(const WidgetContext& ctx) const {
  const auto it = factories_.find(ctx.type);
  if (it == factories_.end()) {
    qWarning().noquote() << "Unknown widget type" << QString::fromStdString(ctx.type) << "for"
                         << QString::fromStdString(ctx.name);
    return nullptr;
  }
  BaseWidget* widget = it->second(ctx);
  if (widget) {
    widget->setProperty("screen_name", ctx.screenName);
    widget->setProperty("bar_id", ctx.barId);
  }
  return widget;
}

BaseWidget* WidgetRegistry::create(const config::LoadResult& config, const std::string& name,
                                   const QString& screenName, const QString& barId) const {
  const auto* entry = config.widget(name);
  if (!entry || entry->status != config::WidgetStatus::Ok) {
    qWarning().noquote() << "Widget" << QString::fromStdString(name)
                         << "is undefined or has invalid options";
    return nullptr;
  }
  return create(WidgetContext{name, entry->type, entry->options, screenName, barId});
}

}  // namespace yasb::widgets
