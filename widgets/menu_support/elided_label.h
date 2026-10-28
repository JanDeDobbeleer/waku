#pragma once

#include <QFontMetrics>
#include <QLabel>
#include <QResizeEvent>
#include <QSizePolicy>
#include <algorithm>

namespace yasb::widgets {

// Port of utilities.ElidedLabel: shows "..." when the text does not fit and keeps the full text
// in fullText().
class ElidedLabel : public QLabel {
  Q_OBJECT
 public:
  explicit ElidedLabel(QWidget* parent = nullptr) : QLabel(parent) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setMinimumWidth(0);
  }

  void setFullText(const QString& text) {
    text_ = text;
    refresh();
  }
  const QString& fullText() const { return text_; }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QLabel::resizeEvent(event);
    refresh();
  }

 private:
  void refresh() {
    // A small margin avoids edge clipping and layout loops.
    const int available = std::max(width() - 4, 0);
    QLabel::setText(QFontMetrics(font()).elidedText(text_, Qt::ElideRight, available));
  }

  QString text_;
};

}  // namespace yasb::widgets
