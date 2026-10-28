#include "widgets/control_center/control_center_widget.h"

#include <QDebug>
#include <QVBoxLayout>

#include "core/widgets/polling_worker_registry.h"

namespace yasb::widgets {
namespace {

QString qs(const std::string& text) { return QString::fromStdString(text); }

}  // namespace

ControlCenterWidget::ControlCenterWidget(const WidgetContext& context, ControlCenterSystem system)
    : BaseWidget(
          qs(context.name), context.eventBus,
          QStringLiteral("control-center-widget ") +
              qs(std::get<config::options::ControlCenterConfig>(context.typedOptions).class_name),
          0),
      options_(std::get<config::options::ControlCenterConfig>(context.typedOptions)),
      system_(std::move(system)),
      globalState_(context.globalState) {
  buildWidgetLabel(qs(options_.label), qs(options_.label_alt));

  if (system_.brightness) {
    connect(system_.brightness, &platform::BrightnessControl::brightnessChanged, this,
            [this](qulonglong monitor, int percent) { onBrightnessChanged(monitor, percent); });
  }
  for (platform::AudioEndpoints* audio : {system_.output, system_.input}) {
    if (!audio) continue;
    connect(audio, &platform::AudioEndpoints::volumeChanged, this, [this] { refreshAudioState(); });
    connect(audio, &platform::AudioEndpoints::deviceChanged, this, [this] { refreshPopupState(); });
  }

  registerCallback(QStringLiteral("toggle_menu"), [this](const QStringList&) { toggleMenu(); });
  setCallbackBinding({qs(options_.callbacks.on_left), qs(options_.callbacks.on_middle),
                      qs(options_.callbacks.on_right), QStringLiteral("do_nothing")});
}

SectionContext ControlCenterWidget::sectionContext() {
  return SectionContext{system_, globalState_, options_.tooltip, [this] { refreshPopupState(); },
                        [this] {
                          if (dialog_) dialog_->hideNow();
                        }};
}

void ControlCenterWidget::toggleMenu() {
  if (dialog_ && dialog_->isVisible()) {
    dialog_->hideAnimated();
  } else {
    showMenu();
  }
}

void ControlCenterWidget::showMenu() {
  try {
    if (!dialog_) {
      const auto& popup = options_.popup;
      dialog_ = new MenuPopup(this, popup.blur, popup.round_corners, qs(popup.round_corners_type),
                              qs(popup.border_color), globalState_.isDark(), true);
      dialog_->setCssClass(QStringLiteral("control-center-menu"));
      auto* layout = new QVBoxLayout(dialog_);
      layout->setContentsMargins(0, 0, 0, 0);
      layout->setSpacing(0);
      layout->setAlignment(Qt::AlignTop);

      const auto& sections = options_.sections;
      for (const std::string& name : options_.sections_order) {
        if (name == "system_controls" && sections.system_controls.show) {
          systemControls_ =
              new SystemControlsSection(dialog_, sections.system_controls, sectionContext());
          layout->addWidget(systemControls_);
        } else if (name == "quick_actions" && sections.quick_actions.show) {
          quickActions_ =
              new QuickActionsSection(dialog_, sections.quick_actions, sectionContext());
          layout->addWidget(quickActions_);
        } else if (name == "sliders" && sections.sliders.show) {
          const qulonglong monitor = system_.monitorOf ? system_.monitorOf(this) : 0;
          sliders_ = new SlidersSection(dialog_, sections.sliders, sectionContext(), monitor);
          layout->addWidget(sliders_);
        } else if (name == "power" && sections.power.show) {
          power_ = new PowerSection(dialog_, sections.power, sectionContext());
          layout->addWidget(power_);
        } else if (name == "media" && sections.media.show) {
          media_ = new MediaSection(dialog_, sections.media, sectionContext());
          layout->addWidget(media_);
        }
      }
    }

    dialog_->setPosition(qs(options_.popup.alignment), qs(options_.popup.direction),
                         options_.popup.offset_left, options_.popup.offset_top);
    dialog_->showToggled();
    refreshPopupState();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Control center menu failed:" << error.what();
  }
}

void ControlCenterWidget::refreshPopupState() {
  if (!dialog_ || !dialog_->isVisible()) return;
  try {
    if (systemControls_) systemControls_->refreshState();
    if (quickActions_) quickActions_->refreshState();
    if (sliders_) sliders_->refreshState();
    if (power_) power_->refreshState();
    if (media_) media_->refreshState();
    if (system_.brightness) system_.brightness->refreshNow();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Control center refresh failed:" << error.what();
  }
}

void ControlCenterWidget::refreshAudioState() {
  if (!dialog_ || !dialog_->isVisible()) return;
  try {
    if (sliders_) sliders_->refreshState();
    if (quickActions_) quickActions_->refreshState();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Control center audio refresh failed:" << error.what();
  }
}

void ControlCenterWidget::onBrightnessChanged(qulonglong monitor, int percent) {
  if (!dialog_ || !dialog_->isVisible() || !sliders_) return;
  sliders_->updateBrightness(monitor, percent);
}

void registerControlCenterWidgets(WidgetRegistry& registry) {
  registry.add("yasb.control_center.ControlCenterWidget", [](const WidgetContext& context) {
    const auto& options = std::get<config::options::ControlCenterConfig>(context.typedOptions);
    return new ControlCenterWidget(context,
                                   makeSystemControlCenter(context.pollingWorkers, options));
  });
}

}  // namespace yasb::widgets
