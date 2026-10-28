#pragma once

#include <QtQml/qqmlregistration.h>

#include <QVariantList>
#include <memory>

#include "platform/system_stats.h"
#include "spike/services/stat_source.h"

namespace yasb::services {

class Memory : public StatSource {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(double percent READ percent NOTIFY updated)
  Q_PROPERTY(qlonglong used READ used NOTIFY updated)
  Q_PROPERTY(qlonglong total READ total NOTIFY updated)
  Q_PROPERTY(qlonglong available READ available NOTIFY updated)
  Q_PROPERTY(double swapPercent READ swapPercent NOTIFY updated)

 public:
  explicit Memory(QObject* parent = nullptr);

  double percent() const { return sample_.percent; }
  qlonglong used() const { return static_cast<qlonglong>(sample_.used); }
  qlonglong total() const { return static_cast<qlonglong>(sample_.total); }
  qlonglong available() const { return static_cast<qlonglong>(sample_.available); }
  double swapPercent() const { return sample_.swapPercent; }

 protected:
  void sample() override;

 private:
  std::shared_ptr<platform::MemoryDataSource> source_;
  platform::MemorySample sample_;
};

class Cpu : public StatSource {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(double percent READ percent NOTIFY updated)
  Q_PROPERTY(QVariantList perCore READ perCore NOTIFY updated)
  Q_PROPERTY(double frequency READ frequency NOTIFY updated)
  Q_PROPERTY(int cores READ cores NOTIFY updated)

 public:
  explicit Cpu(QObject* parent = nullptr);

  double percent() const { return sample_.percent; }
  QVariantList perCore() const;
  double frequency() const { return sample_.frequencyCurrent; }
  int cores() const { return sample_.coresLogical; }

 protected:
  void sample() override;

 private:
  std::shared_ptr<platform::CpuDataSource> source_;
  platform::CpuSample sample_;
};

class Battery : public StatSource {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool available READ available NOTIFY updated)
  Q_PROPERTY(int percent READ percent NOTIFY updated)
  Q_PROPERTY(bool charging READ charging NOTIFY updated)
  Q_PROPERTY(bool plugged READ plugged NOTIFY updated)
  Q_PROPERTY(int timeRemaining READ timeRemaining NOTIFY updated)

 public:
  explicit Battery(QObject* parent = nullptr);

  bool available() const { return available_; }
  int percent() const { return sample_.percent; }
  bool charging() const { return sample_.charging; }
  bool plugged() const { return sample_.plugged; }
  int timeRemaining() const { return sample_.timeRemaining; }

 protected:
  void sample() override;

 private:
  std::shared_ptr<platform::BatteryDataSource> source_;
  platform::BatterySample sample_;
  bool available_ = false;
};

}  // namespace yasb::services
