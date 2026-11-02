#include "features/audio/qml/audio_streams.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "kernel/qt/live_desktop.h"

namespace waku::audio {

AudioStream::AudioStream(AudioStreamSource* source, AudioStreamInfo info, QObject* parent)
    : QObject(parent), source_(source), info_(std::move(info)) {}

void AudioStream::update(const AudioStreamInfo& info) {
  if (info.name == info_.name && info.icon == info_.icon && info.volume == info_.volume &&
      info.muted == info_.muted && info.active == info_.active) {
    return;
  }
  info_ = info;
  emit changed();
}

void AudioStream::setVolume(double volume) {
  volume = std::clamp(volume, 0.0, 1.0);
  if (!source_ || !source_->setVolume(info_.id, volume)) return;
  if (volume == info_.volume) return;
  info_.volume = volume;
  emit changed();
}

void AudioStream::setMuted(bool muted) {
  if (!source_ || !source_->setMuted(info_.id, muted)) return;
  if (muted == info_.muted) return;
  info_.muted = muted;
  emit changed();
}

QVariantMap AudioStream::properties() const {
  return {{QStringLiteral("application.name"), info_.name},
          {QStringLiteral("application.icon_name"), info_.icon},
          {QStringLiteral("application.process.id"), static_cast<int>(info_.pid)},
          {QStringLiteral("media.class"), QStringLiteral("Stream/Output/Audio")}};
}

int AudioStreamModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(streams_.size());
}

QVariant AudioStreamModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= streams_.size() ||
      role != Qt::UserRole) {
    return {};
  }
  return QVariant::fromValue(streams_.at(index.row()));
}

QHash<int, QByteArray> AudioStreamModel::roleNames() const {
  return {{Qt::UserRole, QByteArrayLiteral("modelData")}};
}

AudioStream* AudioStreamModel::find(const QString& id) const {
  for (AudioStream* stream : streams_) {
    if (stream->id() == id) return stream;
  }
  return nullptr;
}

void AudioStreamModel::append(AudioStream* stream) {
  const int row = static_cast<int>(streams_.size());
  beginInsertRows({}, row, row);
  streams_.append(stream);
  endInsertRows();
  emit valuesChanged();
}

void AudioStreamModel::remove(AudioStream* stream) {
  const int row = static_cast<int>(streams_.indexOf(stream));
  if (row < 0) return;
  beginRemoveRows({}, row, row);
  streams_.removeAt(row);
  endRemoveRows();
  emit valuesChanged();
}

AudioStreams::AudioStreams(QObject* parent)
    : AudioStreams(
          kernel::liveDesktop(QStringLiteral("audio")) ? createSystemAudioStreams() : nullptr,
          parent) {}

AudioStreams::AudioStreams(AudioStreamSource* source, QObject* parent)
    : QObject(parent), source_(source), streams_(this) {
  if (source_) {
    source_->setParent(this);
    connect(source_, &AudioStreamSource::streamsChanged, this, &AudioStreams::sync);
  }
  sync();
}

AudioStreams::~AudioStreams() {
  if (source_) source_->stop();
}

void AudioStreams::sync() {
  const std::vector<AudioStreamInfo> infos =
      source_ ? source_->streams() : std::vector<AudioStreamInfo>{};
  for (AudioStream* stream : streams_.values()) {
    const bool present = std::any_of(infos.begin(), infos.end(), [&](const AudioStreamInfo& info) {
      return info.id == stream->id();
    });
    if (present) continue;
    streams_.remove(stream);
    stream->deleteLater();
  }
  for (const AudioStreamInfo& info : infos) {
    if (AudioStream* stream = streams_.find(info.id)) {
      stream->update(info);
    } else {
      streams_.append(new AudioStream(source_, info, this));
    }
  }
}

}  // namespace waku::audio
