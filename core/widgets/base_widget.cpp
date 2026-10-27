#include "core/widgets/base_widget.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QStyle>

#include "core/widgets/event_bus.h"

namespace yasb::widgets {

BaseWidget::BaseWidget(const QString& widgetName, const QString& className, int timerIntervalMs,
                       QWidget* parent)
    : QWidget(parent), widgetName_(widgetName), timerInterval_(timerIntervalMs) {
  // BarWindow's island code identifies container children by this property.
  setProperty("widget_name", widgetName);

  widgetLayout_ = new QHBoxLayout(this);
  widgetLayout_->setContentsMargins(0, 0, 0, 0);
  widgetLayout_->setSpacing(0);

  frame_ = new QFrame(this);
  frame_->setProperty("class", QStringLiteral("widget ") + className);
  widgetLayout_->addWidget(frame_);

  auto* frameLayout = new QHBoxLayout(frame_);
  frameLayout->setContentsMargins(0, 0, 0, 0);
  frameLayout->setSpacing(0);

  containerLayout_ = new QHBoxLayout;
  containerLayout_->setSpacing(0);
  containerLayout_->setContentsMargins(0, 0, 0, 0);
  container_ = new QFrame(frame_);
  container_->setLayout(containerLayout_);
  container_->setProperty("class", QStringLiteral("widget-container"));
  frameLayout->addWidget(container_);

  connect(&EventBus::instance(), &EventBus::widgetHotkey, this, &BaseWidget::handleHotkey);
  connect(&timer_, &QTimer::timeout, this, [this] { runCallback(binding_.timer); });
}

QString BaseWidget::screenName() const { return property("screen_name").toString(); }

QString BaseWidget::barId() const { return property("bar_id").toString(); }

void BaseWidget::startTimer() {
  if (timerInterval_ > 0) timer_.start(timerInterval_);
  runCallback(binding_.timer);
}

void BaseWidget::buildWidgetLabel(const QString& content, const QString& contentAlt,
                                  const QString& placeholder, bool hideIcons) {
  for (QLabel* l : labels_) delete l;
  for (QLabel* l : labelsAlt_) delete l;
  labels_.clear();
  labelsAlt_.clear();
  isIcon_.clear();
  hideIcons_ = hideIcons;
  showAlt_ = false;

  auto build = [&](const QString& text, bool isAlt, std::vector<QLabel*>& out) {
    for (const LabelPart& part : splitLabelParts(text, isAlt)) {
      auto* label = new QLabel(part.text, container_);
      label->setProperty("class", part.cssClass);
      if (!part.isIcon && !placeholder.isNull()) label->setText(placeholder);
      label->setAlignment(Qt::AlignCenter);
      containerLayout_->addWidget(label);
      out.push_back(label);
      if (!isAlt) isIcon_.push_back(part.isIcon);
    }
  };
  build(content, false, labels_);
  if (!contentAlt.isEmpty()) build(contentAlt, true, labelsAlt_);
  applyAltVisibility();
}

void BaseWidget::applyAltVisibility() {
  for (size_t i = 0; i < labels_.size(); ++i) {
    labels_[i]->setVisible(!showAlt_ && !(isIcon_[i] && hideIcons_));
  }
  for (QLabel* l : labelsAlt_) l->setVisible(showAlt_);
}

void BaseWidget::toggleAlt() {
  showAlt_ = !showAlt_;
  applyAltVisibility();
}

void BaseWidget::setLabelClass(QLabel* label, const QString& cssClass) {
  if (label->property("class").toString() == cssClass) return;
  label->setProperty("class", cssClass);
  label->style()->unpolish(label);
  label->style()->polish(label);
}

void BaseWidget::clearHoverState() {
  auto repolish = [](QWidget* w) {
    w->setAttribute(Qt::WA_UnderMouse, false);
    w->style()->unpolish(w);
    w->style()->polish(w);
  };
  repolish(this);
  repolish(frame_);
  for (QWidget* w : frame_->findChildren<QWidget*>()) repolish(w);
}

void BaseWidget::handleHotkey(const QString& widgetName, const QString& action,
                              const QString& screen) {
  if (!hotkeyEnabled_ || widgetName != widgetName_) return;
  if (screen.isEmpty() || screenName() != screen) return;
  if (!action.isEmpty()) runCallback(action);
}

void BaseWidget::mouseReleaseEvent(QMouseEvent* event) {
  switch (event->button()) {
    case Qt::LeftButton:
      runCallback(binding_.left);
      break;
    case Qt::MiddleButton:
      runCallback(binding_.middle);
      break;
    case Qt::RightButton:
      runCallback(binding_.right);
      break;
    default:
      break;
  }
}

void BaseWidget::contextMenuEvent(QContextMenuEvent* event) { event->accept(); }

}  // namespace yasb::widgets
