#include "widgets/glazewm/binding_mode_widget.h"

#include <QDebug>
#include <QEvent>
#include <QRegularExpression>

#include "widgets/glazewm/glazewm_service.h"

namespace yasb::widgets {
namespace {

QString orEmpty(const std::optional<QString>& value) { return value ? *value : QString(); }

}  // namespace

GlazewmBindingModeWidget::GlazewmBindingModeWidget(const WidgetContext& context, bool subscribe)
    : BaseWidget(QString::fromStdString(context.name), context.eventBus,
                 QStringLiteral("glazewm-binding-mode"), 0),
      bus_(context.eventBus),
      options_(std::get<config::options::GlazewmBindingModeConfig>(context.typedOptions)),
      label_(QString::fromStdString(options_.label)),
      labelAlt_(QString::fromStdString(options_.label_alt)) {
  for (const std::string& mode : options_.binding_modes_to_cycle_through) {
    cycle_.push_back(QString::fromStdString(mode));
  }
  buildWidgetLabel(label_, labelAlt_);

  registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) { toggleLabel(); });
  registerCallback(QStringLiteral("disable_binding_mode"),
                   [this](const QStringList&) { disableBindingMode(); });
  registerCallback(QStringLiteral("next_binding_mode"),
                   [this](const QStringList&) { cycleThroughBindingModes(1); });
  registerCallback(QStringLiteral("prev_binding_mode"),
                   [this](const QStringList&) { cycleThroughBindingModes(-1); });
  setCallbackBinding({QString::fromStdString(options_.callbacks.on_left),
                      QString::fromStdString(options_.callbacks.on_middle),
                      QString::fromStdString(options_.callbacks.on_right),
                      QStringLiteral("do_nothing")});

  connect(&bus_, &EventBus::event, this, &GlazewmBindingModeWidget::onBusEvent);
  hide();
  if (subscribe) {
    glazewm::subscribe(bus_, glazewm::kTopicBindingMode,
                       QString::fromStdString(options_.glazewm_server_uri));
  }
}

void GlazewmBindingModeWidget::onBusEvent(const QString& name, const QVariantMap& payload) {
  if (name != glazewm::kStateEvent) return;
  try {
    const QString kind = payload.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("connection")) {
      onConnectionStatus(payload.value(QStringLiteral("connected")).toBool());
    } else if (kind == glazewm::kTopicBindingMode) {
      if (const glazewm::BindingModePtr mode = glazewm::bindingModeFromPayload(payload)) {
        onBindingMode(*mode);
      }
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "glazewm binding mode update failed:" << error.what();
  }
}

// A parentless widget would become a top-level window on show(); wait for the bar to adopt it.
void GlazewmBindingModeWidget::setShown(bool shown) {
  wantShown_ = shown;
  if (!shown) {
    hide();
  } else if (parentWidget()) {
    show();
  }
}

bool GlazewmBindingModeWidget::event(QEvent* event) {
  const bool handled = BaseWidget::event(event);
  if (event->type() == QEvent::ParentChange && parentWidget() && wantShown_) show();
  return handled;
}

void GlazewmBindingModeWidget::onConnectionStatus(bool connected) {
  if (!connected) setShown(false);
}

void GlazewmBindingModeWidget::onBindingMode(const glazewm::BindingMode& mode) {
  const bool hasName = mode.name && !mode.name->isEmpty();
  const auto indexOf = [this](const QString& name) -> std::optional<int> {
    for (size_t i = 0; i < cycle_.size(); ++i) {
      if (cycle_[i] == name) return static_cast<int>(i);
    }
    return std::nullopt;
  };
  if (!hasName) {
    if (const auto none = indexOf(QStringLiteral("none"))) cycleIndex_ = *none;
  }
  if (options_.hide_if_no_active && !hasName) {
    setShown(false);
    return;
  }
  if (hasName) {
    if (const auto found = indexOf(*mode.name)) cycleIndex_ = *found;
  }
  setShown(true);
  active_ = mode;
  updateLabel();
}

void GlazewmBindingModeWidget::toggleLabel() {
  toggleAlt();
  updateLabel();
}

void GlazewmBindingModeWidget::updateLabel() {
  // Python raises AttributeError here before the first event; the callback runner logs it.
  if (!active_) return;
  static const QRegularExpression splitRe(QStringLiteral("(<span.*?>.*?</span>)"));
  static const QRegularExpression stripRe(QStringLiteral("<span.*?>|</span>"));

  const bool alt = showAlt();
  const auto& widgets = alt ? labelsAlt() : labels();
  const QString& content = alt ? labelAlt_ : label_;
  const QString name = orEmpty(active_->name);
  const bool hasName = !name.isEmpty();

  QString bindingMode = orEmpty(active_->displayName);
  if (bindingMode.isEmpty()) bindingMode = name;
  if (bindingMode.isEmpty()) bindingMode = QString::fromStdString(options_.label_if_no_active);
  const auto iconIt = options_.icons.find((hasName ? name : QStringLiteral("none")).toStdString());
  const QString icon = QString::fromStdString(
      iconIt != options_.icons.end() ? iconIt->second : options_.default_icon);
  const std::vector<std::pair<QString, QString>> replacements = {
      {QStringLiteral("{binding_mode}"), bindingMode}, {QStringLiteral("{icon}"), icon}};

  // Same pieces as build_widget_label: split on <span> groups, drop empty and blank ones.
  QStringList pieces;
  qsizetype last = 0;
  auto it = splitRe.globalMatch(content);
  while (it.hasNext()) {
    const auto m = it.next();
    pieces << content.mid(last, m.capturedStart() - last) << m.captured(1);
    last = m.capturedEnd();
  }
  pieces << content.mid(last);

  size_t index = 0;
  for (QString part : pieces) {
    part = part.trimmed();
    if (part.isEmpty() || index >= widgets.size()) continue;
    QLabel* label = widgets[index];
    const QString formatted = replacePlaceholders(part, replacements);
    if (isSpanPart(part)) {
      const QString inner = QString(part).remove(stripRe).trimmed();
      if (inner == QStringLiteral("{binding_mode}") || inner == QStringLiteral("{icon}")) {
        setLabelClass(label, QStringLiteral("icon ") + (hasName ? name : QStringLiteral("none")));
        label->setText(formatted);
      } else {
        label->setText(inner);
      }
    } else {
      label->setText(formatted);
      if (label->property("class").toString() == QStringLiteral("label-offline")) {
        setLabelClass(label, QStringLiteral("label"));
      }
      if (!hasName) setLabelClass(label, QStringLiteral("label-offline"));
    }
    ++index;
  }
}

void GlazewmBindingModeWidget::disableBindingMode() {
  if (activeHasName()) glazewm::disableBindingMode(bus_, *active_->name);
}

void GlazewmBindingModeWidget::cycleThroughBindingModes(int direction) {
  if (cycle_.empty()) return;
  const int count = static_cast<int>(cycle_.size());
  cycleIndex_ += direction;
  if (cycleIndex_ < 0) cycleIndex_ = count - 1;
  if (cycleIndex_ >= count) cycleIndex_ = 0;
  const QString& target = cycle_[cycleIndex_];
  if (target == QStringLiteral("none")) {
    disableBindingMode();
    return;
  }
  glazewm::enableBindingMode(bus_, target);
}

}  // namespace yasb::widgets
