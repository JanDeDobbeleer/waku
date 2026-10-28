#include "widgets/systray/tray_icon_button.h"

#include <QDrag>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>

#include "core/widgets/tooltip.h"
#include "platform/systray_pipe.h"
#include "widgets/systray/tray_drop_widget.h"

namespace yasb::widgets {
namespace {

// winuser.h mouse messages; spelled out so this file needs no Win32 headers.
constexpr std::uint32_t kWmLButtonDown = 0x0201;
constexpr std::uint32_t kWmLButtonUp = 0x0202;
constexpr std::uint32_t kWmLButtonDblClk = 0x0203;
constexpr std::uint32_t kWmRButtonDown = 0x0204;
constexpr std::uint32_t kWmRButtonUp = 0x0205;
constexpr std::uint32_t kWmMButtonDown = 0x0207;
constexpr std::uint32_t kWmMButtonUp = 0x0208;

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
  if (!hasData || !api_ || !api_->isWindow(info.hWnd)) return;

  const bool isMouseClick = action == kWmLButtonDown || action == kWmRButtonDown ||
                            action == kWmMButtonDown || action == kWmLButtonUp ||
                            action == kWmRButtonUp || action == kWmMButtonUp ||
                            action == kWmLButtonDblClk;
  if (isMouseClick) api_->allowSetForeground(info.hWnd);
  sendNotify(action);

  if (info.uVersion >= 3) {
    if (action == kWmLButtonUp)
      sendNotify(platform::kNinSelect);
    else if (action == kWmRButtonUp)
      sendNotify(platform::kNinContextMenu);
  }
}

void TrayIconButton::sendNotify(std::uint32_t message) {
  if (!hasData || !api_ || info.uCallbackMessage == 0) return;
  std::uint64_t wParam = 0;
  std::int32_t lParam = 0;
  if (info.uVersion > 3) {
    const auto [x, y] = api_->cursorPos();
    wParam = static_cast<std::uint32_t>(platform::packI32(x, y));
    lParam =
        platform::packI32(static_cast<std::int32_t>(message), static_cast<std::int32_t>(info.uID));
  } else {
    wParam = info.uID;
    lParam = platform::packI32(static_cast<std::int32_t>(message), 0);
  }
  api_->sendNotifyMessage(info.hWnd, info.uCallbackMessage, wParam, lParam);
}

void TrayIconButton::updateIcon() {
  if (!hasData || info.hIcon == 0) return;
  if (settings_.enableTooltips && globalState_) {
    setTooltip(this, info.szTip.isEmpty() ? info.exe : info.szTip, *globalState_, 50);
  }
  if (!image_.isNull()) setIcon(QIcon(QPixmap::fromImage(image_)));
}

}  // namespace yasb::widgets
