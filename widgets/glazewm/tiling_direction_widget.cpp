#include "widgets/glazewm/tiling_direction_widget.h"

#include <QDebug>
#include <QLayout>
#include <QPushButton>

#include "widgets/glazewm/glazewm_service.h"

namespace yasb::widgets {

GlazewmTilingDirectionWidget::GlazewmTilingDirectionWidget(const WidgetContext& context,
                                                           bool subscribe)
    : BaseWidget(QString::fromStdString(context.name), context.eventBus,
                 QStringLiteral("glazewm-tiling-direction"), 0),
      bus_(context.eventBus),
      options_(std::get<config::options::GlazewmTilingDirectionConfig>(context.typedOptions)) {
  // Python adds the button straight to the frame layout, without a widget-container.
  labelContainer()->hide();

  button_ = new QPushButton(widgetFrame());
  button_->setProperty("class", QStringLiteral("btn"));
  button_->setVisible(false);
  widgetFrame()->layout()->addWidget(button_);
  connect(button_, &QPushButton::clicked, this, [this] { toggleTilingDirection(); });

  connect(&bus_, &EventBus::event, this, &GlazewmTilingDirectionWidget::onBusEvent);
  if (subscribe) {
    glazewm::subscribe(bus_, glazewm::kTopicTilingDirection,
                       QString::fromStdString(options_.glazewm_server_uri));
  }
}

void GlazewmTilingDirectionWidget::onBusEvent(const QString& name, const QVariantMap& payload) {
  if (name != glazewm::kStateEvent) return;
  try {
    const QString kind = payload.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("connection")) {
      onConnection(payload.value(QStringLiteral("connected")).toBool());
    } else if (kind == QStringLiteral("tiling_direction")) {
      onTilingDirection(static_cast<glazewm::TilingDirection>(
          payload.value(QStringLiteral("direction")).toInt()));
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "glazewm tiling direction update failed:" << error.what();
  }
}

void GlazewmTilingDirectionWidget::onConnection(bool connected) { button_->setVisible(connected); }

void GlazewmTilingDirectionWidget::onTilingDirection(glazewm::TilingDirection direction) {
  direction_ = direction;
  const bool horizontal = direction == glazewm::TilingDirection::Horizontal;
  const std::string& label = horizontal ? options_.horizontal_label : options_.vertical_label;
  // An empty label falls back to the direction's name (Python: `label or direction`).
  button_->setText(label.empty()
                       ? (horizontal ? QStringLiteral("horizontal") : QStringLiteral("vertical"))
                       : QString::fromStdString(label));
}

void GlazewmTilingDirectionWidget::toggleTilingDirection() { glazewm::toggleTilingDirection(bus_); }

}  // namespace yasb::widgets
