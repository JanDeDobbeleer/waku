#pragma once

#include <QtQml/qqmlregistration.h>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <memory>

#include "features/media/media_session.h"

namespace wabar::media {

// MIME type of encoded image bytes by their magic number; "application/octet-stream" when the
// format is not recognised.
QString imageMimeType(const QByteArray& bytes);
// `data:<mime>;base64,<bytes>`, or an empty string for no bytes. A QML Image loads it directly.
QString imageDataUrl(const QByteArray& bytes);

// The one MediaSessionService of the process, shared by `Media` and `Mpris` so GSMTC is opened
// once. Created on first use (system source); gone when the last holder lets go. GUI thread only.
std::shared_ptr<MediaSessionService> acquireMediaService();

// The current system media session (GSMTC) as a QML singleton, shared by every widget. Wraps
// MediaSessionService, which owns the session thread; this object lives on the GUI
// thread and re-emits the service's change signals as one `changed`. The engine destroys it,
// which stops the service and joins the session thread. `position` is interpolated by the service
// on a GUI-thread timer while a session plays. `thumbnailUrl` is the album art the player
// delivered, as a data URL (empty when the player gave none); it is rebuilt only when the bytes
// change.
class Media : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
  Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
  Q_PROPERTY(QString artist READ artist NOTIFY changed FINAL)
  Q_PROPERTY(QString album READ album NOTIFY changed FINAL)
  Q_PROPERTY(QString sourceName READ sourceName NOTIFY changed FINAL)
  Q_PROPERTY(QString thumbnailUrl READ thumbnailUrl NOTIFY changed FINAL)
  Q_PROPERTY(bool playing READ playing NOTIFY changed FINAL)
  Q_PROPERTY(double position READ position NOTIFY changed FINAL)
  Q_PROPERTY(double duration READ duration NOTIFY changed FINAL)
  Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY changed FINAL)
  Q_PROPERTY(bool canPlayPause READ canPlayPause NOTIFY changed FINAL)
  Q_PROPERTY(bool canNext READ canNext NOTIFY changed FINAL)

 public:
  explicit Media(QObject* parent = nullptr);
  // Test seams: a fake session source, or a service shared with a test's `Mpris`.
  Media(std::unique_ptr<MediaSessionSource> source, QObject* parent);
  Media(std::shared_ptr<MediaSessionService> service, QObject* parent);
  ~Media() override;

  bool available() const { return current() != nullptr; }
  QString title() const;
  QString artist() const;
  QString album() const;
  QString sourceName() const;
  QString thumbnailUrl() const { return thumbnailUrl_; }
  bool playing() const;
  double position() const;
  double duration() const;
  bool canPrevious() const;
  bool canPlayPause() const;
  bool canNext() const;

  Q_INVOKABLE void playPause() { service_->playPause(); }
  Q_INVOKABLE void next() { service_->next(); }
  Q_INVOKABLE void previous() { service_->previous(); }
  Q_INVOKABLE void seek(double seconds) { service_->seek(seconds); }

 signals:
  void changed();

 private:
  const MediaSessionState* current() const { return service_->currentSession(); }
  void onServiceChanged();

  std::shared_ptr<MediaSessionService> service_;
  QByteArray thumbnailBytes_;
  QString thumbnailUrl_;
};

}  // namespace wabar::media
