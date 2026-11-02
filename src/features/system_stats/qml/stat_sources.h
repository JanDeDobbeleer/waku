#pragma once

#include <QtQml/qqmlregistration.h>

#include <QVariantList>
#include <memory>

#include "features/system_stats/system_stats.h"
#include "kernel/qml/stat_source.h"

namespace wabar::system_stats {

class Memory : public kernel::StatSource {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(double percent READ percent NOTIFY updated)
  Q_PROPERTY(qlonglong used READ used NOTIFY updated)
  Q_PROPERTY(qlonglong total READ total NOTIFY updated)
  Q_PROPERTY(qlonglong available READ available NOTIFY updated)
  Q_PROPERTY(double swapPercent READ swapPercent NOTIFY updated)
  Q_PROPERTY(qlonglong cached READ cached NOTIFY updated)
  Q_PROPERTY(qlonglong swapUsed READ swapUsed NOTIFY updated)
  Q_PROPERTY(qlonglong swapTotal READ swapTotal NOTIFY updated)

 public:
  explicit Memory(QObject* parent = nullptr);
  Memory(std::shared_ptr<MemoryDataSource> source, QObject* parent);

  double percent() const { return sample_.percent; }
  qlonglong used() const { return static_cast<qlonglong>(sample_.used); }
  qlonglong total() const { return static_cast<qlonglong>(sample_.total); }
  qlonglong available() const { return static_cast<qlonglong>(sample_.available); }
  double swapPercent() const { return sample_.swapPercent; }
  qlonglong cached() const { return static_cast<qlonglong>(sample_.cached); }
  qlonglong swapUsed() const { return static_cast<qlonglong>(sample_.swapUsed); }
  qlonglong swapTotal() const { return static_cast<qlonglong>(sample_.swapTotal); }

 protected:
  void sample() override;

 private:
  std::shared_ptr<MemoryDataSource> source_;
  MemorySample sample_;
};

class Cpu : public kernel::StatSource {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(double percent READ percent NOTIFY updated)
  Q_PROPERTY(QVariantList perCore READ perCore NOTIFY updated)
  Q_PROPERTY(double frequency READ frequency NOTIFY updated)
  Q_PROPERTY(int cores READ cores NOTIFY updated)
  Q_PROPERTY(int coresPhysical READ coresPhysical NOTIFY updated)
  Q_PROPERTY(double maxFrequency READ maxFrequency NOTIFY updated)

 public:
  explicit Cpu(QObject* parent = nullptr);
  Cpu(std::shared_ptr<CpuDataSource> source, QObject* parent);

  double percent() const { return sample_.percent; }
  QVariantList perCore() const;
  double frequency() const { return sample_.frequencyCurrent; }
  int cores() const { return sample_.coresLogical; }
  int coresPhysical() const { return sample_.coresPhysical; }
  double maxFrequency() const { return sample_.frequencyMax; }

 protected:
  void sample() override;

 private:
  std::shared_ptr<CpuDataSource> source_;
  CpuSample sample_;
};

class Battery : public kernel::StatSource {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool available READ available NOTIFY updated)
  Q_PROPERTY(int percent READ percent NOTIFY updated)
  Q_PROPERTY(bool charging READ charging NOTIFY updated)
  Q_PROPERTY(bool plugged READ plugged NOTIFY updated)
  Q_PROPERTY(int timeRemaining READ timeRemaining NOTIFY updated)

 public:
  explicit Battery(QObject* parent = nullptr);
  Battery(std::shared_ptr<BatteryDataSource> source, QObject* parent);

  bool available() const { return available_; }
  int percent() const { return sample_.percent; }
  bool charging() const { return sample_.charging; }
  bool plugged() const { return sample_.plugged; }
  int timeRemaining() const { return sample_.timeRemaining; }

 protected:
  void sample() override;

 private:
  std::shared_ptr<BatteryDataSource> source_;
  BatterySample sample_;
  bool available_ = false;
};

}  // namespace wabar::system_stats
