#include "core/widgets/registry.h"

#include <QDebug>

#include "core/config/loader.h"

namespace yasb::widgets {

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

}  // namespace yasb::widgets
