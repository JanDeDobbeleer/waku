#pragma once

#include <QFrame>
#include <QPropertyAnimation>
#include <QString>
#include <QWidget>

namespace yasb::widgets {

// Port of utilities.PopupWidget as the Wi-Fi and Bluetooth menus use it (not pinnable): a frameless
// Qt::Popup window with an inner content frame carrying the CSS class, DWM blur on show, a fade in
// and out, and an application-wide click filter that closes it (and swallows the click) when the
// user presses outside it. `persistent` keeps the widget alive after it hides.
class MenuPopup : public QWidget {
  Q_OBJECT
 public:
  MenuPopup(QWidget* anchor, bool blur, bool roundCorners, const QString& roundCornersType,
            const QString& borderColor, bool darkMode, bool persistent = false,
            int animationMs = 80);
  ~MenuPopup() override;

  // Sets the `class` property on the popup and on its content frame (Python overrides setProperty).
  void setCssClass(const QString& cssClass);
  void setPosition(const QString& alignment, const QString& direction, int offsetLeft,
                   int offsetTop);
  // Fades the popup out and hides it (deletes it unless persistent).
  void hideAnimated();
  // Hides at once, without the fade (PopupWidget.hide).
  void hideNow();
  // Shows the popup, or fades out the anchor's other open popup instead (toggle behaviour of
  // PopupWidget.show). Returns false when it toggled another popup closed.
  bool showToggled();
  bool isClosing() const { return closing_; }
  // The visible, non-closing popup opened from `anchor`, or null.
  static MenuPopup* openFor(QWidget* anchor);

 protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void onFadeFinished();

  QWidget* anchor_;
  QFrame* content_;
  bool blur_;
  bool roundCorners_;
  QString roundCornersType_;
  QString borderColor_;
  bool darkMode_;
  bool persistent_;
  bool closing_ = false;
  bool positioned_ = false;
  QString alignment_;
  QString direction_;
  int offsetLeft_ = 0;
  int offsetTop_ = 0;
  QPropertyAnimation* fade_;
};

}  // namespace yasb::widgets
