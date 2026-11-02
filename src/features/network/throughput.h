#pragma once

#include <QString>
#include <QtGlobal>
#include <map>
#include <memory>
#include <vector>

namespace waku::network {

// One network interface as the IP helper reports it: the 64-bit octet counters since the adapter
// came up. `id` is stable while the adapter exists (the interface LUID).
struct InterfaceCounters {
  quint64 id = 0;
  QString name;
  bool isUp = false;
  quint64 rxBytes = 0;
  quint64 txBytes = 0;

  bool operator==(const InterfaceCounters&) const = default;
};

// Reads the counters of the non-loopback interfaces. Tests substitute a fake; the real source
// (createSystemThroughputSource) calls the IP helper API and must not be driven from tests.
class ThroughputSource {
 public:
  virtual ~ThroughputSource() = default;
  // One blocking read; an empty list when the query fails. GUI thread.
  virtual std::vector<InterfaceCounters> read() = 0;
};

// The real source; null off the live desktop.
std::unique_ptr<ThroughputSource> createSystemThroughputSource();

struct AdapterRate {
  QString name;
  bool isUp = false;
  double rxBytesPerSecond = 0;
  double txBytesPerSecond = 0;
};

// Turns successive counter reads into rates and running totals. Pure logic, no clock: the caller
// passes the seconds between two reads.
//
// Rules, per interface (keyed by id):
//  - the first read of an interface sets its baseline and counts nothing;
//  - a counter that went backwards (adapter reset) counts what it shows now;
//  - an interface that disappears is forgotten, and its past bytes stay in the totals;
//  - the aggregate rates and totals sum the interfaces that are up now; the adapter list holds
//    every interface, with its own rate.
// Totals are bytes counted since the tracker was built; `resetBaseline` keeps them.
class ThroughputTracker {
 public:
  // Feeds one read. `elapsedSeconds` <= 0 only sets the baseline.
  void update(const std::vector<InterfaceCounters>& counters, double elapsedSeconds);
  // Forgets the previous read (sampling paused), so the pause is not counted as one long interval.
  void resetBaseline();
  // Zeroes the rates (not the totals), for when sampling stops.
  void clearRates();

  double rxBytesPerSecond() const { return rx_; }
  double txBytesPerSecond() const { return tx_; }
  double rxBytesTotal() const { return static_cast<double>(rxTotal_); }
  double txBytesTotal() const { return static_cast<double>(txTotal_); }
  const std::vector<AdapterRate>& adapters() const { return adapters_; }

 private:
  struct Previous {
    quint64 rx = 0;
    quint64 tx = 0;
  };
  std::map<quint64, Previous> previous_;
  std::vector<AdapterRate> adapters_;
  double rx_ = 0;
  double tx_ = 0;
  quint64 rxTotal_ = 0;
  quint64 txTotal_ = 0;
};

}  // namespace waku::network
