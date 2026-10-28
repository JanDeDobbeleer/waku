#pragma once

#include <QFrame>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <optional>
#include <vector>

#include "widgets/systray/tray_drop_widget.h"

namespace yasb::widgets {

// The `popup` block of the systray options.
struct SystrayPopupOptions {
  bool blur = true;
  bool roundCorners = true;
  QString roundCornersType = QStringLiteral("normal");
  QString borderColor = QStringLiteral("System");
  QString alignment = QStringLiteral("left");
  QString direction = QStringLiteral("down");
  int offsetTop = 6;
  int offsetLeft = 0;
};

// Pure part of SystrayPopup._set_position: the popup is placed under (or above) the bar, aligned
// to the toggle button, then clamped into `screen` (QRect::right()/bottom() semantics).
QPoint computeSystrayPopupPosition(const QPoint& buttonOrigin, const QSize& buttonSize,
                                   const QPoint& barOrigin, int barHeight, const QSize& popupSize,
                                   const std::optional<QRect>& screen, const QString& alignment,
                                   const QString& direction, int offsetLeft, int offsetTop);

// Port of SystrayPopup (services/systray/systray_popup.py): a frameless tool window holding the
// grid of unpinned icons. It fades in and out, closes when it loses activation (unless a drag runs
// or the click was on the toggle button) and keeps the toggle button's checked state and text in
// step.
class SystrayPopup : public QWidget {
  Q_OBJECT
 public:
  SystrayPopup(QWidget* parent, QPushButton* toggleButton, const SystrayPopupOptions& options,
               int iconsPerRow, const QString& labelCollapsed);

  TrayDropWidget* gridWidget() const { return grid_; }
  // Shown and not fading out.
  bool isOpen() const { return isVisible() && !closing_; }
  bool isClosing() const { return closing_; }

  void addIcon(TrayIconButton* icon) { grid_->addIconToGrid(icon); }
  void toggle(const QString& labelExpanded);
  void open(const QString& labelExpanded);
  void relayoutGrid();
  void sortUnpinned(const std::vector<TrayIconButton*>& sorted);
  void hideAnimated();

 signals:
  void closed();

 protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  struct PositionArgs {
    QString alignment;
    QString direction;
    int offsetLeft;
    int offsetTop;
  };

  void setPosition(const PositionArgs& args);
  void onAnimationFinished();
  void onPopupClosed();
  void onWatchTick();
  QRect popupRect() const;

  QPushButton* toggleButton_;
  SystrayPopupOptions options_;
  QString labelCollapsed_;
  bool closing_ = false;
  std::optional<PositionArgs> positionArgs_;
  QFrame* content_;
  TrayDropWidget* grid_;
  QPropertyAnimation* fade_;
  int watchTicks_ = 0;
  QTimer* watchTimer_;
};

}  // namespace yasb::widgets
