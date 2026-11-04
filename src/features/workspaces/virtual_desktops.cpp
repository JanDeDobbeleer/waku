#include "features/workspaces/virtual_desktops.h"

#include <QUuid>
#include <cstring>

namespace waku::workspaces::vdesk {

QStringList parseDesktopIds(const QByteArray& bytes) {
  QStringList ids;
  for (qsizetype offset = 0; offset + 16 <= bytes.size(); offset += 16) {
    // The registry holds the GUID as the raw struct: Data1..Data3 little endian, then 8 bytes.
    quint32 data1 = 0;
    quint16 data2 = 0;
    quint16 data3 = 0;
    std::memcpy(&data1, bytes.constData() + offset, 4);
    std::memcpy(&data2, bytes.constData() + offset + 4, 2);
    std::memcpy(&data3, bytes.constData() + offset + 6, 2);
    uchar data4[8];
    std::memcpy(data4, bytes.constData() + offset + 8, 8);
    ids << QUuid(data1, data2, data3, data4[0], data4[1], data4[2], data4[3], data4[4], data4[5],
                 data4[6], data4[7])
               .toString(QUuid::WithBraces)
               .toUpper();
  }
  return ids;
}

int removalFallbackIndex(int count, int index) {
  if (count < 2 || index < 0 || index >= count) return -1;
  return index == 0 ? 1 : index - 1;
}

}  // namespace waku::workspaces::vdesk
