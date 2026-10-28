#pragma once

#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRect>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "platform/dwm_thumbnail.h"
#include "platform/window_system.h"

namespace yasb::widgets {

class TaskbarWidget;
class PreviewItem;
class PreviewPopup;
class PreviewManager;

// How many groups of `per` it takes to hold `count`.
int ceilDiv(int count, int per);

// How many thumbnails fit side by side once shrunk as far as PreviewPopup::kMinWidth allows.
int previewGridColumns(int count, int screenWidth, int margin, int padding);

struct PreviewEntry {
  platform::WindowHandle hwnd = 0;
  // nullopt: ask the window system for the title.
  std::optional<QString> title;
  QPixmap icon;
  bool flashing = false;
};

// Fades a top-level window in (windowOpacity 0 -> 1). Port of PreviewAnimation.
class PreviewFade {
 public:
  PreviewFade(QWidget* widget, int durationMs) : widget_(widget), duration_(durationMs) {}
  void start();
  void stop();
  bool running() const { return running_; }

 private:
  QPointer<QWidget> widget_;
  int duration_;
  QPointer<QPropertyAnimation> animation_;
  bool running_ = false;
};

// A frameless window sitting over the popup's thumbnail hole; DWM draws the live thumbnail into
// it. It captures the pointer for hover and clicks.
class ThumbnailHost final : public QWidget {
  Q_OBJECT

 public:
  ThumbnailHost(PreviewPopup* popup, PreviewItem* item);

 protected:
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

 private:
  QPointer<PreviewPopup> popup_;
  QPointer<PreviewItem> item_;
  platform::WindowHandle hwnd_;
};

class PreviewCloseButton final : public QLabel {
 public:
  explicit PreviewCloseButton(QWidget* parent) : QLabel(parent) {}
  std::function<void()> action;

 protected:
  void mousePressEvent(QMouseEvent*) override {
    if (action) action();
  }
};

// One window inside a preview popup: a header (icon, title, close button) above its thumbnail.
class PreviewItem final : public QFrame {
  Q_OBJECT

 public:
  static constexpr int kIconSize = 16;
  static constexpr int kSpacing = 6;

  PreviewItem(PreviewPopup* popup, const PreviewEntry& entry, const QString& title);

  platform::WindowHandle hwnd() const { return hwnd_; }
  const QString& title() const { return title_; }
  QFrame* header() const { return header_; }
  int headerHeight() const { return headerHeight_; }
  QLabel* titleLabel() const { return titleLabel_; }
  QLabel* iconLabel() const { return iconLabel_; }
  QLabel* closeButton() const { return closeButton_; }
  QRect thumbRect() const { return thumbRect_; }
  void setThumbRect(const QRect& rect) { thumbRect_ = rect; }
  std::unique_ptr<platform::ThumbnailHandle> thumbnail;

  void updateElidedTitle(std::optional<int> width = std::nullopt);
  int naturalWidth() const;
  void roundOuterCorners(int radius, bool atLeft, bool atRight, bool atTop, bool atBottom);
  void setHovered(bool hovered, bool peek = false);

 protected:
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

 private:
  PreviewPopup* popup_;
  platform::WindowHandle hwnd_;
  QString title_;
  QFrame* header_;
  QLabel* iconLabel_;
  QLabel* titleLabel_;
  PreviewCloseButton* closeButton_;
  int headerWidth_ = 0;
  int headerHeight_ = 0;
  QRect thumbRect_;
};

// A row of live thumbnails or, once they no longer fit, a list of titles.
class PreviewPopup final : public QFrame {
  Q_OBJECT

 public:
  // The narrowest a cell may get; below it the windows are listed by title instead.
  static constexpr int kMinWidth = 100;
  static constexpr int kBackdropRefreshMs = 20;

  PreviewPopup(QWidget* parent, int width, int padding, int margin, int animationDuration,
               bool blur, bool peek);

  QFrame* content() const { return content_; }
  const std::vector<PreviewItem*>& items() const { return items_; }
  void setItems(std::vector<PreviewItem*> items) { items_ = std::move(items); }
  bool listMode() const { return listMode_; }
  std::optional<platform::WindowHandle> srcHwnd() const { return srcHwnd_; }
  std::optional<QPoint> finalPos() const { return finalPos_; }
  PreviewManager* manager() const { return manager_; }
  void setManager(PreviewManager* manager) { manager_ = manager; }
  qreal dpr() const { return dpr_; }

  // Builds an item per entry and lays the popup out; false when no window is left to show.
  bool showFor(const std::vector<PreviewEntry>& entries, QWidget* anchor,
               platform::WindowSystem& system);
  void calculateAndPosition();
  QSize sourceSize(const PreviewItem* item) const;
  void startAnimation();

  void closeWindow(platform::WindowHandle hwnd);
  void activateTarget(platform::WindowHandle hwnd);
  void cancelHide();
  void scheduleHide();
  QRect globalArea() const;
  // True while the pointer is over the preview or still crossing the gap to its button.
  bool keepsHover(const QPoint& cursor) const;

 protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  int cornerRadius();
  int popupSpace() const;
  int place(int popupWidth, int popupHeight);
  void layoutAsList();
  void doHide();

  QFrame* content_;
  int width_;
  int margin_;
  int padding_;
  bool blur_;
  bool peek_;
  PreviewFade fade_;
  std::vector<PreviewItem*> items_;
  PreviewManager* manager_ = nullptr;
  std::optional<int> radius_;
  std::optional<platform::WindowHandle> srcHwnd_;
  std::optional<QPoint> finalPos_;
  QPointer<QWidget> anchor_;
  platform::WindowSystem* system_ = nullptr;
  qreal dpr_ = 1.0;
  int anchorCenterX_ = 0;
  int anchorTop_ = 0;
  int anchorBottom_ = 0;
  QRect screenGeometry_;
  bool listMode_ = false;
  QTimer hideTimer_;
  QTimer backdrop_;
};

// Port of TaskbarThumbnailManager: owns the popup, its DWM thumbnails and the peek state.
class PreviewManager final : public QObject {
  Q_OBJECT

 public:
  // How long the pointer rests on a window before the desktop is faded for it.
  static constexpr int kPeekDelayMs = 400;
  static constexpr int kAnimationMs = 200;

  PreviewManager(TaskbarWidget* taskbar, std::shared_ptr<platform::WindowSystem> system,
                 std::shared_ptr<platform::ThumbnailBackend> thumbnails,
                 std::shared_ptr<platform::LivePreviewBackend> livePreview, int width, int padding,
                 int margin, bool blur, bool peek);
  ~PreviewManager() override;

  PreviewPopup* popup() const { return popup_; }
  TaskbarWidget* taskbar() const { return taskbar_; }
  bool peekEnabled() const { return peek_; }
  platform::WindowHandle peeking() const { return peeking_; }

  void stop();
  void showPreviewForHwnds(const std::vector<platform::WindowHandle>& hwnds, QWidget* anchor);
  void hidePreview();
  void setPeek(platform::WindowHandle hwnd, bool hovered = true);
  // Test hook: the peek timer fires only while the pointer is on the preview.
  void setPointerOnPreview(std::function<bool()> probe) { pointerOnPreview_ = std::move(probe); }

 private:
  void takePeek();
  bool pointerOnPreview() const;
  void applyPeek(platform::WindowHandle hwnd);
  void keepOutOfPeek();
  void releaseThumbnails();
  void showExternalThumbnails();

  TaskbarWidget* taskbar_;
  std::shared_ptr<platform::WindowSystem> system_;
  std::shared_ptr<platform::ThumbnailBackend> thumbnails_;
  std::shared_ptr<platform::LivePreviewBackend> livePreview_;
  int width_;
  int padding_;
  int margin_;
  bool blur_;
  bool peek_;
  platform::WindowHandle peeking_ = 0;
  platform::WindowHandle peekPending_ = 0;
  QTimer peekOn_;
  // Restoring the desktop is deferred, so moving between thumbnails does not bounce it back.
  QTimer peekOff_;
  QPointer<PreviewPopup> popup_;
  std::vector<QPointer<ThumbnailHost>> hosts_;
  std::vector<std::unique_ptr<PreviewFade>> hostFades_;
  std::function<bool()> pointerOnPreview_;
};

}  // namespace yasb::widgets
