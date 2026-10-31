#include "features/audio/qml/audio_service.h"

#include <algorithm>

#include "kernel/qt/live_desktop.h"

namespace wabar::audio {

AudioEndpoints* systemAudioIfLive(AudioFlow flow) {
  return kernel::liveDesktop(QStringLiteral("audio")) ? createSystemAudio(flow) : nullptr;
}

AudioBase::AudioBase(AudioEndpoints* endpoints, QObject* parent)
    : QObject(parent), endpoints_(endpoints) {
  if (!endpoints_) return;
  endpoints_->setParent(this);
  connect(endpoints_, &AudioEndpoints::volumeChanged, this, &AudioBase::changed);
  connect(endpoints_, &AudioEndpoints::deviceChanged, this, &AudioBase::changed);
}

AudioBase::~AudioBase() {
  if (endpoints_) endpoints_->stop();
}

bool AudioBase::available() const { return endpoints_ && endpoints_->available(); }

int AudioBase::volume() const { return endpoints_ ? endpoints_->volumePercent().value_or(0) : 0; }

void AudioBase::setVolume(int percent) {
  if (endpoints_ && endpoints_->setVolumePercent(std::clamp(percent, 0, 100))) emit changed();
}

bool AudioBase::muted() const { return endpoints_ && endpoints_->muted().value_or(false); }

void AudioBase::setMuted(bool muted) {
  if (endpoints_ && endpoints_->setMuted(muted)) emit changed();
}

QVariantList AudioBase::devices() const {
  QVariantList list;
  if (!endpoints_) return list;
  for (const AudioDevice& device : endpoints_->devices()) {
    list << QVariantMap{{QStringLiteral("id"), device.id}, {QStringLiteral("name"), device.name}};
  }
  return list;
}

QString AudioBase::defaultDevice() const {
  return endpoints_ ? endpoints_->defaultDeviceId() : QString();
}

bool AudioBase::setDefaultDevice(const QString& id) {
  return endpoints_ && endpoints_->setDefaultDevice(id);
}

}  // namespace wabar::audio
