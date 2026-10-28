#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "core/widgets/tooltip.h"
#include "widgets/control_center/control_center_sections.h"

namespace yasb::widgets {
namespace {

QString qs(const std::string& text) { return QString::fromStdString(text); }
QString percentText(int value) { return QString::number(value) + QLatin1Char('%'); }

}  // namespace

SlidersSection::SlidersSection(QWidget* parent,
                               const config::options::SlidersSectionConfig& options,
                               SectionContext context, qulonglong widgetMonitor)
    : QFrame(parent),
      options_(options),
      context_(std::move(context)),
      widgetMonitor_(widgetMonitor) {
  setProperty("class", "section sliders");
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  if (options_.volume.show_slider) {
    const Row row = createRow(QStringLiteral("volume"), options_.volume,
                              [this](int value) { onVolumeChanged(value); });
    volumeSlider_ = row.slider;
    volumeValue_ = row.value;
    volumeSourceButton_ = row.source;
    layout->addWidget(row.frame);
  }
  if (options_.microphone.show_slider) {
    const Row row = createRow(QStringLiteral("microphone"), options_.microphone,
                              [this](int value) { onMicrophoneChanged(value); });
    microphoneSlider_ = row.slider;
    microphoneValue_ = row.value;
    microphoneSourceButton_ = row.source;
    layout->addWidget(row.frame);
  }
  if (options_.brightness.show_slider) {
    const Row row = createRow(QStringLiteral("brightness"), options_.brightness,
                              [this](int value) { onBrightnessChanged(value); });
    brightnessSlider_ = row.slider;
    brightnessValueLabel_ = row.value;
    brightnessSourceButton_ = row.source;
    layout->addWidget(row.frame);
  }
}

std::optional<qulonglong> SlidersSection::brightnessTarget() const {
  if (selectedMonitor_) return selectedMonitor_;
  if (widgetMonitor_) return widgetMonitor_;
  if (context_.system.brightness) {
    const auto monitors = context_.system.brightness->monitors();
    if (!monitors.empty()) return monitors.front().id;
  }
  return std::nullopt;
}

std::optional<int> SlidersSection::currentBrightness() const {
  if (!context_.system.brightness) return std::nullopt;
  const auto target = brightnessTarget();
  if (!target) return std::nullopt;
  return context_.system.brightness->brightness(*target);
}

std::pair<int, QString> SlidersSection::initialState(const QString& key) {
  const auto& system = context_.system;
  if (key == QLatin1String("brightness")) {
    const int value = currentBrightness().value_or(0);
    return {value, percentText(value)};
  }
  platform::AudioEndpoints* endpoint =
      key == QLatin1String("volume") ? system.output : system.input;
  if (!endpoint) return {0, percentText(0)};
  const int value = endpoint->volumePercent().value_or(0);
  return {value, percentText(value)};
}

SlidersSection::Row SlidersSection::createRow(
    const QString& key, const config::options::ControlCenterSliderConfig& options,
    const std::function<void(int)>& onChanged) {
  Row result;
  auto* row = new QFrame(this);
  row->setProperty("class", QStringLiteral("slider ") + key);
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* icon = new QLabel(qs(options.icon), row);
  icon->setProperty("class", "icon");
  icon->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  layout->addWidget(icon);

  auto* slider = new QSlider(Qt::Horizontal, row);
  slider->setProperty("class", "slider-control");
  slider->setRange(0, 100);
  slider->setMouseTracking(true);
  slider->installEventFilter(this);
  const auto [initialValue, initialText] = initialState(key);
  slider->setValue(initialValue);
  connect(slider, &QSlider::valueChanged, this, onChanged);
  if (key == QLatin1String("volume")) {
    connect(slider, &QSlider::sliderReleased, this, [this] {
      if (context_.system.beep) context_.system.beep();
    });
  }
  layout->addWidget(slider, 1);

  auto* value = new QLabel(initialText, row);
  value->setProperty("class", "value");
  value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  layout->addWidget(value);

  QPushButton* source = nullptr;
  if (options.show_source_selector) {
    source = new QPushButton(qs(options.source_selector_icon), row);
    source->setProperty("class", "source-selector");
    source->setFlat(true);
    QString tip;
    if (key == QLatin1String("volume")) {
      connect(source, &QPushButton::clicked, this, [this] { showVolumeSourceMenu(); });
      tip = QStringLiteral("Select output device");
    } else if (key == QLatin1String("microphone")) {
      connect(source, &QPushButton::clicked, this, [this] { showMicrophoneSourceMenu(); });
      tip = QStringLiteral("Select input device");
    } else {
      connect(source, &QPushButton::clicked, this, [this] { showBrightnessSourceMenu(); });
      tip = QStringLiteral("Select monitor");
    }
    if (context_.tooltip) {
      setTooltip(source, tip, context_.globalState, 400, QStringLiteral("top"));
    }
    layout->addWidget(source);
  }

  result.frame = row;
  result.slider = slider;
  result.value = value;
  result.source = source;
  return result;
}

void SlidersSection::onAudioChanged(platform::AudioEndpoints* endpoint, QLabel* label, int value) {
  if (!endpoint || !endpoint->available()) return;
  endpoint->setVolumePercent(value);
  if (value > 0 && endpoint->muted().value_or(false)) {
    endpoint->setMuted(false);
    if (context_.refreshPopup) context_.refreshPopup();
    return;
  }
  if (label) label->setText(percentText(value));
}

void SlidersSection::onVolumeChanged(int value) {
  onAudioChanged(context_.system.output, volumeValue_, value);
}

void SlidersSection::onMicrophoneChanged(int value) {
  onAudioChanged(context_.system.input, microphoneValue_, value);
}

void SlidersSection::onBrightnessChanged(int value) {
  if (context_.system.brightness) {
    if (const auto target = brightnessTarget())
      context_.system.brightness->setBrightness(*target, value);
  }
  if (brightnessValueLabel_) brightnessValueLabel_->setText(percentText(value));
}

QMenu* SlidersSection::sourceMenu(const QString& key) const {
  const auto it = menus_.find(key);
  return it == menus_.end() ? nullptr : it->second.data();
}

void SlidersSection::popupMenu(QMenu* menu, QPushButton* button) {
  if (!button) return;
  const QPoint pos =
      button->mapToGlobal(QPoint(button->width() - menu->sizeHint().width(), button->height()));
  button->setAttribute(Qt::WA_UnderMouse, false);
  button->update();
  menu->popup(pos);
}

void SlidersSection::showAudioSourceMenu(const QString& key, platform::AudioEndpoints* endpoint,
                                         QPushButton* button) {
  if (QMenu* open = sourceMenu(key); open && open->isVisible()) {
    open->close();
    return;
  }
  if (!endpoint) return;
  const auto devices = endpoint->devices();
  if (devices.empty()) return;

  QMenu* menu = createContextMenu(window(), context_.globalState.isDark());
  const QString current = endpoint->defaultDeviceId();
  for (const platform::AudioDevice& device : devices) {
    QAction* action = menu->addAction(device.name);
    action->setCheckable(true);
    action->setChecked(device.id == current);
    const QString id = device.id;
    connect(action, &QAction::triggered, this, [this, endpoint, id] {
      if (endpoint->setDefaultDevice(id) && context_.refreshPopup) context_.refreshPopup();
    });
  }
  menus_[key] = menu;
  popupMenu(menu, button);
}

void SlidersSection::showVolumeSourceMenu() {
  showAudioSourceMenu(QStringLiteral("volume"), context_.system.output, volumeSourceButton_);
}

void SlidersSection::showMicrophoneSourceMenu() {
  showAudioSourceMenu(QStringLiteral("microphone"), context_.system.input, microphoneSourceButton_);
}

void SlidersSection::showBrightnessSourceMenu() {
  const QString key = QStringLiteral("brightness");
  if (QMenu* open = sourceMenu(key); open && open->isVisible()) {
    open->close();
    return;
  }
  platform::BrightnessControl* service = context_.system.brightness;
  if (!service) return;
  const auto monitors = service->monitors();
  if (monitors.empty()) return;
  const auto current = brightnessTarget();
  if (monitors.size() == 1 && current && monitors.front().id == *current) return;

  QMenu* menu = createContextMenu(window(), context_.globalState.isDark());
  int index = 0;
  for (const platform::BrightnessMonitor& monitor : monitors) {
    QAction* action = menu->addAction(service->monitorSubtitle(monitor.id, index));
    action->setCheckable(true);
    action->setChecked(current && monitor.id == *current);
    const qulonglong id = monitor.id;
    connect(action, &QAction::triggered, this, [this, id] {
      if (selectedMonitor_ == id) return;
      selectedMonitor_ = id;
      if (context_.refreshPopup) context_.refreshPopup();
    });
    ++index;
  }
  menus_[key] = menu;
  popupMenu(menu, brightnessSourceButton_);
}

bool SlidersSection::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::Wheel &&
      (watched == volumeSlider_ || watched == microphoneSlider_ || watched == brightnessSlider_)) {
    auto* slider = static_cast<QSlider*>(watched);
    const int step = static_cast<QWheelEvent*>(event)->angleDelta().y() > 0 ? 1 : -1;
    slider->setValue(slider->value() + step);
    return true;
  }
  return QFrame::eventFilter(watched, event);
}

void SlidersSection::refreshAudioRow(platform::AudioEndpoints* endpoint, const QString& key,
                                     QSlider* slider, QLabel* label) {
  if (!slider) return;
  const bool disabled = !endpoint || !endpoint->available();
  setCssClass(slider->parentWidget(),
              disabled ? QStringLiteral("slider ") + key + QStringLiteral(" disabled")
                       : QStringLiteral("slider ") + key);
  slider->setEnabled(!disabled);
  if (disabled) return;
  if (const auto value = endpoint->volumePercent()) {
    const QSignalBlocker blocker(slider);
    slider->setValue(*value);
    if (label) label->setText(percentText(*value));
  }
}

void SlidersSection::refreshState() {
  refreshAudioRow(context_.system.output, QStringLiteral("volume"), volumeSlider_, volumeValue_);
  refreshAudioRow(context_.system.input, QStringLiteral("microphone"), microphoneSlider_,
                  microphoneValue_);
  if (!brightnessSlider_) return;
  const auto value = currentBrightness();
  setCssClass(
      brightnessSlider_->parentWidget(),
      value ? QStringLiteral("slider brightness") : QStringLiteral("slider brightness disabled"));
  brightnessSlider_->setEnabled(value.has_value());
  const int shown = value.value_or(0);
  {
    const QSignalBlocker blocker(brightnessSlider_);
    brightnessSlider_->setValue(shown);
  }
  if (brightnessValueLabel_) brightnessValueLabel_->setText(percentText(shown));
}

void SlidersSection::updateBrightness(qulonglong monitor, int percent) {
  if (!brightnessSlider_) return;
  if (brightnessTarget() != monitor) return;
  setCssClass(brightnessSlider_->parentWidget(), QStringLiteral("slider brightness"));
  brightnessSlider_->setEnabled(true);
  {
    const QSignalBlocker blocker(brightnessSlider_);
    brightnessSlider_->setValue(percent);
  }
  if (brightnessValueLabel_) brightnessValueLabel_->setText(percentText(percent));
}

}  // namespace yasb::widgets
