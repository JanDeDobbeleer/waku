#pragma once

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <array>
#include <optional>

namespace yasb::css {

struct TransitionSpec {
  QString prop;
  int durationMs = 0;
  QString easing = QStringLiteral("ease");
  int delayMs = 0;
};

using Declaration = QPair<QString, QString>;

struct StyleRule {
  QString selector;
  QString baseSelector;
  QList<Declaration> properties;  // insertion-ordered, names unique
  QStringList pseudoSet;          // animation pseudo-classes only, sorted
  QList<TransitionSpec> transitions;
  QStringList segments;
  bool subcontrol = false;
  bool hasAttrs = false;
  std::array<int, 3> specificity{0, 0, 0};
  int order = 0;
  bool resetsTransitions = false;
  int scopeRank = 0;
  bool hasEffectProps = false;
  bool hasCursorProp = false;
  bool hasBorderRadiusProps = false;
  int line = 0;                              // 1-based line of the rule in the parsed text
  bool droppedSubcontrolTransition = false;  // `transition` declared on a ::sub-control

  bool has(const QString& name) const;
  QString value(const QString& name) const;
};

struct ParsedStyleSheet {
  QString qss;  // static QSS to hand to Qt: engine-managed declarations removed
  QList<StyleRule> rules;
};

// Port of qt_css_engine.extract_rules (MIT, (c) 2026 George Sladkovsky).
ParsedStyleSheet extractRules(const QString& stylesheet);

QString translateGradients(const QString& value);

struct AttrCondition {
  QString name;
  QString op;  // "=" or "exists"; other operators parse but never match
  std::optional<QString> value;
};

struct CompiledSegment {
  QString objName;
  QString tag;
  QSet<QString> classes;
  QList<AttrCondition> attrs;
};

QList<AttrCondition> parseAttrConditions(const QString& segment);
CompiledSegment compileSegment(const QString& segment);

}  // namespace yasb::css
