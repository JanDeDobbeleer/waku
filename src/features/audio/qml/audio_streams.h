#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include "features/audio/audio_stream_source.h"

namespace waku::audio {

// One app's audio session on the default output, shaped after a Quickshell PwNode of an audio
// stream so QML written for Quickshell.Services.Pipewire reads it unchanged: `audio` is the stream
// itself (`stream.audio.volume`, `stream.audio.muted`), `properties["application.name"]`,
// `description`, `isStream` and `ready`. `volume` is 0..1 and settable; so is `muted`.
class AudioStream : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("AudioStream objects come from AudioStreams.streams")
  Q_PROPERTY(QString id READ id CONSTANT FINAL)
  Q_PROPERTY(QString name READ name NOTIFY changed FINAL)
  Q_PROPERTY(QString description READ name NOTIFY changed FINAL)
  Q_PROPERTY(QString icon READ icon NOTIFY changed FINAL)
  Q_PROPERTY(int pid READ pid CONSTANT FINAL)
  Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY changed FINAL)
  Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed FINAL)
  Q_PROPERTY(bool active READ active NOTIFY changed FINAL)
  Q_PROPERTY(bool isStream READ isStream CONSTANT FINAL)
  Q_PROPERTY(bool ready READ ready CONSTANT FINAL)
  Q_PROPERTY(QObject* audio READ audio CONSTANT FINAL)
  Q_PROPERTY(QVariantMap properties READ properties NOTIFY changed FINAL)

 public:
  AudioStream(AudioStreamSource* source, AudioStreamInfo info, QObject* parent);

  // Copies the session in; emits `changed` when something differs.
  void update(const AudioStreamInfo& info);

  QString id() const { return info_.id; }
  QString name() const { return info_.name; }
  QString icon() const { return info_.icon; }
  int pid() const { return static_cast<int>(info_.pid); }
  double volume() const { return info_.volume; }
  void setVolume(double volume);
  bool muted() const { return info_.muted; }
  void setMuted(bool muted);
  bool active() const { return info_.active; }
  bool isStream() const { return true; }
  bool ready() const { return true; }
  QObject* audio() { return this; }
  QVariantMap properties() const;

 signals:
  void changed();

 private:
  AudioStreamSource* source_;
  AudioStreamInfo info_;
};

// The streams as a model (role `modelData`) with a `values` list, like Quickshell's ObjectModel.
class AudioStreamModel : public QAbstractListModel {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(QList<waku::audio::AudioStream*> values READ values NOTIFY valuesChanged FINAL)
  Q_PROPERTY(int count READ count NOTIFY valuesChanged FINAL)

 public:
  explicit AudioStreamModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QList<AudioStream*> values() const { return streams_; }
  int count() const { return static_cast<int>(streams_.size()); }
  AudioStream* find(const QString& id) const;
  void append(AudioStream* stream);
  void remove(AudioStream* stream);

 signals:
  void valuesChanged();

 private:
  QList<AudioStream*> streams_;
};

// The per-app audio sessions of the default output device as a QML singleton: `streams` lists an
// `AudioStream` per app session (system sounds excluded). The engine destroys it, which releases
// the Core Audio callbacks. Without a live desktop there is no source and the list stays empty.
class AudioStreams : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available CONSTANT FINAL)
  Q_PROPERTY(waku::audio::AudioStreamModel* streams READ streams CONSTANT FINAL)

 public:
  explicit AudioStreams(QObject* parent = nullptr);
  // Takes ownership of `source` (also the test seam: pass a fake); null means no source.
  AudioStreams(AudioStreamSource* source, QObject* parent);
  ~AudioStreams() override;

  bool available() const { return source_ != nullptr; }
  AudioStreamModel* streams() { return &streams_; }

 private:
  void sync();

  AudioStreamSource* source_;
  AudioStreamModel streams_;
};

}  // namespace waku::audio
