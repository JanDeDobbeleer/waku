#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>

#include "features/media/media_session.h"

namespace waku::media {

// Enum holders for QML (`MprisLoopState.Track`, `MprisPlaybackState.Playing`), as in
// Quickshell.Services.Mpris.
class MprisLoopState : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("MprisLoopState only holds the enum")

 public:
  enum Enum { None = 0, Track = 1, Playlist = 2 };
  Q_ENUM(Enum)
};

class MprisPlaybackState : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("MprisPlaybackState only holds the enum")

 public:
  enum Enum { Stopped = 0, Playing = 1, Paused = 2 };
  Q_ENUM(Enum)
};

// One GSMTC session in the shape of Quickshell's MprisPlayer. Properties GSMTC cannot supply stay
// empty (`desktopEntry`). `position` and `length` are in seconds; assigning `position` seeks. The
// object lives as long as its session; a vanished session destroys it. GUI thread only.
class MprisPlayer : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("MprisPlayer objects come from Mpris.players")
  Q_PROPERTY(QString uniqueId READ uniqueId CONSTANT FINAL)
  Q_PROPERTY(QString identity READ identity NOTIFY identityChanged FINAL)
  Q_PROPERTY(QString desktopEntry READ desktopEntry CONSTANT FINAL)
  Q_PROPERTY(QString dbusName READ dbusName CONSTANT FINAL)
  Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY trackTitleChanged FINAL)
  Q_PROPERTY(QString trackArtist READ trackArtist NOTIFY trackArtistChanged FINAL)
  Q_PROPERTY(QString trackAlbum READ trackAlbum NOTIFY trackAlbumChanged FINAL)
  Q_PROPERTY(QString trackArtUrl READ trackArtUrl NOTIFY trackArtUrlChanged FINAL)
  Q_PROPERTY(QVariantMap metadata READ metadata NOTIFY metadataChanged FINAL)
  Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
  Q_PROPERTY(double length READ length NOTIFY lengthChanged FINAL)
  Q_PROPERTY(bool positionSupported READ positionSupported NOTIFY canSeekChanged FINAL)
  Q_PROPERTY(bool lengthSupported READ lengthSupported NOTIFY canSeekChanged FINAL)
  Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged FINAL)
  Q_PROPERTY(waku::media::MprisPlaybackState::Enum playbackState READ playbackState NOTIFY
                 playbackStateChanged FINAL)
  Q_PROPERTY(bool canControl READ canControl CONSTANT FINAL)
  Q_PROPERTY(bool canPlay READ canPlay NOTIFY canPlayChanged FINAL)
  Q_PROPERTY(bool canPause READ canPause NOTIFY canPlayChanged FINAL)
  Q_PROPERTY(bool canTogglePlaying READ canTogglePlaying NOTIFY canPlayChanged FINAL)
  Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY canGoNextChanged FINAL)
  Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY canGoPreviousChanged FINAL)
  Q_PROPERTY(bool canSeek READ canSeek NOTIFY canSeekChanged FINAL)
  Q_PROPERTY(bool shuffleSupported READ shuffleSupported NOTIFY shuffleSupportedChanged FINAL)
  Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
  Q_PROPERTY(bool loopSupported READ loopSupported NOTIFY loopSupportedChanged FINAL)
  Q_PROPERTY(waku::media::MprisLoopState::Enum loopState READ loopState WRITE setLoopState NOTIFY
                 loopStateChanged)

 public:
  MprisPlayer(MediaSessionService* service, QString appId, int serial, QObject* parent);

  QString appId() const { return appId_; }
  // Copies the session's state in and emits one signal per property that changed.
  void update(const MediaSessionState& state);

  QString uniqueId() const { return QString::number(serial_); }
  QString identity() const { return identity_; }
  QString desktopEntry() const { return {}; }
  QString dbusName() const { return appId_; }
  QString trackTitle() const { return title_; }
  QString trackArtist() const { return artist_; }
  QString trackAlbum() const { return album_; }
  QString trackArtUrl() const { return artUrl_; }
  QVariantMap metadata() const;
  double position() const { return position_; }
  void setPosition(double seconds);
  double length() const { return length_; }
  bool positionSupported() const { return canSeek_; }
  bool lengthSupported() const { return canSeek_; }
  bool isPlaying() const { return playing_; }
  MprisPlaybackState::Enum playbackState() const;
  bool canControl() const { return true; }
  bool canPlay() const { return canPlay_; }
  bool canPause() const { return canPlay_; }
  bool canTogglePlaying() const { return canPlay_; }
  bool canGoNext() const { return canNext_; }
  bool canGoPrevious() const { return canPrevious_; }
  bool canSeek() const { return canSeek_; }
  bool shuffleSupported() const { return shuffleSupported_; }
  bool shuffle() const { return shuffle_; }
  void setShuffle(bool enabled);
  bool loopSupported() const { return loopSupported_; }
  MprisLoopState::Enum loopState() const { return loopState_; }
  void setLoopState(MprisLoopState::Enum state);

  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void togglePlaying();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void next();
  Q_INVOKABLE void previous();
  // Relative seek in seconds.
  Q_INVOKABLE void seek(double offset);

 signals:
  void identityChanged();
  void trackTitleChanged();
  void trackArtistChanged();
  void trackAlbumChanged();
  void trackArtUrlChanged();
  void metadataChanged();
  void positionChanged();
  void lengthChanged();
  void isPlayingChanged();
  void playbackStateChanged();
  void canPlayChanged();
  void canGoNextChanged();
  void canGoPreviousChanged();
  void canSeekChanged();
  void shuffleSupportedChanged();
  void shuffleChanged();
  void loopSupportedChanged();
  void loopStateChanged();
  // After the title, artist or album changed, once every track property holds its new value.
  void postTrackChanged();

 private:
  MediaSessionService* service_;
  QString appId_;
  int serial_;
  QString identity_;
  QString title_;
  QString artist_;
  QString album_;
  QByteArray artBytes_;
  QString artUrl_;
  double position_ = 0;
  double length_ = 0;
  int status_ = 0;
  bool playing_ = false;
  bool canPlay_ = true;
  bool canNext_ = true;
  bool canPrevious_ = true;
  bool canSeek_ = false;
  bool shuffleSupported_ = false;
  bool shuffle_ = false;
  bool loopSupported_ = false;
  MprisLoopState::Enum loopState_ = MprisLoopState::None;
};

// The players as a model (role `modelData`) with a `values` list, like Quickshell's ObjectModel.
class MprisPlayerModel : public QAbstractListModel {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(QList<waku::media::MprisPlayer*> values READ values NOTIFY valuesChanged FINAL)
  Q_PROPERTY(int count READ count NOTIFY valuesChanged FINAL)

 public:
  explicit MprisPlayerModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QList<MprisPlayer*> values() const { return players_; }
  int count() const { return static_cast<int>(players_.size()); }
  MprisPlayer* find(const QString& appId) const;
  void append(MprisPlayer* player);
  void remove(MprisPlayer* player);

 signals:
  void valuesChanged();

 private:
  QList<MprisPlayer*> players_;
};

// Quickshell.Services.Mpris' `Mpris` singleton over the GSMTC sessions. `players` is every media
// session; `activePlayer` is the one waku follows (the system's current session; assigning a
// player selects it). Shares the process's MediaSessionService with `Media`.
class Mpris : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(waku::media::MprisPlayerModel* players READ players CONSTANT FINAL)
  Q_PROPERTY(waku::media::MprisPlayer* activePlayer READ activePlayer WRITE setActivePlayer NOTIFY
                 activePlayerChanged)

 public:
  explicit Mpris(QObject* parent = nullptr);
  // Test seam: a service whose source is a fake.
  Mpris(std::shared_ptr<MediaSessionService> service, QObject* parent);
  ~Mpris() override;

  MprisPlayerModel* players() { return &players_; }
  MprisPlayer* activePlayer() const { return active_; }
  void setActivePlayer(MprisPlayer* player);

 signals:
  void activePlayerChanged();

 private:
  void sync();

  std::shared_ptr<MediaSessionService> service_;
  MprisPlayerModel players_;
  MprisPlayer* active_ = nullptr;
  int nextSerial_ = 1;
};

}  // namespace waku::media
