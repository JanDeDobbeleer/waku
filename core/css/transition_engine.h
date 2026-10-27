#pragma once

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <memory>

#include "core/css/rules.h"

class QCoreApplication;
class QWidget;

namespace yasb::css {

Q_DECLARE_LOGGING_CATEGORY(lcTransitions)

// App-wide event filter that animates the CSS properties Qt's QSS engine cannot: `transition`
// on width, padding, margin, colours and opacity, `opacity` itself (QGraphicsOpacityEffect),
// `cursor`, and `:clicked` activation. Everything else is handed to Qt untouched.
// Constructs the engine cannot honour are logged once, with selector and line.
class TransitionEngine : public QObject {
  Q_OBJECT

 public:
  explicit TransitionEngine(QObject* parent = nullptr);
  ~TransitionEngine() override;

  // Parses a preprocessed stylesheet, resets all widget state and returns the static QSS
  // that Qt should receive (engine-managed declarations removed).
  QString loadStyleSheet(const QString& processedCss);
  const QList<StyleRule>& rules() const;

  void install(QCoreApplication* app);

  // The inline value the engine currently applies to `prop` (empty when it has none).
  QString currentValue(const QWidget* widget, const QString& prop) const;

  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

}  // namespace yasb::css
