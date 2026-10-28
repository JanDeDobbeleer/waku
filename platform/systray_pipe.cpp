#include "platform/systray_pipe.h"

#include <QFileInfo>
#include <algorithm>
#include <cstring>

namespace yasb::platform {
namespace {

// Offsets inside SHELLTRAYDATA (packed): the layout hook/trayhook.cpp and Python YASB's
// NOTIFYICONDATA ctypes struct agree on.
constexpr int kOffMessage = 4;
constexpr int kOffHWnd = 12;
constexpr int kOffUId = 16;
constexpr int kOffFlags = 20;
constexpr int kOffCallback = 24;
constexpr int kOffIcon = 28;
constexpr int kOffTip = 32;
constexpr int kTipUnits = 128;
constexpr int kOffState = 288;
constexpr int kOffStateMask = 292;
constexpr int kOffInfo = 296;
constexpr int kInfoUnits = 256;
constexpr int kOffVersion = 808;
constexpr int kOffInfoTitle = 812;
constexpr int kInfoTitleUnits = 64;
constexpr int kOffInfoFlags = 940;
constexpr int kOffGuid = 944;

std::uint32_t u32(const char* base, int offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, base + offset, sizeof(value));
  return value;
}

QUuid guidAt(const char* base, int offset) {
  std::uint32_t data1 = 0;
  std::uint16_t data2 = 0;
  std::uint16_t data3 = 0;
  std::memcpy(&data1, base + offset, 4);
  std::memcpy(&data2, base + offset + 4, 2);
  std::memcpy(&data3, base + offset + 6, 2);
  const auto* d4 = reinterpret_cast<const unsigned char*>(base + offset + 8);
  return QUuid(data1, data2, data3, d4[0], d4[1], d4[2], d4[3], d4[4], d4[5], d4[6], d4[7]);
}

QString unitsAt(const char* base, int offset, int count) {
  char16_t units[256] = {};
  std::memcpy(units, base + offset, static_cast<size_t>(count) * sizeof(char16_t));
  return trayArrayToString(units, count);
}

}  // namespace

QString trayArrayToString(const char16_t* units, int count) {
  int length = 0;
  while (length < count && units[length] != 0) ++length;
  QString text = QString::fromUtf16(units, length);
  text.remove(QLatin1Char('\r'));
  return text;
}

QString trayExeName(const QString& exePath) {
  if (exePath.isEmpty()) return {};
  const QString name = QFileInfo(exePath).fileName();
  const qsizetype dot = name.indexOf(QLatin1Char('.'));
  return dot < 0 ? name : name.left(dot);
}

std::int32_t packI32(std::int32_t low, std::int32_t high) {
  const std::uint32_t lowPart = static_cast<std::uint32_t>(low) & 0xFFFFu;
  const std::uint32_t highPart = static_cast<std::uint32_t>(static_cast<std::int16_t>(high)) << 16;
  return static_cast<std::int32_t>(lowPart | highPart);
}

ParsedTrayMessage parseShellTrayData(const char* payload, std::int64_t size, const char* rgba,
                                     std::int64_t rgbaSize, int iconWidth, int iconHeight,
                                     const ExePathResolver& exePath) {
  ParsedTrayMessage result;
  if (!payload || size < kShellTrayDataSize) {
    result.invalidReason =
        QStringLiteral("tray data is %1 bytes, expected %2").arg(size).arg(kShellTrayDataSize);
    return result;
  }

  const std::uint32_t message = u32(payload, kOffMessage);
  const std::uint32_t flags = u32(payload, kOffFlags);
  TrayIconData& data = result.data;
  data.messageType = message;
  data.hWnd = u32(payload, kOffHWnd);
  data.uID = u32(payload, kOffUId);
  data.uFlags = flags;

  if (message == kNimDelete) {
    if (flags & kNifGuid) data.guid = guidAt(payload, kOffGuid);
    data.uFlags = 0;
    result.kind = ParsedTrayMessage::Kind::IconDeleted;
    return result;
  }
  if (message != kNimAdd && message != kNimModify && message != kNimSetVersion) {
    result.kind = ParsedTrayMessage::Kind::Ignored;
    return result;
  }

  if (exePath) {
    data.exePath = exePath(data.hWnd);
    data.exe = trayExeName(data.exePath);
  }
  const std::uint32_t version = u32(payload, kOffVersion);
  if (version > 0 && version <= 4) data.uVersion = version;
  if (flags & kNifMessage) data.uCallbackMessage = u32(payload, kOffCallback);
  if (flags & kNifIcon) {
    data.hIcon = u32(payload, kOffIcon);
    const std::int64_t expected = static_cast<std::int64_t>(iconWidth) * iconHeight * 4;
    if (rgba && iconWidth > 0 && iconHeight > 0 && rgbaSize == expected) {
      data.iconWidth = iconWidth;
      data.iconHeight = iconHeight;
      data.iconRgba = QByteArray(rgba, static_cast<qsizetype>(rgbaSize));
    }
  }
  if (flags & kNifTip) data.szTip = unitsAt(payload, kOffTip, kTipUnits);
  if (flags & kNifState) {
    data.dwState = u32(payload, kOffState);
    data.dwStateMask = u32(payload, kOffStateMask);
  }
  if (flags & kNifGuid) data.guid = guidAt(payload, kOffGuid);
  if (flags & kNifInfo) {
    data.dwInfoFlags = u32(payload, kOffInfoFlags);
    data.szInfoTitle = unitsAt(payload, kOffInfoTitle, kInfoTitleUnits);
    data.szInfo = unitsAt(payload, kOffInfo, kInfoUnits);
  }
  result.kind = ParsedTrayMessage::Kind::IconModified;
  return result;
}

ParsedTrayMessage parseTrayPipeMessage(const QByteArray& message, const ExePathResolver& exePath) {
  ParsedTrayMessage result;
  if (message.size() < 4) {
    result.invalidReason = QStringLiteral("message shorter than its type field");
    return result;
  }
  const char* bytes = message.constData();
  const std::uint32_t type = u32(bytes, 0);

  if (type == 1) {
    result.kind = ParsedTrayMessage::Kind::Text;
    result.text = QString::fromUtf8(bytes + 4, message.size() - 4);
    return result;
  }
  if (type != 2) {
    result.kind = ParsedTrayMessage::Kind::Ignored;
    return result;
  }
  if (message.size() < kPipeHeaderSize) {
    result.invalidReason = QStringLiteral("invalid COPYDATA message size: %1").arg(message.size());
    return result;
  }

  // type, dwData (u64), cbData, iconWidth, iconHeight, iconDataSize
  const std::uint32_t cbData = u32(bytes, 12);
  const std::uint32_t iconWidth = u32(bytes, 16);
  const std::uint32_t iconHeight = u32(bytes, 20);
  const std::uint32_t iconDataSize = u32(bytes, 24);

  const std::int64_t available = message.size() - kPipeHeaderSize;
  const std::int64_t payloadSize = std::min<std::int64_t>(cbData, available);
  const char* payload = bytes + kPipeHeaderSize;
  const char* rgba = nullptr;
  std::int64_t rgbaSize = 0;
  if (iconDataSize > 0 && available > static_cast<std::int64_t>(cbData)) {
    rgba = payload + cbData;
    rgbaSize = std::min<std::int64_t>(iconDataSize, available - cbData);
  }
  return parseShellTrayData(payload, payloadSize, rgba, rgbaSize, static_cast<int>(iconWidth),
                            static_cast<int>(iconHeight), exePath);
}

}  // namespace yasb::platform
