#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

#include "platform/media_session.h"

namespace yasb::services {

// The current system media session (GSMTC) as a QML singleton, shared by every widget. Wraps
// platform::MediaSessionService, which owns the session thread; this object lives on the GUI
// thread and re-emits the service's change signals as one `changed`. The engine destroys it,
// which stops the service and joins the session thread. `position` is interpolated by the service
// on a GUI-thread timer while a session plays. Album art is not exposed yet.
class Media : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
  Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
  Q_PROPERTY(QString artist READ artist NOTIFY changed FINAL)
  Q_PROPERTY(QString album READ album NOTIFY changed FINAL)
  Q_PROPERTY(QString sourceName READ sourceName NOTIFY changed FINAL)
  Q_PROPERTY(bool playing READ playing NOTIFY changed FINAL)
  Q_PROPERTY(double position READ position NOTIFY changed FINAL)
  Q_PROPERTY(double duration READ duration NOTIFY changed FINAL)
  Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY changed FINAL)
  Q_PROPERTY(bool canPlayPause READ canPlayPause NOTIFY changed FINAL)
  Q_PROPERTY(bool canNext READ canNext NOTIFY changed FINAL)

 public:
  explicit Media(QObject* parent = nullptr);
  ~Media() override;

  bool available() const { return current() != nullptr; }
  QString title() const;
  QString artist() const;
  QString album() const;
  QString sourceName() const;
  bool playing() const;
  double position() const;
  double duration() const;
  bool canPrevious() const;
  bool canPlayPause() const;
  bool canNext() const;

  Q_INVOKABLE void playPause() { service_.playPause(); }
  Q_INVOKABLE void next() { service_.next(); }
  Q_INVOKABLE void previous() { service_.previous(); }
  Q_INVOKABLE void seek(double seconds) { service_.seek(seconds); }

 signals:
  void changed();

 private:
  const platform::MediaSessionState* current() const { return service_.currentSession(); }

  platform::MediaSessionService service_;
};

}  // namespace yasb::services
