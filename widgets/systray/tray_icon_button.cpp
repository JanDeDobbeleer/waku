#include "widgets/systray/tray_icon_button.h"

#include <QDrag>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>

#include "core/widgets/tooltip.h"
#include "platform/systray_click.h"
#include "widgets/systray/tray_drop_widget.h"

namespace yasb::widgets {
namespace {

using platform::kWmLButtonDblClk;
using platform::kWmLButtonDown;
using platform::kWmLButtonUp;
using platform::kWmMButtonDown;
using platform::kWmMButtonUp;
using platform::kWmRButtonDown;
using platform::kWmRButtonUp;

}  // namespace

TrayIconButton::TrayIconButton(const TrayIconSettings& settings, platform::TrayWindowApi* api,
                               bar::GlobalState* globalState)
    : settings_(settings), api_(api), globalState_(globalState) {
  setProperty("class", QStringLiteral("button"));
  setIconSize(QSize(settings_.iconSize, settings_.iconSize));
}

void TrayIconButton::updateScaledPixmap() {
  if (hasData && !image_.isNull()) {
    scaledPixmap_ = QPixmap::fromImage(image_).scaled(16, 16, Qt::KeepAspectRatio);
  } else {
    scaledPixmap_ = QPixmap();
  }
}

void TrayIconButton::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    lastCursorPos_ = event->pos();
    if (event->modifiers() & settings_.pinModifier) {
      emit pinnedChanged(this);
    } else {
      lmbPressed_ = true;
      updateScaledPixmap();
    }
  }
  QPushButton::mousePressEvent(event);
}

void TrayIconButton::mouseMoveEvent(QMouseEvent* event) {
  // A drag starts once the left button moved more than 10 pixels.
  if (lmbPressed_ && (event->pos() - lastCursorPos_).manhattanLength() > 10) {
    dragInProgress_ = true;
    // Dim the icon while it is being dragged.
    auto* effect = new QGraphicsOpacityEffect(this);
    effect->setOpacity(0.3);
    setGraphicsEffect(effect);
    QPointer<TrayIconButton> self(this);
    {
      QDrag drag(this);
      if (!scaledPixmap_.isNull()) drag.setPixmap(scaledPixmap_);
      auto* mime = new QMimeData;
      mime->setText(text());
      drag.setMimeData(mime);
      const Qt::DropAction result = drag.exec(Qt::MoveAction);
      if (self && result == Qt::IgnoreAction) {
        // Dropped outside every valid target: tell the parent container.
        if (auto* parent = qobject_cast<TrayDropWidget*>(parentWidget())) emit parent->dragEnded();
      }
    }
    if (self) {
      setGraphicsEffect(nullptr);
      lmbPressed_ = false;
    }
    dragInProgress_ = false;
    if (!self) return;
  }
  QPushButton::mouseMoveEvent(event);
}

void TrayIconButton::mouseReleaseEvent(QMouseEvent* event) {
  if (ignoreNextRelease_) {
    ignoreNextRelease_ = false;
    QPushButton::mouseReleaseEvent(event);
    return;
  }
  lmbPressed_ = false;
  const Qt::MouseButton button = event->button();
  if (button == Qt::LeftButton && (lastCursorPos_ - event->pos()).manhattanLength() > 8) {
    QPushButton::mouseReleaseEvent(event);
    return;
  }
  if (button == Qt::LeftButton) {
    sendAction(kWmLButtonDown);
    sendAction(kWmLButtonUp);
  } else if (button == Qt::RightButton) {
    sendAction(kWmRButtonDown);
    sendAction(kWmRButtonUp);
  } else if (button == Qt::MiddleButton) {
    sendAction(kWmMButtonDown);
    sendAction(kWmMButtonUp);
  }
  QPushButton::mouseReleaseEvent(event);
}

void TrayIconButton::mouseDoubleClickEvent(QMouseEvent* event) {
  ignoreNextRelease_ = true;
  sendAction(kWmLButtonDblClk);
  QPushButton::mouseDoubleClickEvent(event);
}

void TrayIconButton::sendAction(std::uint32_t action) {
  if (!hasData || !api_) return;
  platform::sendTrayAction(info, *api_, action);
}

void TrayIconButton::updateIcon() {
  if (!hasData || info.hIcon == 0) return;
  if (settings_.enableTooltips && globalState_) {
    setTooltip(this, info.szTip.isEmpty() ? info.exe : info.szTip, *globalState_, 50);
  }
  if (!image_.isNull()) setIcon(QIcon(QPixmap::fromImage(image_)));
}

}  // namespace yasb::widgets
