#include "features/media/qml/mpris.h"

#include <utility>

#include "features/media/qml/media_service.h"

namespace waku::media {

MprisPlayer::MprisPlayer(MediaSessionService* service, QString appId, int serial, QObject* parent)
    : QObject(parent), service_(service), appId_(std::move(appId)), serial_(serial) {}

void MprisPlayer::update(const MediaSessionState& state) {
  const QString identity = state.sourceName.isEmpty() ? state.appId : state.sourceName;
  const bool trackChanged =
      state.title != title_ || state.artist != artist_ || state.albumTitle != album_;
  const bool artChanged = state.thumbnail != artBytes_;
  const bool metadataDirty = trackChanged || artChanged || state.duration != length_;
  const MprisPlaybackState::Enum oldPlayback = playbackState();
  const bool canSeek = state.timelineEnabled;
  const bool canPlay = state.playEnabled;
  const bool shuffleSupported = state.shuffleEnabled;
  const bool loopSupported = state.repeatEnabled;
  const auto loopState = static_cast<MprisLoopState::Enum>(
      state.autoRepeatMode >= 0 && state.autoRepeatMode <= 2 ? state.autoRepeatMode : 0);

  const bool identityDirty = identity != identity_;
  const bool titleDirty = state.title != title_;
  const bool artistDirty = state.artist != artist_;
  const bool albumDirty = state.albumTitle != album_;
  const bool lengthDirty = state.duration != length_;
  const bool positionDirty = state.currentPos != position_;
  const bool playingDirty = state.isPlaying != playing_;
  const bool canPlayDirty = canPlay != canPlay_;
  const bool nextDirty = state.nextEnabled != canNext_;
  const bool previousDirty = state.prevEnabled != canPrevious_;
  const bool seekDirty = canSeek != canSeek_;
  const bool shuffleSupportedDirty = shuffleSupported != shuffleSupported_;
  const bool shuffleDirty = state.shuffleActive != shuffle_;
  const bool loopSupportedDirty = loopSupported != loopSupported_;
  const bool loopStateDirty = loopState != loopState_;

  identity_ = identity;
  title_ = state.title;
  artist_ = state.artist;
  album_ = state.albumTitle;
  length_ = state.duration;
  position_ = state.currentPos;
  status_ = state.playbackStatus;
  playing_ = state.isPlaying;
  canPlay_ = canPlay;
  canNext_ = state.nextEnabled;
  canPrevious_ = state.prevEnabled;
  canSeek_ = canSeek;
  shuffleSupported_ = shuffleSupported;
  shuffle_ = state.shuffleActive;
  loopSupported_ = loopSupported;
  loopState_ = loopState;
  if (artChanged) {
    artBytes_ = state.thumbnail;
    artUrl_ = imageDataUrl(artBytes_);
  }

  if (identityDirty) emit identityChanged();
  if (titleDirty) emit trackTitleChanged();
  if (artistDirty) emit trackArtistChanged();
  if (albumDirty) emit trackAlbumChanged();
  if (artChanged) emit trackArtUrlChanged();
  if (lengthDirty) emit lengthChanged();
  if (metadataDirty) emit metadataChanged();
  if (positionDirty) emit positionChanged();
  if (playingDirty) emit isPlayingChanged();
  if (playbackState() != oldPlayback) emit playbackStateChanged();
  if (canPlayDirty) emit canPlayChanged();
  if (nextDirty) emit canGoNextChanged();
  if (previousDirty) emit canGoPreviousChanged();
  if (seekDirty) emit canSeekChanged();
  if (shuffleSupportedDirty) emit shuffleSupportedChanged();
  if (shuffleDirty) emit shuffleChanged();
  if (loopSupportedDirty) emit loopSupportedChanged();
  if (loopStateDirty) emit loopStateChanged();
  if (trackChanged) emit postTrackChanged();
}

QVariantMap MprisPlayer::metadata() const {
  QVariantMap map;
  if (!title_.isEmpty()) map.insert(QStringLiteral("xesam:title"), title_);
  if (!artist_.isEmpty()) map.insert(QStringLiteral("xesam:artist"), QStringList{artist_});
  if (!album_.isEmpty()) map.insert(QStringLiteral("xesam:album"), album_);
  if (!artUrl_.isEmpty()) map.insert(QStringLiteral("mpris:artUrl"), artUrl_);
  if (length_ > 0) {
    map.insert(QStringLiteral("mpris:length"), static_cast<qlonglong>(length_ * 1000000.0));
  }
  return map;
}

MprisPlaybackState::Enum MprisPlayer::playbackState() const {
  if (status_ == 4) return MprisPlaybackState::Playing;
  if (status_ == 5) return MprisPlaybackState::Paused;
  return MprisPlaybackState::Stopped;
}

double MprisPlayer::position() const {
  const MediaSessionState* state = service_->findSession(appId_);
  return state ? service_->livePosition(*state) : position_;
}

void MprisPlayer::tickPosition() {
  position_ = position();
  emit positionChanged();
}

void MprisPlayer::setPosition(double seconds) {
  if (!canSeek_) return;
  service_->seekSession(appId_, seconds < 0 ? 0 : seconds);
}

void MprisPlayer::setShuffle(bool enabled) {
  if (!shuffleSupported_) return;
  service_->sendTo(appId_, MediaCommand::SetShuffle, enabled ? 1 : 0);
}

void MprisPlayer::setLoopState(MprisLoopState::Enum state) {
  if (!loopSupported_) return;
  service_->sendTo(appId_, MediaCommand::SetRepeat, static_cast<int>(state));
}

void MprisPlayer::play() { service_->sendTo(appId_, MediaCommand::Play); }
void MprisPlayer::pause() { service_->sendTo(appId_, MediaCommand::Pause); }
void MprisPlayer::togglePlaying() { service_->sendTo(appId_, MediaCommand::PlayPause); }
void MprisPlayer::stop() { service_->sendTo(appId_, MediaCommand::Stop); }
void MprisPlayer::next() { service_->sendTo(appId_, MediaCommand::Next); }
void MprisPlayer::previous() { service_->sendTo(appId_, MediaCommand::Previous); }
void MprisPlayer::seek(double offset) { setPosition(position() + offset); }

int MprisPlayerModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(players_.size());
}

QVariant MprisPlayerModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= players_.size() ||
      role != Qt::UserRole) {
    return {};
  }
  return QVariant::fromValue(players_.at(index.row()));
}

QHash<int, QByteArray> MprisPlayerModel::roleNames() const {
  return {{Qt::UserRole, QByteArrayLiteral("modelData")}};
}

MprisPlayer* MprisPlayerModel::find(const QString& appId) const {
  for (MprisPlayer* player : players_) {
    if (player->appId() == appId) return player;
  }
  return nullptr;
}

void MprisPlayerModel::append(MprisPlayer* player) {
  const int row = static_cast<int>(players_.size());
  beginInsertRows({}, row, row);
  players_.append(player);
  endInsertRows();
  emit valuesChanged();
}

void MprisPlayerModel::remove(MprisPlayer* player) {
  const int row = static_cast<int>(players_.indexOf(player));
  if (row < 0) return;
  beginRemoveRows({}, row, row);
  players_.removeAt(row);
  endRemoveRows();
  emit valuesChanged();
}

Mpris::Mpris(QObject* parent) : Mpris(acquireMediaService(), parent) {}

Mpris::Mpris(std::shared_ptr<MediaSessionService> service, QObject* parent)
    : QObject(parent), service_(std::move(service)), players_(this) {
  connect(service_.get(), &MediaSessionService::stateChanged, this, &Mpris::sync);
  connect(service_.get(), &MediaSessionService::positionTicked, this, &Mpris::onPositionTick);
  service_->start();
  sync();
}

// The players are children of this object, destroyed with it; the shared service stops its
// source when its last holder lets go.
Mpris::~Mpris() = default;

void Mpris::setActivePlayer(MprisPlayer* player) {
  if (!player || player == active_) return;
  service_->selectSession(player->appId());
}

// A tick moves only the position: no player is compared field by field.
void Mpris::onPositionTick() {
  for (MprisPlayer* player : players_.values()) {
    const MediaSessionState* state = service_->findSession(player->appId());
    if (state && state->isPlaying) player->tickPosition();
  }
}

void Mpris::sync() {
  const auto& sessions = service_->sessions();
  // Vanished sessions first.
  for (MprisPlayer* player : players_.values()) {
    if (service_->findSession(player->appId())) continue;
    players_.remove(player);
    player->deleteLater();
  }
  for (const MediaSessionState& state : sessions) {
    MprisPlayer* player = players_.find(state.appId);
    if (!player) {
      player = new MprisPlayer(service_.get(), state.appId, nextSerial_++, this);
      player->update(state);
      players_.append(player);
    } else {
      player->update(state);
    }
  }
  const MediaSessionState* current = service_->currentSession();
  MprisPlayer* active = current ? players_.find(current->appId) : nullptr;
  if (active != active_) {
    active_ = active;
    emit activePlayerChanged();
  }
}

}  // namespace waku::media
