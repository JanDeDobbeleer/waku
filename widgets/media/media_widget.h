#pragma once

#include <QImage>
#include <QLabel>
#include <QMap>
#include <QPixmap>
#include <QPointer>
#include <QProgressBar>
#include <QSlider>
#include <QString>
#include <functional>

#include "core/bar/theme_class.h"
#include "core/widgets/base_widget.h"
#include "core/widgets/popup.h"
#include "core/widgets/registry.h"
#include "platform/media_session.h"

namespace yasb::widgets {

class MediaWidget;

// Port of the label-cleaning half of services/media/tokenizer.py: drops placeholders whose value
// is empty and separators ({s}) that no longer sit between two placeholders.
QString cleanMediaLabel(const QString& content, const QMap<QString, QString>& values);

// Python len()/slicing work on code points; QString::size() counts UTF-16 units.
QString truncateMediaField(const QString& text, int maxSize);

// "m:ss" or "h:mm:ss" (MediaWidget._format_time).
QString formatMediaTime(double seconds);

struct ThumbnailStyle {
  int labelWidth = 0;       // sizeHint width of the active label
  int paddingAdded = 0;     // thumbnail_padding unless a scrolling label is used
  int containerHeight = 1;  // contents height of the widget container
  int alpha = 50;
  int cornerRadius = 0;
  bool controlsLeft = true;
  bool symmetric = false;
  bool edgeFade = false;
};

// MediaWidget._crop_thumbnail: scale to the label width, crop to the bar height, apply alpha with
// edge fade or rounded corners. A null image means the thumbnail could not be built.
QImage renderBarThumbnail(const QImage& source, const ThumbnailStyle& style);
// MediaWidget._create_thumbnail_for_popup: cover-scale and centre-crop to a square.
QImage renderPopupThumbnail(const QImage& source, int size);

class MediaMenuPopup;

// ClickableLabel: a left click on an enabled (no "disabled" class) label runs `action`; errors are
// logged, never thrown into Qt.
class MediaButton : public QLabel {
 public:
  explicit MediaButton(QWidget* parent = nullptr) : QLabel(parent) {}
  std::function<void()> action;

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
};

// RoundedClickableLabel: clips its pixmap to rounded corners at paint time.
class MediaThumbnailLabel final : public MediaButton {
 public:
  MediaThumbnailLabel(QWidget* parent, int radius) : MediaButton(parent), radius_(radius) {}

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  int radius_;
};

// yasb.media.MediaWidget. The session service is shared (see platform/media_session.h); every
// slot here runs on the GUI thread.
class MediaWidget final : public BaseWidget {
  Q_OBJECT

 public:
  static constexpr double kMaxTimelineDuration = 604800;  // 7 days

  MediaWidget(const WidgetContext& context, platform::MediaSessionService* service);

  // Test and parity hooks.
  QLabel* label() const { return label_; }
  QLabel* labelAlt() const { return labelAlt_; }
  QLabel* thumbnailLabel() const { return thumbnailLabel_; }
  QProgressBar* progressBar() const { return progressBar_; }
  QWidget* progressContainer() const { return progressContainer_; }
  QLabel* prevButton() const;
  QLabel* playButton() const;
  QLabel* nextButton() const;
  MediaMenuPopup* menu() const;
  bool showingAltLabel() const { return showAltLabel_; }

  void showMenu();

 protected:
  void wheelEvent(QWheelEvent* event) override;

 private:
  const platform::MediaSessionState* current() const;
  void onMediaDataChanged();
  void onSessionStatusChanged();
  void onMediaPropertiesChanged();
  void onTimelineChanged();
  void onPlaybackInfoChanged();
  void updateInterpolatedPosition();
  void updatePopupMenuButtons();
  void updatePopupProperties(const platform::MediaSessionState& session);
  void toggleLabel();
  void togglePlayPause();
  void openMediaSource();
  QString formatMaxFieldSize(const QString& text, const char* fieldType = "default") const;
  MediaButton* createMediaButton(const QString& icon, std::function<void()> action);
  QPixmap popupThumbnailFor(const platform::MediaSessionState& session) const;
  void applyButtonState(QLabel* button, const char* base, bool enabled) const;
  void onSliderReleased();
  void onSliderValueChanged(int value);

  config::options::MediaWidgetConfig options_;
  bar::GlobalState& globalState_;
  QPointer<platform::MediaSessionService> service_;

  QLabel* label_ = nullptr;
  QLabel* labelAlt_ = nullptr;
  QLabel* thumbnailLabel_ = nullptr;
  QProgressBar* progressBar_ = nullptr;
  QWidget* progressContainer_ = nullptr;
  QPointer<MediaButton> prev_;
  QPointer<MediaButton> play_;
  QPointer<MediaButton> next_;
  QPixmap emptyThumb_;
  bool showAltLabel_ = false;
  bool isPlaying_ = false;
  bool seeking_ = false;
  QString lastAppId_;
  bool hadSession_ = false;

  QPointer<MediaMenuPopup> dialog_;
  QPointer<QLabel> popupTitle_;
  QPointer<QLabel> popupArtist_;
  QPointer<QLabel> popupSource_;
  QPointer<MediaThumbnailLabel> popupThumbnail_;
  QPointer<MediaButton> popupPrev_;
  QPointer<MediaButton> popupPlay_;
  QPointer<MediaButton> popupNext_;
  QPointer<QLabel> popupCurrentTime_;
  QPointer<QLabel> popupTotalTime_;
  QPointer<QSlider> progressSlider_;
};

// Frameless media flyout (class "media-menu"); blur and rounded corners are applied on show.
class MediaMenuPopup final : public PopupWidget {
  Q_OBJECT

 public:
  MediaMenuPopup(QWidget* anchor, bar::GlobalState& globalState, bool blur, bool roundCorners,
                 const QString& roundCornersType, const QString& borderColor);

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  bool blur_;
  bool roundCorners_;
  QString roundCornersType_;
  QString borderColor_;
  bar::GlobalState& globalState_;
};

// Registers yasb.media.MediaWidget; the shared MediaSessionService is created on first use.
void registerMediaWidgets(WidgetRegistry& registry);

}  // namespace yasb::widgets
