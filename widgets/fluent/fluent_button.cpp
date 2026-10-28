#include "widgets/fluent/fluent_button.h"

#include <QBrush>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QPropertyAnimation>
#include <algorithm>
#include <iterator>

#include "widgets/fluent/fluent_theme.h"

namespace yasb::widgets::fluent {
namespace {

constexpr int kDurationMs = 83;
constexpr double kRadius = 4.0;
// DEFAULT_PADDING = (11, 5, 11, 6): left, top, right, bottom.
constexpr int kPadLeft = 11;
constexpr int kPadTop = 5;
constexpr int kPadRight = 11;
constexpr int kPadBottom = 6;

struct StateKeys {
  const char* state;
  const char* bg;
  const char* fg;
  const char* border;
  const char* borderTop;
};

constexpr StateKeys kAccentStates[] = {
    {"normal", "accent_fill_default", "text_on_accent_primary",
     "control_stroke_on_accent_secondary", "control_stroke_on_accent_default"},
    {"hover", "accent_fill_secondary", "text_on_accent_primary",
     "control_stroke_on_accent_secondary", "control_stroke_on_accent_default"},
    {"pressed", "accent_fill_tertiary", "text_on_accent_secondary",
     "control_stroke_on_accent_default", "control_stroke_on_accent_default"},
    {"disabled", "control_fill_disabled", "text_disabled", nullptr, nullptr},
};

constexpr StateKeys kDefaultStates[] = {
    {"normal", "control_fill_default", "text_primary", "control_stroke_default",
     "control_stroke_secondary"},
    {"hover", "control_fill_secondary", "text_primary", "control_stroke_default",
     "control_stroke_secondary"},
    {"pressed", "control_fill_tertiary", "text_secondary", "control_stroke_default",
     "control_stroke_default"},
    {"disabled", "control_fill_disabled", "text_disabled", nullptr, nullptr},
};

QColor resolve(const char* key) {
  return key != nullptr ? tokenColor(QString::fromLatin1(key)) : QColor(0, 0, 0, 0);
}

QFont::Weight weightFor(const QString& name) {
  const QString key = name.toLower();
  if (key == QLatin1String("thin")) return QFont::Thin;
  if (key == QLatin1String("light")) return QFont::Light;
  if (key == QLatin1String("medium")) return QFont::Medium;
  if (key == QLatin1String("demibold")) return QFont::DemiBold;
  if (key == QLatin1String("bold")) return QFont::Bold;
  return QFont::Normal;
}

}  // namespace

FluentButton::FluentButton(const QString& text, const QString& variant, int fontSize,
                           const QString& fontWeight, QWidget* parent)
    : QPushButton(text, parent), variant_(variant), group_(new QParallelAnimationGroup(this)) {
  QFont font;
  font.setFamilies(fontFamilies());
  font.setPixelSize(fontSize);
  font.setWeight(weightFor(fontWeight));
  setFont(font);

  applyState(QStringLiteral("normal"));
  for (const char* name : {"bg", "fg", "border", "borderTop"}) {
    auto* animation = new QPropertyAnimation(this, name, this);
    animation->setDuration(kDurationMs);
    animation->setEasingCurve(QEasingCurve::InOutCubic);
    group_->addAnimation(animation);
  }
}

FluentButton::StateColors FluentButton::colorsFor(const QString& state) const {
  const StateKeys* table = kDefaultStates;
  size_t count = std::size(kDefaultStates);
  if (variant_ == QLatin1String("accent")) {
    table = kAccentStates;
    count = std::size(kAccentStates);
  }
  const StateKeys* chosen = table;  // "normal" is first
  for (size_t i = 0; i < count; ++i) {
    if (state == QLatin1String(table[i].state)) chosen = &table[i];
  }
  return {resolve(chosen->bg), resolve(chosen->fg), resolve(chosen->border),
          resolve(chosen->borderTop)};
}

QString FluentButton::interactionState(bool hovering) const {
  if (!isEnabled()) return QStringLiteral("disabled");
  return hovering ? QStringLiteral("hover") : QStringLiteral("normal");
}

void FluentButton::applyState(const QString& state) {
  const StateColors colors = colorsFor(state);
  bg_ = colors.bg;
  fg_ = colors.fg;
  border_ = colors.border;
  borderTop_ = colors.borderTop;
}

void FluentButton::animateTo(const QString& state) {
  const StateColors target = colorsFor(state);
  group_->stop();
  const QColor starts[] = {bg_, fg_, border_, borderTop_};
  const QColor ends[] = {target.bg, target.fg, target.border, target.borderTop};
  for (int i = 0; i < group_->animationCount(); ++i) {
    auto* animation = static_cast<QPropertyAnimation*>(group_->animationAt(i));
    animation->setStartValue(starts[i]);
    animation->setEndValue(ends[i]);
  }
  group_->start();
}

void FluentButton::setBg(const QColor& color) {
  bg_ = color;
  update();
}

void FluentButton::setFg(const QColor& color) {
  fg_ = color;
  update();
}

void FluentButton::setBorder(const QColor& color) {
  border_ = color;
  update();
}

void FluentButton::setBorderTop(const QColor& color) {
  borderTop_ = color;
  update();
}

QSize FluentButton::sizeHint() const {
  const QFontMetrics metrics(font());
  const int textWidth = text().isEmpty() ? 0 : metrics.horizontalAdvance(text());
  const int textHeight = text().isEmpty() ? 0 : metrics.height();
  return {std::max(1, kPadLeft + kPadRight + textWidth + 2),
          std::max(1, kPadTop + kPadBottom + textHeight + 2)};
}

void FluentButton::enterEvent(QEnterEvent* event) {
  if (isEnabled()) animateTo(interactionState(true));
  QPushButton::enterEvent(event);
}

void FluentButton::leaveEvent(QEvent* event) {
  if (isEnabled()) animateTo(interactionState(false));
  QPushButton::leaveEvent(event);
}

void FluentButton::mousePressEvent(QMouseEvent* event) {
  if (isEnabled()) animateTo(QStringLiteral("pressed"));
  QPushButton::mousePressEvent(event);
}

void FluentButton::mouseReleaseEvent(QMouseEvent* event) {
  // clicked may destroy this widget (Cancel closes the dialog).
  const QPointer<FluentButton> self(this);
  const QPoint position = event->position().toPoint();
  QPushButton::mouseReleaseEvent(event);
  if (self.isNull() || !isEnabled()) return;
  animateTo(interactionState(isVisible() && rect().contains(position)));
}

void FluentButton::changeEvent(QEvent* event) {
  QPushButton::changeEvent(event);
  if (event->type() == QEvent::EnabledChange)
    animateTo(isEnabled() ? interactionState(underMouse()) : QStringLiteral("disabled"));
}

void FluentButton::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = QRectF(rect());

  p.setPen(Qt::NoPen);
  p.setBrush(bg_);
  p.drawRoundedRect(r, kRadius, kRadius);

  // 1px border inside, on top of the fill.
  if (border_.alpha() || borderTop_.alpha()) {
    QLinearGradient gradient(0, 0, 0, r.height());
    gradient.setColorAt(0, borderTop_);
    gradient.setColorAt(1, border_);
    p.setPen(QPen(QBrush(gradient), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), kRadius - 0.5, kRadius - 0.5);
  }

  if (!text().isEmpty()) {
    p.setPen(fg_);
    p.setFont(font());
    p.drawText(rect().adjusted(kPadLeft, kPadTop, -kPadRight, -kPadBottom), Qt::AlignCenter,
               text());
  }
}

QLabel* makeTextBlock(const QString& text, const QString& variant, QWidget* parent) {
  struct Variant {
    const char* name;
    int size;
    QFont::Weight weight;
    const char* color;
  };
  static const Variant kVariants[] = {
      {"title", 28, QFont::DemiBold, "text_primary"},
      {"title-large", 40, QFont::DemiBold, "text_primary"},
      {"subtitle", 20, QFont::DemiBold, "text_primary"},
      {"body", 14, QFont::Normal, "text_primary"},
      {"body-strong", 14, QFont::DemiBold, "text_primary"},
      {"body-secondary", 14, QFont::Normal, "text_secondary"},
      {"caption", 12, QFont::DemiBold, "text_secondary"},
      {"caption-strong", 12, QFont::DemiBold, "text_primary"},
  };
  const Variant* chosen = &kVariants[3];  // body
  for (const Variant& candidate : kVariants) {
    if (variant == QLatin1String(candidate.name)) chosen = &candidate;
  }
  auto* label = new QLabel(text, parent);
  QFont font;
  font.setFamilies(fontFamilies());
  font.setPixelSize(chosen->size);
  font.setWeight(chosen->weight);
  label->setFont(font);
  label->setStyleSheet(QStringLiteral("color:%1").arg(token(QString::fromLatin1(chosen->color))));
  return label;
}

}  // namespace yasb::widgets::fluent
