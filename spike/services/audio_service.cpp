#include "spike/services/audio_service.h"

#include <algorithm>

namespace wabar::services {

AudioBase::AudioBase(platform::AudioEndpoints* endpoints, QObject* parent)
    : QObject(parent), endpoints_(endpoints) {
  endpoints_->setParent(this);
  connect(endpoints_, &platform::AudioEndpoints::volumeChanged, this, &AudioBase::changed);
  connect(endpoints_, &platform::AudioEndpoints::deviceChanged, this, &AudioBase::changed);
}

AudioBase::~AudioBase() { endpoints_->stop(); }

bool AudioBase::available() const { return endpoints_->available(); }

int AudioBase::volume() const { return endpoints_->volumePercent().value_or(0); }

void AudioBase::setVolume(int percent) {
  if (endpoints_->setVolumePercent(std::clamp(percent, 0, 100))) emit changed();
}

bool AudioBase::muted() const { return endpoints_->muted().value_or(false); }

void AudioBase::setMuted(bool muted) {
  if (endpoints_->setMuted(muted)) emit changed();
}

QVariantList AudioBase::devices() const {
  QVariantList list;
  for (const platform::AudioDevice& device : endpoints_->devices()) {
    list << QVariantMap{{QStringLiteral("id"), device.id}, {QStringLiteral("name"), device.name}};
  }
  return list;
}

QString AudioBase::defaultDevice() const { return endpoints_->defaultDeviceId(); }

bool AudioBase::setDefaultDevice(const QString& id) { return endpoints_->setDefaultDevice(id); }

}  // namespace wabar::services
