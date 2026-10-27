#include "base_widget.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QStyle>

#include "event_bus.h"

namespace yasb::widgets {

BaseWidget::BaseWidget(const QString& widgetName, const QString& className, int timerIntervalMs,
                       QWidget* parent)
    : QWidget(parent), m_widgetName(widgetName), m_timerInterval(timerIntervalMs) {
  // BarWindow's island code identifies container children by this property.
  setProperty("widget_name", widgetName);

  m_widgetLayout = new QHBoxLayout(this);
  m_widgetLayout->setContentsMargins(0, 0, 0, 0);
  m_widgetLayout->setSpacing(0);

  m_frame = new QFrame(this);
  m_frame->setProperty("class", QStringLiteral("widget ") + className);
  m_widgetLayout->addWidget(m_frame);

  auto* frameLayout = new QHBoxLayout(m_frame);
  frameLayout->setContentsMargins(0, 0, 0, 0);
  frameLayout->setSpacing(0);

  m_containerLayout = new QHBoxLayout;
  m_containerLayout->setSpacing(0);
  m_containerLayout->setContentsMargins(0, 0, 0, 0);
  m_container = new QFrame(m_frame);
  m_container->setLayout(m_containerLayout);
  m_container->setProperty("class", QStringLiteral("widget-container"));
  frameLayout->addWidget(m_container);

  connect(&EventBus::instance(), &EventBus::widgetHotkey, this, &BaseWidget::handleHotkey);
  connect(&m_timer, &QTimer::timeout, this, [this] { runCallback(m_binding.timer); });
}

QString BaseWidget::screenName() const { return property("screen_name").toString(); }

QString BaseWidget::barId() const { return property("bar_id").toString(); }

void BaseWidget::startTimer() {
  if (m_timerInterval > 0) m_timer.start(m_timerInterval);
  runCallback(m_binding.timer);
}

void BaseWidget::buildWidgetLabel(const QString& content, const QString& contentAlt,
                                  const QString& placeholder, bool hideIcons) {
  for (QLabel* l : m_labels) delete l;
  for (QLabel* l : m_labelsAlt) delete l;
  m_labels.clear();
  m_labelsAlt.clear();
  m_isIcon.clear();
  m_hideIcons = hideIcons;
  m_showAlt = false;

  auto build = [&](const QString& text, bool isAlt, std::vector<QLabel*>& out) {
    for (const LabelPart& part : splitLabelParts(text, isAlt)) {
      auto* label = new QLabel(part.text, m_container);
      label->setProperty("class", part.cssClass);
      if (!part.isIcon && !placeholder.isNull()) label->setText(placeholder);
      label->setAlignment(Qt::AlignCenter);
      m_containerLayout->addWidget(label);
      out.push_back(label);
      if (!isAlt) m_isIcon.push_back(part.isIcon);
    }
  };
  build(content, false, m_labels);
  if (!contentAlt.isEmpty()) build(contentAlt, true, m_labelsAlt);
  applyAltVisibility();
}

void BaseWidget::applyAltVisibility() {
  for (size_t i = 0; i < m_labels.size(); ++i) {
    m_labels[i]->setVisible(!m_showAlt && !(m_isIcon[i] && m_hideIcons));
  }
  for (QLabel* l : m_labelsAlt) l->setVisible(m_showAlt);
}

void BaseWidget::toggleAlt() {
  m_showAlt = !m_showAlt;
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
  repolish(m_frame);
  for (QWidget* w : m_frame->findChildren<QWidget*>()) repolish(w);
}

void BaseWidget::handleHotkey(const QString& widgetName, const QString& action,
                              const QString& screen) {
  if (!m_hotkeyEnabled || widgetName != m_widgetName) return;
  if (screen.isEmpty() || screenName() != screen) return;
  if (!action.isEmpty()) runCallback(action);
}

void BaseWidget::mouseReleaseEvent(QMouseEvent* event) {
  switch (event->button()) {
    case Qt::LeftButton:
      runCallback(m_binding.left);
      break;
    case Qt::MiddleButton:
      runCallback(m_binding.middle);
      break;
    case Qt::RightButton:
      runCallback(m_binding.right);
      break;
    default:
      break;
  }
}

void BaseWidget::contextMenuEvent(QContextMenuEvent* event) { event->accept(); }

}  // namespace yasb::widgets
