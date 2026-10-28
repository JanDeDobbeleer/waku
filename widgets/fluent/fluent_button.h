#pragma once

#include <QColor>
#include <QParallelAnimationGroup>
#include <QPushButton>
#include <QString>

class QLabel;

namespace yasb::widgets::fluent {

// Port of Python's core/ui/components/button.py (animated fluent button): "default" and "accent"
// variants, colours from the theme tokens, 83 ms colour transitions between states.
class FluentButton : public QPushButton {
  Q_OBJECT
  Q_PROPERTY(QColor bg READ bg WRITE setBg)
  Q_PROPERTY(QColor fg READ fg WRITE setFg)
  Q_PROPERTY(QColor border READ border WRITE setBorder)
  Q_PROPERTY(QColor borderTop READ borderTop WRITE setBorderTop)

 public:
  explicit FluentButton(const QString& text = QString(),
                        const QString& variant = QStringLiteral("default"), int fontSize = 14,
                        const QString& fontWeight = QStringLiteral("normal"),
                        QWidget* parent = nullptr);

  QColor bg() const { return bg_; }
  void setBg(const QColor& color);
  QColor fg() const { return fg_; }
  void setFg(const QColor& color);
  QColor border() const { return border_; }
  void setBorder(const QColor& color);
  QColor borderTop() const { return borderTop_; }
  void setBorderTop(const QColor& color);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return sizeHint(); }

 protected:
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void changeEvent(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  struct StateColors {
    QColor bg, fg, border, borderTop;
  };
  StateColors colorsFor(const QString& state) const;
  QString interactionState(bool hovering) const;
  void applyState(const QString& state);
  void animateTo(const QString& state);

  QString variant_;
  QColor bg_, fg_, border_, borderTop_;
  QParallelAnimationGroup* group_;
};

// Port of Python's TextBlock: a QLabel with a typography variant ("title", "subtitle", "body",
// "caption", ...) coloured by a theme token.
QLabel* makeTextBlock(const QString& text, const QString& variant, QWidget* parent);

}  // namespace yasb::widgets::fluent
