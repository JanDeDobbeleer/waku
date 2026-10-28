#include "widgets/taskbar/taskbar_preview.h"

#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QEasingCurve>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QImage>
#include <QRegion>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <algorithm>

#include "core/widgets/backdrop.h"
#include "widgets/taskbar/taskbar_widget.h"

namespace yasb::widgets {
namespace {

void repolish(QWidget* widget) {
  if (!widget) return;
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
  widget->update();
}

}  // namespace

int ceilDiv(int count, int per) { return (count + per - 1) / per; }

int previewGridColumns(int count, int screenWidth, int margin, int padding) {
  const int gap = 2 * padding;
  const int available = screenWidth - (2 * margin) - (2 * padding);
  return std::max(1, std::min(count, (available + gap) / (PreviewPopup::kMinWidth + gap)));
}

// --- PreviewFade

void PreviewFade::start() {
  if (running_ || !widget_) return;
  if (duration_ <= 0) {
    widget_->setWindowOpacity(1.0);
    widget_->show();
    return;
  }
  widget_->setWindowOpacity(0.0);
  widget_->show();
  animation_ = new QPropertyAnimation(widget_, "windowOpacity", widget_);
  animation_->setDuration(duration_);
  animation_->setStartValue(0.0);
  animation_->setEndValue(1.0);
  animation_->setEasingCurve(QEasingCurve::OutCubic);
  running_ = true;
  QObject::connect(animation_, &QPropertyAnimation::finished, widget_,
                   [this] { running_ = false; });
  animation_->start();
}

void PreviewFade::stop() {
  if (animation_) {
    animation_->stop();
    animation_->deleteLater();
    animation_ = nullptr;
  }
  running_ = false;
}

// --- ThumbnailHost

ThumbnailHost::ThumbnailHost(PreviewPopup* popup, PreviewItem* item)
    : popup_(popup), item_(item), hwnd_(item->hwnd()) {
  setMouseTracking(true);
}

void ThumbnailHost::enterEvent(QEnterEvent* event) {
  if (popup_) popup_->cancelHide();
  if (item_) item_->setHovered(true, /*peek=*/true);
  QWidget::enterEvent(event);
}

void ThumbnailHost::leaveEvent(QEvent* event) {
  if (popup_) popup_->scheduleHide();
  if (item_) item_->setHovered(false, /*peek=*/true);
  QWidget::leaveEvent(event);
}

void ThumbnailHost::mousePressEvent(QMouseEvent* event) {
  if (popup_)
    popup_->activateTarget(hwnd_);
  else
    QWidget::mousePressEvent(event);
}

// --- PreviewItem

PreviewItem::PreviewItem(PreviewPopup* popup, const PreviewEntry& entry, const QString& title)
    : QFrame(popup->content()), popup_(popup), hwnd_(entry.hwnd), title_(title) {
  setProperty("class", entry.flashing ? "preview-item flashing" : "preview-item");

  header_ = new QFrame(this);
  header_->setProperty("class", "header");
  auto* layout = new QHBoxLayout(header_);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(kSpacing);

  iconLabel_ = new QLabel(header_);
  iconLabel_->setFixedSize(kIconSize, kIconSize);
  iconLabel_->setScaledContents(true);

  titleLabel_ = new QLabel(header_);
  titleLabel_->setProperty("class", "title");

  closeButton_ = new PreviewCloseButton(header_);
  closeButton_->setProperty("class", "close-button");
  closeButton_->setText(QStringLiteral(""));
  closeButton_->setAlignment(Qt::AlignCenter);
  closeButton_->setCursor(Qt::PointingHandCursor);
  closeButton_->action = [popup, hwnd = entry.hwnd] { popup->closeWindow(hwnd); };

  layout->addWidget(iconLabel_, 0, Qt::AlignVCenter);
  layout->addWidget(titleLabel_, 1, Qt::AlignVCenter);
  layout->addWidget(closeButton_, 0, Qt::AlignVCenter);

  headerHeight_ = header_->sizeHint().height();
  closeButton_->setVisible(false);

  if (!entry.icon.isNull()) {
    const qreal dpr = popup->dpr();
    const int target = std::max(1, static_cast<int>(kIconSize * dpr));
    QPixmap scaled =
        entry.icon.scaled(target, target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    iconLabel_->setPixmap(scaled);
  }
}

void PreviewItem::updateElidedTitle(std::optional<int> width) {
  if (width) headerWidth_ = *width;
  int available = headerWidth_ - iconLabel_->width() - kSpacing;
  if (closeButton_->isVisibleTo(this)) {
    // Ask for the size, the button has no geometry yet on the hover that reveals it.
    const int closeWidth = std::max(closeButton_->sizeHint().width(), closeButton_->minimumWidth());
    available -= closeWidth + kSpacing;
  }
  if (available <= 0) {
    titleLabel_->setText(QString());
    return;
  }
  titleLabel_->setText(
      QFontMetrics(titleLabel_->font()).elidedText(title_, Qt::ElideRight, available));
}

int PreviewItem::naturalWidth() const {
  const int closeWidth = std::max(closeButton_->sizeHint().width(), closeButton_->minimumWidth());
  const int titleWidth = QFontMetrics(titleLabel_->font()).horizontalAdvance(title_);
  return iconLabel_->width() + titleWidth + closeWidth + (2 * kSpacing);
}

void PreviewItem::roundOuterCorners(int radius, bool atLeft, bool atRight, bool atTop,
                                    bool atBottom) {
  QStringList corners;
  if (radius > 0) {
    const struct {
      const char* name;
      bool touching;
    } sides[] = {{"top-left", atTop && atLeft},
                 {"top-right", atTop && atRight},
                 {"bottom-left", atBottom && atLeft},
                 {"bottom-right", atBottom && atRight}};
    for (const auto& side : sides) {
      if (side.touching)
        corners
            << QStringLiteral("border-%1-radius:%2px").arg(QLatin1String(side.name)).arg(radius);
    }
  }
  const QString style =
      corners.isEmpty() ? QString() : QStringLiteral(".preview-item{") + corners.join(';') + '}';
  if (styleSheet() != style) setStyleSheet(style);
}

void PreviewItem::setHovered(bool hovered, bool peek) {
  if (hovered) {
    // Only one window is ever hovered, so a neighbour that missed its own leave lets go here.
    for (PreviewItem* other : popup_->items()) {
      if (other != this) other->setHovered(false);
    }
  }
  if (underMouse() != hovered) {
    setAttribute(Qt::WA_UnderMouse, hovered);
    update();
  }
  if (closeButton_->isVisibleTo(this) != hovered) {
    closeButton_->setVisible(hovered);
    updateElidedTitle();
  }
  // A list row is all header and has no thumbnail to hover, so there peek is the only preview.
  if (peek || popup_->listMode()) {
    if (PreviewManager* manager = popup_->manager()) manager->setPeek(hwnd_, hovered);
  }
}

void PreviewItem::enterEvent(QEnterEvent* event) {
  setHovered(true);
  QFrame::enterEvent(event);
}

void PreviewItem::leaveEvent(QEvent* event) {
  setHovered(false);
  QFrame::leaveEvent(event);
}

void PreviewItem::mousePressEvent(QMouseEvent* event) {
  popup_->activateTarget(hwnd_);
  QFrame::mousePressEvent(event);
}

// --- PreviewPopup

PreviewPopup::PreviewPopup(QWidget* parent, int width, int padding, int margin,
                           int animationDuration, bool blur, bool peek)
    : QFrame(parent),
      width_(width),
      margin_(margin),
      padding_(padding),
      blur_(blur),
      peek_(peek),
      fade_(this, animationDuration) {
  setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint |
                 Qt::NoDropShadowWindowHint);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_TranslucentBackground);

  content_ = new QFrame(this);
  content_->setProperty("class", "taskbar-preview");
  if (blur_) {
    try {
      enableBlur(winId(), false, true, QStringLiteral("normal"), QStringLiteral("None"));
    } catch (...) {
    }
  }

  hideTimer_.setSingleShot(true);
  hideTimer_.setInterval(300);
  connect(&hideTimer_, &QTimer::timeout, this, [this] { doHide(); });

  // Backdrop blur fix. Without updating the popup content the blur shows a stale background from
  // the window or wallpaper, the same result as the real taskbar.
  backdrop_.setInterval(kBackdropRefreshMs);
  connect(&backdrop_, &QTimer::timeout, this, [this] { content_->update(QRect(0, 0, 1, 1)); });
}

int PreviewPopup::cornerRadius() {
  if (radius_) return *radius_;
  radius_ = 0;
  const int probe = std::min({64, width() / 2, height() / 2});
  if (probe > 0) {
    QImage image(probe, probe, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    content_->render(&image, QPoint(), QRegion(0, 0, probe, probe), QWidget::RenderFlags());
    // A transparent inside means no background is painted, keep the plain rectangle.
    const int opaque = image.pixelColor(probe - 1, probe - 1).alpha();
    if (opaque > 0) {
      int radius = 0;
      while (radius < probe && image.pixelColor(radius, 0).alpha() < opaque) ++radius;
      radius_ = radius >= probe ? 0 : radius;
    }
  }
  return *radius_;
}

void PreviewPopup::showEvent(QShowEvent* event) {
  // Only peek moves what is behind the preview, so without it the backdrop cannot go stale.
  if (blur_ && peek_) backdrop_.start();
  QFrame::showEvent(event);
}

void PreviewPopup::hideEvent(QHideEvent* event) {
  backdrop_.stop();
  fade_.stop();
  cancelHide();
  QFrame::hideEvent(event);
}

bool PreviewPopup::showFor(const std::vector<PreviewEntry>& entries, QWidget* anchor,
                           platform::WindowSystem& system) {
  system_ = &system;
  std::vector<PreviewEntry> live;
  for (const PreviewEntry& entry : entries) {
    if (system.isWindow(entry.hwnd)) live.push_back(entry);
  }
  if (live.empty() || !anchor) return false;

  // The window the anchor button currently stands for, used to match an already open preview.
  const QVariant anchorHwnd = anchor->property("hwnd");
  srcHwnd_ = anchorHwnd.isValid() ? static_cast<platform::WindowHandle>(anchorHwnd.toULongLong())
                                  : live.front().hwnd;
  anchor_ = anchor;

  // The anchor's screen decides the scaling, so previews on multi-monitor setups look right.
  QScreen* screen = anchor->screen();
  if (!screen) return false;
  dpr_ = screen->devicePixelRatio();

  items_.clear();
  for (const PreviewEntry& entry : live) {
    const QString title = entry.title ? *entry.title : system.query(entry.hwnd).title;
    items_.push_back(new PreviewItem(this, entry, title));
  }

  const QPoint anchorCenter = anchor->mapToGlobal(anchor->rect().center());
  screenGeometry_ = screen->geometry();
  anchorCenterX_ = anchorCenter.x();
  anchorTop_ = anchor->mapToGlobal(anchor->rect().topLeft()).y();
  anchorBottom_ = anchor->mapToGlobal(anchor->rect().bottomLeft()).y();

  // A second row of thumbnails would span the screen and register that many more live DWM
  // thumbnails at once, so the windows are listed by title from there on instead.
  const int count = static_cast<int>(items_.size());
  listMode_ = count > previewGridColumns(count, screenGeometry_.width(), margin_, padding_);

  calculateAndPosition();
  return true;
}

int PreviewPopup::popupSpace() const {
  const int above = anchorTop_ - screenGeometry_.top() - margin_;
  const int below = screenGeometry_.bottom() - anchorBottom_ - margin_;
  return std::max({0, above, below});
}

int PreviewPopup::place(int popupWidth, int popupHeight) {
  popupWidth = std::max(popupWidth, kMinWidth);
  int x = anchorCenterX_ - popupWidth / 2;
  int y = anchorBottom_ + margin_;
  if (anchorTop_ - popupHeight - margin_ >= screenGeometry_.top())
    y = anchorTop_ - popupHeight - margin_;

  x = std::max(screenGeometry_.left() + margin_,
               std::min(screenGeometry_.right() - popupWidth - margin_, x));
  y = std::max(screenGeometry_.top() + margin_,
               std::min(screenGeometry_.bottom() - popupHeight - margin_, y));

  finalPos_ = QPoint(x, y);
  setGeometry(x, y, popupWidth, popupHeight);
  content_->setGeometry(0, 0, popupWidth, popupHeight);
  return popupWidth;
}

QSize PreviewPopup::sourceSize(const PreviewItem* item) const {
  // DWM first, it is the more accurate: physical pixels, converted to logical ones.
  if (item->thumbnail) {
    if (const auto size = item->thumbnail->sourceSize()) {
      if (dpr_ > 0)
        return QSize(std::max(1, static_cast<int>(size->width() / dpr_)),
                     std::max(1, static_cast<int>(size->height() / dpr_)));
      return *size;
    }
  }
  if (system_) {
    const QRect rect = system_->windowRect(item->hwnd());
    if (!rect.isNull()) return QSize(std::max(1, rect.width()), std::max(1, rect.height()));
  }
  return QSize(800, 600);
}

// Every thumbnail is fitted into the same box, the configured width at 16:9, keeping its own
// aspect ratio. The box gives way as the row fills, so more windows means smaller thumbnails
// until they would pass kMinWidth, which is where the list takes over.
void PreviewPopup::calculateAndPosition() {
  const int count = static_cast<int>(items_.size());
  if (!count) return;
  if (listMode_) {
    layoutAsList();
    return;
  }

  const int headerHeight = items_.front()->headerHeight();
  // Cells are spaced by two paddings so every item keeps the same padding around its own cell.
  const int gap = 2 * padding_;

  // The box every thumbnail is fitted into, shortened when the screen is too short for it.
  const int availableWidth = screenGeometry_.width() - (2 * margin_) - (2 * padding_);
  const int boxWidth =
      std::max(kMinWidth, std::min(width_, (availableWidth - (count - 1) * gap) / count));
  const int boxHeight = std::max(1, std::min(static_cast<int>(boxWidth * 9 / 16),
                                             popupSpace() - headerHeight - (2 * padding_)));

  std::vector<QSize> sizes;
  for (const PreviewItem* item : items_) {
    const QSize source = sourceSize(item);
    const double scale = std::min(static_cast<double>(boxWidth) / source.width(),
                                  static_cast<double>(boxHeight) / source.height());
    sizes.emplace_back(std::max(1, static_cast<int>(source.width() * scale)),
                       std::max(1, static_cast<int>(source.height() * scale)));
  }

  // A cell hugs its own thumbnail, down to the width its header still needs, and the row is as
  // tall as the tallest thumbnail in it.
  std::vector<int> cells;
  int rowWidth = (count - 1) * gap;
  int rowHeight = 0;
  for (const QSize& size : sizes) {
    cells.push_back(std::max(kMinWidth, size.width()));
    rowWidth += cells.back();
    rowHeight = std::max(rowHeight, size.height());
  }
  const int totalHeight = headerHeight + rowHeight + (2 * padding_);

  const int popupWidth = place(rowWidth + (2 * padding_), totalHeight);

  int cellX = (popupWidth - rowWidth) / 2;
  for (int index = 0; index < count; ++index) {
    PreviewItem* item = items_[static_cast<size_t>(index)];
    const QSize thumb = sizes[static_cast<size_t>(index)];
    const int cell = cells[static_cast<size_t>(index)];
    // Items tile the popup, each one padded evenly around its cell, and the outer ones reach the
    // popup edge so hovering a window never leaves a background strip showing.
    const int left = index == 0 ? 0 : cellX - padding_;
    const int right = index == count - 1 ? popupWidth : cellX + cell + padding_;
    item->setGeometry(left, 0, right - left, totalHeight);
    item->header()->setGeometry(cellX - left, padding_, cell, headerHeight);
    item->updateElidedTitle(cell);
    item->roundOuterCorners(cornerRadius(), left == 0, right == popupWidth, true, true);
    item->setThumbRect(QRect(cellX + (cell - thumb.width()) / 2,
                             padding_ + headerHeight + (rowHeight - thumb.height()) / 2,
                             thumb.width(), thumb.height()));
    cellX += cell + gap;
  }
}

// Rows tile the popup and carry their own padding, so it doubles as the padding around the whole
// list, the same way the grid cells reach its edges.
void PreviewPopup::layoutAsList() {
  const int count = static_cast<int>(items_.size());
  const int headerHeight = items_.front()->headerHeight();
  const int rowHeight = headerHeight + (2 * padding_);
  const int availableWidth = screenGeometry_.width() - (2 * margin_);

  // A list too tall for the screen wraps into further columns rather than running off it.
  const int rowsThatFit = std::max(1, popupSpace() / rowHeight);
  const int widestAllowed = std::max(1, availableWidth / kMinWidth);
  const int columns = std::max(1, std::min(ceilDiv(count, rowsThatFit), widestAllowed));
  const int rows = ceilDiv(count, columns);

  // Columns are as wide as the longest title needs, between one and two thumbnail widths.
  int wanted = 0;
  for (const PreviewItem* item : items_) wanted = std::max(wanted, item->naturalWidth());
  wanted += 2 * padding_;
  int columnWidth = std::min(std::max(wanted, width_), 2 * width_);
  columnWidth = std::max(kMinWidth, std::min(columnWidth, availableWidth / columns));

  const int totalHeight = rows * rowHeight;
  const int popupWidth = place(columnWidth * columns, totalHeight);
  columnWidth = popupWidth / columns;

  for (int index = 0; index < count; ++index) {
    PreviewItem* item = items_[static_cast<size_t>(index)];
    const int column = index / rows;
    const int row = index % rows;
    // The last column takes the width left over, so every row reaches the popup edge.
    const int left = column * columnWidth;
    const int right = column == columns - 1 ? popupWidth : left + columnWidth;
    const int top = row * rowHeight;
    const int headerWidth = right - left - (2 * padding_);
    item->setGeometry(left, top, right - left, rowHeight);
    item->header()->setGeometry(padding_, padding_, headerWidth, headerHeight);
    item->updateElidedTitle(headerWidth);
    item->roundOuterCorners(cornerRadius(), left == 0, right == popupWidth, top == 0,
                            top + rowHeight == totalHeight);
  }
}

void PreviewPopup::closeWindow(platform::WindowHandle hwnd) {
  try {
    if (system_ && system_->isWindow(hwnd)) system_->closeApplication(hwnd, false);
    if (!manager_) return;
    std::vector<platform::WindowHandle> remaining;
    for (const PreviewItem* item : items_) {
      if (item->hwnd() != hwnd) remaining.push_back(item->hwnd());
    }
    if (!remaining.empty() && anchor_) {
      QPointer<PreviewManager> manager = manager_;
      QPointer<QWidget> anchor = anchor_;
      QTimer::singleShot(0, manager_, [manager, anchor, remaining] {
        if (manager && anchor) manager->showPreviewForHwnds(remaining, anchor);
      });
    } else {
      manager_->hidePreview();
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Failed to close window:" << error.what();
  }
}

void PreviewPopup::activateTarget(platform::WindowHandle hwnd) {
  try {
    if (system_ && system_->isWindow(hwnd) && manager_) {
      // hidePreview() deletes this popup, so keep the manager and go through it.
      PreviewManager* manager = manager_;
      manager->taskbar()->bringToForeground(hwnd);
      manager->hidePreview();
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Failed to bring window to foreground:" << error.what();
  }
}

void PreviewPopup::mousePressEvent(QMouseEvent* event) {
  // A click on the popup background activates the window it was opened from.
  if (srcHwnd_) activateTarget(*srcHwnd_);
  QFrame::mousePressEvent(event);
}

void PreviewPopup::enterEvent(QEnterEvent* event) {
  cancelHide();
  QFrame::enterEvent(event);
}

void PreviewPopup::leaveEvent(QEvent* event) {
  scheduleHide();
  QFrame::leaveEvent(event);
}

void PreviewPopup::cancelHide() { hideTimer_.stop(); }

void PreviewPopup::scheduleHide() { hideTimer_.start(); }

QRect PreviewPopup::globalArea() const { return QRect(mapToGlobal(QPoint(0, 0)), size()); }

bool PreviewPopup::keepsHover(const QPoint& cursor) const {
  const QRect area = globalArea();
  if (area.contains(cursor)) return true;
  if (!anchor_) return false;
  const QRect anchorRect(anchor_->mapToGlobal(QPoint(0, 0)), anchor_->size());
  if (anchorRect.contains(cursor)) return true;
  // The gap between the bar and the preview, never the bar row itself, so crossing the gap keeps
  // the preview open while moving along the bar lets it close.
  const QRect span = area.united(anchorRect);
  const int top = std::min(area.bottom(), anchorRect.bottom()) + 1;
  const int bottom = std::max(area.top(), anchorRect.top()) - 1;
  return QRect(span.left(), top, span.width(), bottom - top + 1).contains(cursor);
}

void PreviewPopup::doHide() {
  if (!isVisible()) return;
  if (keepsHover(QCursor::pos())) {
    hideTimer_.start();
    return;
  }
  if (manager_) manager_->hidePreview();
}

void PreviewPopup::startAnimation() {
  if (fade_.running() || !finalPos_) return;
  fade_.start();
}

// --- PreviewManager

PreviewManager::PreviewManager(TaskbarWidget* taskbar,
                               std::shared_ptr<platform::WindowSystem> system,
                               std::shared_ptr<platform::ThumbnailBackend> thumbnails,
                               std::shared_ptr<platform::LivePreviewBackend> livePreview, int width,
                               int padding, int margin, bool blur, bool peek)
    : QObject(taskbar),
      taskbar_(taskbar),
      system_(std::move(system)),
      thumbnails_(std::move(thumbnails)),
      livePreview_(std::move(livePreview)),
      width_(width),
      padding_(padding),
      margin_(margin),
      blur_(blur),
      peek_(peek && livePreview_ && livePreview_->isAvailable()) {
  // Peek needs the undocumented DwmpActivateLivePreview; without it the preview works without peek.
  if (peek && !peek_) qWarning() << "Taskbar peek is unavailable on this Windows build";
  if (peek_) livePreview_->activate(false, 0, 0);
  peekOn_.setSingleShot(true);
  peekOn_.setInterval(kPeekDelayMs);
  connect(&peekOn_, &QTimer::timeout, this, [this] { takePeek(); });
  peekOff_.setSingleShot(true);
  peekOff_.setInterval(60);
  connect(&peekOff_, &QTimer::timeout, this, [this] { applyPeek(0); });
}

PreviewManager::~PreviewManager() { stop(); }

void PreviewManager::stop() {
  releaseThumbnails();
  if (popup_) {
    popup_->close();
    popup_->deleteLater();
  }
  popup_.clear();
}

void PreviewManager::setPeek(platform::WindowHandle hwnd, bool hovered) {
  if (!peek_) return;
  if (!hovered) {
    if (hwnd == peeking_ || hwnd == peekPending_) {
      peekOn_.stop();
      peekPending_ = 0;
      if (hwnd == peeking_) peekOff_.start();
    }
  } else if (peeking_) {
    // The desktop is already faded, so moving to another window switches at once.
    peekOff_.stop();
    applyPeek(hwnd);
  } else if (peekPending_ != hwnd || !peekOn_.isActive()) {
    // An item reports the pointer more than once on arrival, so the wait is timed from the first
    // of those rather than restarted by each.
    peekOff_.stop();
    peekPending_ = hwnd;
    peekOn_.start();
  }
}

void PreviewManager::takePeek() {
  const platform::WindowHandle hwnd = peekPending_;
  peekPending_ = 0;
  if (pointerOnPreview()) applyPeek(hwnd);
}

bool PreviewManager::pointerOnPreview() const {
  if (pointerOnPreview_) return pointerOnPreview_();
  return popup_ && popup_->isVisible() && popup_->globalArea().contains(QCursor::pos());
}

void PreviewManager::applyPeek(platform::WindowHandle hwnd) {
  if (hwnd == peeking_) return;
  QWidget* bar = taskbar_ ? taskbar_->window() : nullptr;
  const platform::WindowHandle caller = bar ? static_cast<platform::WindowHandle>(bar->winId()) : 0;
  livePreview_->activate(hwnd != 0, hwnd, caller);
  peeking_ = hwnd;
}

void PreviewManager::keepOutOfPeek() {
  if (!peek_) return;
  QWidget* bar = taskbar_ ? taskbar_->window() : nullptr;
  if (bar) livePreview_->excludeFromPeek(bar->winId());
  if (popup_) livePreview_->excludeFromPeek(popup_->winId());
  for (const auto& host : hosts_) {
    if (host) livePreview_->excludeFromPeek(host->winId());
  }
}

void PreviewManager::releaseThumbnails() {
  // Every teardown path runs through here, so the desktop can never stay faded out.
  peekOn_.stop();
  peekOff_.stop();
  peekPending_ = 0;
  if (peek_) applyPeek(0);
  for (auto& fade : hostFades_) fade->stop();
  hostFades_.clear();
  if (popup_) {
    for (PreviewItem* item : popup_->items()) item->thumbnail.reset();
  }
  for (const auto& host : hosts_) {
    if (host) {
      host->hide();
      host->deleteLater();
    }
  }
  hosts_.clear();
}

void PreviewManager::showPreviewForHwnds(const std::vector<platform::WindowHandle>& hwnds,
                                         QWidget* anchor) {
  try {
    // Close the previous preview first so hidden widgets do not pile up.
    releaseThumbnails();
    if (popup_) {
      popup_->close();
      popup_->deleteLater();
      popup_.clear();
    }

    popup_ = new PreviewPopup(taskbar_, width_, padding_, margin_, kAnimationMs, blur_, peek_);
    popup_->setManager(this);

    std::vector<PreviewEntry> entries;
    bool flashing = false;
    for (platform::WindowHandle hwnd : hwnds) {
      PreviewEntry entry;
      entry.hwnd = hwnd;
      QString title;
      QPixmap icon;
      if (taskbar_->windowEntry(hwnd, title, icon)) {
        entry.title = title;
        entry.icon = icon;
      }
      entry.flashing = taskbar_->isWindowFlashing(hwnd);
      entries.push_back(entry);
      flashing = flashing || taskbar_->containerClass(hwnd).contains(QLatin1String("flashing"));
    }

    if (!popup_->showFor(entries, anchor, *system_)) {
      popup_->deleteLater();
      popup_.clear();
      return;
    }
    if (flashing) {
      popup_->content()->setProperty("class", "taskbar-preview flashing");
      repolish(popup_->content());
    }

    if (popup_->listMode()) {
      // Nothing to register, the list is laid out and masking would only cut holes in it.
      keepOutOfPeek();
    } else {
      // Register the thumbnails, which recalculates the layout with accurate DWM sizes.
      showExternalThumbnails();
    }

    if (popup_) {
      popup_->startAnimation();
      // Showing the popup puts it on top, lift the thumbnails back above it.
      for (const auto& host : hosts_) {
        if (host) host->raise();
      }
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Failed to show taskbar preview:" << error.what();
    if (popup_) {
      popup_->close();
      popup_->deleteLater();
      popup_.clear();
    }
  }
}

void PreviewManager::hidePreview() {
  try {
    releaseThumbnails();
    if (popup_) {
      if (popup_->isVisible()) popup_->clearMask();
      popup_->hide();
      popup_->close();
      popup_->deleteLater();
      popup_.clear();
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Error while hiding the taskbar preview:" << error.what();
  }
}

void PreviewManager::showExternalThumbnails() {
  if (!popup_) return;
  std::vector<PreviewItem*> registered;
  for (PreviewItem* item : popup_->items()) {
    auto* host = new ThumbnailHost(popup_, item);
    host->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint |
                         Qt::NoDropShadowWindowHint | Qt::WindowDoesNotAcceptFocus);
    host->setAttribute(Qt::WA_TranslucentBackground, false);
    host->setAttribute(Qt::WA_ShowWithoutActivating, true);
    host->setAttribute(Qt::WA_NoSystemBackground, false);

    auto thumbnail = thumbnails_->registerThumbnail(host->winId(), item->hwnd());
    if (!thumbnail) {
      // The window died between the layout pass and now, drop its slot.
      host->deleteLater();
      item->hide();
      item->deleteLater();
      continue;
    }
    item->thumbnail = std::move(thumbnail);
    registered.push_back(item);
    hosts_.push_back(host);
  }

  popup_->setItems(registered);
  if (registered.empty()) {
    hidePreview();
    return;
  }

  // Recalculate with accurate dimensions now that DWM can report the source sizes.
  popup_->calculateAndPosition();
  const std::optional<QPoint> base = popup_->finalPos();
  if (!base) return;
  const qreal dpr = popup_->dpr();
  QRegion holes;

  for (size_t index = 0; index < registered.size(); ++index) {
    ThumbnailHost* host = hosts_[index];
    PreviewItem* item = registered[index];
    const QRect rect = item->thumbRect();
    host->setGeometry(base->x() + rect.x(), base->y() + rect.y(), rect.width(), rect.height());
    host->show();

    auto fade = std::make_unique<PreviewFade>(host, kAnimationMs);
    fade->start();
    hostFades_.push_back(std::move(fade));

    item->thumbnail->show(QSize(std::max(1, static_cast<int>(rect.width() * dpr)),
                                std::max(1, static_cast<int>(rect.height() * dpr))));
    holes = holes.united(QRegion(rect));
  }

  // Every host exists now, so peek can be told to leave our windows alone up front.
  keepOutOfPeek();

  popup_->raise();
  popup_->setMask(QRegion(0, 0, popup_->width(), popup_->height()).subtracted(holes));
}

}  // namespace yasb::widgets
