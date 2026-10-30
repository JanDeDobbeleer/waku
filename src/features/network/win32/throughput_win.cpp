// clang-format off
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
// clang-format on

#include <QString>
#include <memory>
#include <vector>

#include "features/network/throughput.h"
#include "kernel/qt/live_desktop.h"

namespace wabar::network {
namespace {

// Frees the table GetIfTable2 allocates.
struct IfTableDeleter {
  void operator()(MIB_IF_TABLE2* table) const {
    if (table) FreeMibTable(table);
  }
};

// GetIfTable2 with the 64-bit octet counters (InOctets / OutOctets), so a 32-bit wrap cannot
// happen. Loopback and filtered-out interface rows are skipped.
class IpHelperThroughputSource final : public ThroughputSource {
 public:
  std::vector<InterfaceCounters> read() override {
    MIB_IF_TABLE2* raw = nullptr;
    if (GetIfTable2(&raw) != NO_ERROR || !raw) return {};
    const std::unique_ptr<MIB_IF_TABLE2, IfTableDeleter> table(raw);
    std::vector<InterfaceCounters> counters;
    counters.reserve(table->NumEntries);
    for (ULONG i = 0; i < table->NumEntries; ++i) {
      const MIB_IF_ROW2& row = table->Table[i];
      if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
      InterfaceCounters entry;
      entry.id = row.InterfaceLuid.Value;
      entry.name = QString::fromWCharArray(row.Alias);
      entry.isUp = row.OperStatus == IfOperStatusUp;
      entry.rxBytes = row.InOctets;
      entry.txBytes = row.OutOctets;
      counters.push_back(std::move(entry));
    }
    return counters;
  }
};

}  // namespace

std::unique_ptr<ThroughputSource> createSystemThroughputSource() {
  if (!kernel::liveDesktop()) return nullptr;
  return std::make_unique<IpHelperThroughputSource>();
}

}  // namespace wabar::network
