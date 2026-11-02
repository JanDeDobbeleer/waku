#include "core/widgets/widget_builder.h"

#include <QDebug>
#include <exception>
#include <utility>

namespace yasb::widgets {

WidgetBuilder::WidgetBuilder(const config::LoadResult& config, const WidgetRegistry& registry,
                             EventBus& eventBus, bar::GlobalState& globalState,
                             PollingWorkerRegistry& pollingWorkers)
    : config_(config),
      registry_(registry),
      eventBus_(eventBus),
      globalState_(globalState),
      pollingWorkers_(pollingWorkers) {}

BaseWidget* WidgetBuilder::buildWidget(const std::string& name, const QString& screenName,
                                       const QString& barId) {
  const config::WidgetResult* entry = config_.widget(name);
  const auto skip = [&](std::set<std::string>& bucket, const char* why) -> BaseWidget* {
    if (bucket.insert(name).second)
      qWarning().noquote() << "Widget" << QString::fromStdString(name) << why;
    return nullptr;
  };
  if (!entry || entry->status == config::WidgetStatus::Undefined ||
      entry->status == config::WidgetStatus::Error) {
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
  if (!registry_.contains(entry->type))
    return skip(unknownTypes_, "has a type that is not registered");
  // Factories read options with nlohmann; a throw here must not unwind into Qt.
  try {
    TypedWidgetOptions typedOptions;
    if (entry->type == "yasb.cpu.CpuWidget") {
      auto options = config_.widgetOptions<config::options::CpuConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    } else if (entry->type == "yasb.memory.MemoryWidget") {
      auto options = config_.widgetOptions<config::options::MemoryConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    } else if (entry->type == "yasb.battery.BatteryWidget") {
      auto options = config_.widgetOptions<config::options::BatteryConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    } else if (entry->type == "yasb.github.GithubWidget") {
      auto options = config_.widgetOptions<config::options::GithubConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    } else if (entry->type == "komorebi.workspaces.WorkspaceWidget") {
      auto options = config_.widgetOptions<config::options::KomorebiWorkspacesConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    } else if (entry->type == "komorebi.active_layout.ActiveLayoutWidget") {
      auto options = config_.widgetOptions<config::options::ActiveLayoutConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    } else if (entry->type == "yasb.clock.ClockWidget") {
      auto options = config_.widgetOptions<config::options::ClockConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    } else if (entry->type == "yasb.media.MediaWidget") {
      auto options = config_.widgetOptions<config::options::MediaWidgetConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    } else if (entry->type == "yasb.wifi.WifiWidget") {
      auto options = config_.widgetOptions<config::options::WifiConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    } else if (entry->type == "yasb.bluetooth.BluetoothWidget") {
      auto options = config_.widgetOptions<config::options::BluetoothConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    } else if (entry->type == "yasb.notifications.NotificationsWidget") {
      auto options = config_.widgetOptions<config::options::NotificationsConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    } else if (entry->type == "yasb.wallpapers.WallpapersWidget") {
      auto options = config_.widgetOptions<config::options::WallpapersConfig>(name);
      if (!options) return skip(invalidOptions_, "has invalid options");
      typedOptions = std::move(*options);
    }
    return registry_.create(WidgetContext{name, entry->type, entry->options, screenName, barId,
                                          eventBus_, globalState_, pollingWorkers_,
                                          std::move(typedOptions)});
  } catch (const std::exception& error) {
    qCritical().noquote() << "Widget" << QString::fromStdString(name)
                          << "could not be created:" << QString::fromUtf8(error.what());
    return nullptr;
  }
}

WidgetBuilder::Widgets WidgetBuilder::buildWidgets(const Columns& columns,
                                                   const QString& screenName,
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
  for (const auto& n : unknownTypes_)
    lines.push_back("Widget '" + n + "' has a missing or unknown type");
  return lines;
}

}  // namespace yasb::widgets
