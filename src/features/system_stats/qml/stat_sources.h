#pragma once

#include <QtQml/qqmlregistration.h>

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "features/system_stats/system_stats.h"
#include "kernel/qml/stat_source.h"

namespace waku::system_stats {

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
  Q_PROPERTY(double temperature READ temperature NOTIFY updated)
  Q_PROPERTY(bool temperatureAvailable READ temperatureAvailable NOTIFY updated)

 public:
  explicit Cpu(QObject* parent = nullptr);
  Cpu(std::shared_ptr<CpuDataSource> source, QObject* parent);

  double percent() const { return sample_.percent; }
  QVariantList perCore() const;
  double frequency() const { return sample_.frequencyCurrent; }
  int cores() const { return sample_.coresLogical; }
  int coresPhysical() const { return sample_.coresPhysical; }
  double maxFrequency() const { return sample_.frequencyMax; }
  double temperature() const;
  bool temperatureAvailable() const { return sample_.temperature.has_value(); }

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

// GPU load and dedicated video memory from the PDH "GPU Engine" and "GPU Adapter Memory" counters.
// `usage` (also `percent` and `percentage`) is the busiest engine type over all adapters, 0..100;
// `memory` is the dedicated video memory in use and `memoryTotal` the dedicated memory of the
// hardware adapters, both in bytes (`memoryTotal` is 0 for an integrated GPU without dedicated
// memory). All stay 0 and `available` stays false when the counters are missing.
// `temperature` is Celsius, NaN while `temperatureAvailable` is false (the driver reports none).
class Gpu : public kernel::StatSource {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool available READ available NOTIFY updated)
  Q_PROPERTY(double usage READ percent NOTIFY updated)
  Q_PROPERTY(double percent READ percent NOTIFY updated)
  Q_PROPERTY(double percentage READ percent NOTIFY updated)
  Q_PROPERTY(qlonglong memory READ memory NOTIFY updated)
  Q_PROPERTY(qlonglong memoryUsed READ memory NOTIFY updated)
  Q_PROPERTY(qlonglong memoryTotal READ memoryTotal NOTIFY updated)
  Q_PROPERTY(double memoryPercent READ memoryPercent NOTIFY updated)
  Q_PROPERTY(QString name READ name NOTIFY updated)
  Q_PROPERTY(double temperature READ temperature NOTIFY updated)
  Q_PROPERTY(bool temperatureAvailable READ temperatureAvailable NOTIFY updated)

 public:
  explicit Gpu(QObject* parent = nullptr);
  Gpu(std::shared_ptr<GpuDataSource> source, QObject* parent);

  bool available() const { return sample_.available; }
  double percent() const { return sample_.percent; }
  qlonglong memory() const { return static_cast<qlonglong>(sample_.memoryUsed); }
  qlonglong memoryTotal() const { return static_cast<qlonglong>(sample_.memoryTotal); }
  double memoryPercent() const;
  QString name() const { return QString::fromStdString(sample_.name); }
  double temperature() const;
  bool temperatureAvailable() const { return sample_.temperature.has_value(); }

 protected:
  void sample() override;

 private:
  std::shared_ptr<GpuDataSource> source_;
  GpuSample sample_;
};

// The mounted volumes with a drive letter (fixed, removable and optical drives; network drives
// are left out so an unreachable share cannot stall the sample). `disks` is a list of maps with
// `mount` ("C:\\"), `name` (volume label), `fileSystem`, `total`, `free`, `used` (bytes), `perc`
// and `percent` (used share, 0..100), `hasRoot` and `system` (the drive holding Windows) and
// `removable`. `primaryDisk` is the system drive's map (empty when unknown).
class Disks : public kernel::StatSource {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QVariantList disks READ disks NOTIFY updated)
  Q_PROPERTY(QVariantMap primaryDisk READ primaryDisk NOTIFY updated)

 public:
  explicit Disks(QObject* parent = nullptr);
  Disks(std::shared_ptr<DiskDataSource> source, QObject* parent);

  QVariantList disks() const { return disks_; }
  QVariantMap primaryDisk() const { return primary_; }

 protected:
  void sample() override;

 private:
  std::shared_ptr<DiskDataSource> source_;
  QVariantList disks_;
  QVariantMap primary_;
};

}  // namespace waku::system_stats
