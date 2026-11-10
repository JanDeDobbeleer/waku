// The tray message parser: the `WM_COPYDATA` payload of a `Shell_NotifyIcon` call. Nothing here
// touches Explorer or the real tray.
#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>
#include <algorithm>

#include "features/tray/tray_pipe.h"
#include "features/tray/tray_types.h"

using namespace waku::tray;

namespace {

// --- wire format, laid out independently of the parser ---------------------------------------

#pragma pack(push, 1)
struct TestGuid {
  std::uint32_t data1;
  std::uint16_t data2;
  std::uint16_t data3;
  std::uint8_t data4[8];
};
struct Nid32 {
  std::uint32_t cbSize, hWnd, uID, uFlags, uCallbackMessage, hIcon;
  char16_t szTip[128];
  std::uint32_t dwState, dwStateMask;
  char16_t szInfo[256];
  std::uint32_t uVersion;
  char16_t szInfoTitle[64];
  std::uint32_t dwInfoFlags;
  TestGuid guidItem;
  std::uint32_t hBalloonIcon;
};
struct ShellTrayData {
  std::uint32_t signature, message;
  Nid32 nid;
};
struct CopyDataHeader {
  std::uint32_t type;
  std::uint64_t dwData;
  std::uint32_t cbData, iconWidth, iconHeight, iconDataSize;
};
#pragma pack(pop)
static_assert(sizeof(Nid32) == 956);
static_assert(sizeof(ShellTrayData) == kShellTrayDataSize);
static_assert(sizeof(CopyDataHeader) == kPipeHeaderSize);

void putText(char16_t* destination, int capacity, const QString& text) {
  const int count = std::min<int>(static_cast<int>(text.size()), capacity - 1);
  for (int i = 0; i < count; ++i) destination[i] = text.at(i).unicode();
}

ShellTrayData makeTray(std::uint32_t message, std::uint32_t flags, std::uint32_t hwnd = 0x1234,
                       std::uint32_t uid = 7) {
  ShellTrayData tray{};
  tray.signature = 0x34753423;
  tray.message = message;
  tray.nid.cbSize = sizeof(Nid32);
  tray.nid.hWnd = hwnd;
  tray.nid.uID = uid;
  tray.nid.uFlags = flags;
  return tray;
}

QByteArray pipeMessage(const ShellTrayData& tray, const QByteArray& rgba = {}, int width = 0,
                       int height = 0) {
  CopyDataHeader header{};
  header.type = 2;
  header.dwData = 1;
  header.cbData = sizeof(tray);
  header.iconWidth = static_cast<std::uint32_t>(width);
  header.iconHeight = static_cast<std::uint32_t>(height);
  header.iconDataSize = static_cast<std::uint32_t>(rgba.size());
  QByteArray bytes(reinterpret_cast<const char*>(&header), sizeof(header));
  bytes.append(reinterpret_cast<const char*>(&tray), sizeof(tray));
  bytes.append(rgba);
  return bytes;
}

QByteArray textMessage(const QByteArray& text) {
  const std::uint32_t type = 1;
  QByteArray bytes(reinterpret_cast<const char*>(&type), sizeof(type));
  bytes.append(text);
  return bytes;
}

// --- parser ----------------------------------------------------------------------------------

TEST(TrayPipeParser, AddWithEveryFieldIsValidated) {
  ShellTrayData tray = makeTray(kNimAdd, kNifMessage | kNifIcon | kNifTip | kNifState | kNifGuid);
  tray.nid.uCallbackMessage = 0x8001;
  tray.nid.hIcon = 0x55;
  putText(tray.nid.szTip, 128, QStringLiteral("Tip\rtext"));
  tray.nid.dwState = 1;
  tray.nid.dwStateMask = 1;
  tray.nid.uVersion = 4;
  tray.nid.guidItem = {
      0x7820ae75, 0x23e3, 0x4229, {0x82, 0xc1, 0xe4, 0x1c, 0xb6, 0x7d, 0x5b, 0x9c}};
  const QByteArray rgba(2 * 2 * 4, '\x7f');

  const auto parsed = parseTrayPipeMessage(pipeMessage(tray, rgba, 2, 2), [](std::uint64_t hwnd) {
    EXPECT_EQ(hwnd, 0x1234u);
    return QStringLiteral("C:\\Apps\\my.app.exe");
  });
  ASSERT_EQ(parsed.kind, ParsedTrayMessage::Kind::IconModified);
  const TrayIconData& data = parsed.data;
  EXPECT_EQ(data.messageType, kNimAdd);
  EXPECT_EQ(data.hWnd, 0x1234u);
  EXPECT_EQ(data.uID, 7u);
  EXPECT_EQ(data.uCallbackMessage, 0x8001u);
  EXPECT_EQ(data.hIcon, 0x55u);
  EXPECT_EQ(data.szTip, QStringLiteral("Tiptext"));  // array_to_str drops CR
  EXPECT_EQ(data.dwState, 1u);
  EXPECT_EQ(data.dwStateMask, 1u);
  EXPECT_EQ(data.uVersion, 4u);
  ASSERT_TRUE(data.guid.has_value());
  EXPECT_EQ(*data.guid, kTrayBatteryIconGuid);
  EXPECT_EQ(data.exePath, QStringLiteral("C:\\Apps\\my.app.exe"));
  EXPECT_EQ(data.exe, QStringLiteral("my"));  // Path(...).name.split(".")[0]
  EXPECT_EQ(data.iconWidth, 2);
  EXPECT_EQ(data.iconHeight, 2);
  EXPECT_EQ(data.iconRgba, rgba);
}

TEST(TrayPipeParser, ACallbackMessageBelowWmUserIsDropped) {
  // WM_CLOSE or WM_SYSCOMMAND as a "callback" would make a click on the icon close or command
  // another program's window; NOTIFYICONDATA callbacks are WM_USER-range messages.
  for (const std::uint32_t system : {0x10u, 0x112u, 0x111u, 0x3FFu}) {
    ShellTrayData tray = makeTray(kNimAdd, kNifMessage);
    tray.nid.uCallbackMessage = system;
    const auto parsed = parseTrayPipeMessage(pipeMessage(tray));
    ASSERT_EQ(parsed.kind, ParsedTrayMessage::Kind::IconModified);
    EXPECT_EQ(parsed.data.uCallbackMessage, 0u) << system;
  }
  for (const std::uint32_t user : {0x400u, 0x8001u, 0xC123u}) {
    ShellTrayData tray = makeTray(kNimAdd, kNifMessage);
    tray.nid.uCallbackMessage = user;
    const auto parsed = parseTrayPipeMessage(pipeMessage(tray));
    ASSERT_EQ(parsed.kind, ParsedTrayMessage::Kind::IconModified);
    EXPECT_EQ(parsed.data.uCallbackMessage, user);
  }
}

TEST(TrayPipeParser, FieldsWithoutTheirFlagStayEmpty) {
  ShellTrayData tray = makeTray(kNimModify, kNifTip);
  tray.nid.uCallbackMessage = 99;
  tray.nid.hIcon = 5;
  tray.nid.dwState = 1;
  putText(tray.nid.szTip, 128, QStringLiteral("only the tip"));
  const auto parsed = parseTrayPipeMessage(pipeMessage(tray));
  ASSERT_EQ(parsed.kind, ParsedTrayMessage::Kind::IconModified);
  EXPECT_EQ(parsed.data.szTip, QStringLiteral("only the tip"));
  EXPECT_EQ(parsed.data.uCallbackMessage, 0u);
  EXPECT_EQ(parsed.data.hIcon, 0u);
  EXPECT_EQ(parsed.data.dwState, 0u);
  EXPECT_FALSE(parsed.data.guid.has_value());
  EXPECT_TRUE(parsed.data.iconRgba.isEmpty());
}

TEST(TrayPipeParser, VersionOutsideOneToFourIsIgnored) {
  for (const std::uint32_t version : {0u, 5u, 99u}) {
    ShellTrayData tray = makeTray(kNimSetVersion, 0);
    tray.nid.uVersion = version;
    const auto parsed = parseTrayPipeMessage(pipeMessage(tray));
    ASSERT_EQ(parsed.kind, ParsedTrayMessage::Kind::IconModified);
    EXPECT_EQ(parsed.data.uVersion, 0u) << version;
  }
}

TEST(TrayPipeParser, InfoBalloonFields) {
  ShellTrayData tray = makeTray(kNimModify, kNifInfo);
  putText(tray.nid.szInfo, 256, QStringLiteral("balloon body"));
  putText(tray.nid.szInfoTitle, 64, QStringLiteral("balloon title"));
  tray.nid.dwInfoFlags = 3;
  const auto parsed = parseTrayPipeMessage(pipeMessage(tray));
  EXPECT_EQ(parsed.data.szInfo, QStringLiteral("balloon body"));
  EXPECT_EQ(parsed.data.szInfoTitle, QStringLiteral("balloon title"));
  EXPECT_EQ(parsed.data.dwInfoFlags, 3u);
}

TEST(TrayPipeParser, DeleteCarriesWindowIdAndGuidOnlyWithTheFlag) {
  ShellTrayData tray = makeTray(kNimDelete, kNifGuid);
  tray.nid.guidItem = {
      0x7820ae73, 0x23e3, 0x4229, {0x82, 0xc1, 0xe4, 0x1c, 0xb6, 0x7d, 0x5b, 0x9c}};
  auto parsed = parseTrayPipeMessage(pipeMessage(tray));
  ASSERT_EQ(parsed.kind, ParsedTrayMessage::Kind::IconDeleted);
  EXPECT_EQ(parsed.data.hWnd, 0x1234u);
  EXPECT_EQ(parsed.data.uID, 7u);
  ASSERT_TRUE(parsed.data.guid.has_value());
  EXPECT_EQ(*parsed.data.guid, kTrayVolumeIconGuid);

  tray.nid.uFlags = 0;
  parsed = parseTrayPipeMessage(pipeMessage(tray));
  ASSERT_EQ(parsed.kind, ParsedTrayMessage::Kind::IconDeleted);
  EXPECT_FALSE(parsed.data.guid.has_value());
}

TEST(TrayPipeParser, TextMessageAndIgnoredTypes) {
  auto parsed = parseTrayPipeMessage(textMessage("[DLL] Pipeline connected.\n"));
  EXPECT_EQ(parsed.kind, ParsedTrayMessage::Kind::Text);
  EXPECT_EQ(parsed.text, QStringLiteral("[DLL] Pipeline connected.\n"));

  const std::uint32_t unknown = 9;
  parsed = parseTrayPipeMessage(QByteArray(reinterpret_cast<const char*>(&unknown), 4));
  EXPECT_EQ(parsed.kind, ParsedTrayMessage::Kind::Ignored);

  // A shell message that is neither add, modify, set-version nor delete.
  parsed = parseTrayPipeMessage(pipeMessage(makeTray(3, 0)));
  EXPECT_EQ(parsed.kind, ParsedTrayMessage::Kind::Ignored);
}

TEST(TrayPipeParser, MalformedMessagesAreInvalidNotFatal) {
  EXPECT_EQ(parseTrayPipeMessage(QByteArray()).kind, ParsedTrayMessage::Kind::Invalid);
  EXPECT_EQ(parseTrayPipeMessage(QByteArray(3, 'x')).kind, ParsedTrayMessage::Kind::Invalid);

  // COPYDATA shorter than its header.
  const std::uint32_t type = 2;
  QByteArray shortCopy(reinterpret_cast<const char*>(&type), 4);
  shortCopy.append(QByteArray(10, '\0'));
  EXPECT_EQ(parseTrayPipeMessage(shortCopy).kind, ParsedTrayMessage::Kind::Invalid);

  // The payload is cut off before a whole SHELLTRAYDATA.
  QByteArray truncated = pipeMessage(makeTray(kNimAdd, 0));
  truncated.chop(200);
  EXPECT_EQ(parseTrayPipeMessage(truncated).kind, ParsedTrayMessage::Kind::Invalid);
}

TEST(TrayPipeParser, OnlyPythonExceptionsEndTheSession) {
  // Python returns silently from these two; the session goes on.
  EXPECT_FALSE(parseTrayPipeMessage(QByteArray(3, 'x')).fatal);
  const std::uint32_t type = 2;
  QByteArray shortCopy(reinterpret_cast<const char*>(&type), 4);
  shortCopy.append(QByteArray(10, '\0'));
  EXPECT_FALSE(parseTrayPipeMessage(shortCopy).fatal);

  // from_buffer_copy on a short payload raises.
  QByteArray truncated = pipeMessage(makeTray(kNimAdd, 0));
  truncated.chop(200);
  const auto cutPayload = parseTrayPipeMessage(truncated);
  EXPECT_EQ(cutPayload.kind, ParsedTrayMessage::Kind::Invalid);
  EXPECT_TRUE(cutPayload.fatal);

  // Image.frombytes on short pixel data raises.
  const auto shortPixels =
      parseTrayPipeMessage(pipeMessage(makeTray(kNimAdd, kNifIcon), QByteArray(10, 'x'), 2, 2));
  EXPECT_EQ(shortPixels.kind, ParsedTrayMessage::Kind::Invalid);
  EXPECT_TRUE(shortPixels.fatal);
}

TEST(TrayPipeParser, SurplusIconPixelsAreDropped) {
  const ShellTrayData tray = makeTray(kNimAdd, kNifIcon);
  const auto parsed = parseTrayPipeMessage(pipeMessage(tray, QByteArray(20, 'x'), 2, 2));
  ASSERT_EQ(parsed.kind, ParsedTrayMessage::Kind::IconModified);
  EXPECT_TRUE(parsed.data.iconRgba.isEmpty());
  EXPECT_EQ(parsed.data.hIcon, 0u);
}

TEST(TrayPipeParser, Helpers) {
  const char16_t units[] = {u'a', u'\r', u'b', 0, u'c'};
  EXPECT_EQ(trayArrayToString(units, 5), QStringLiteral("ab"));
  EXPECT_EQ(trayExeName(QStringLiteral("C:\\x\\Discord.exe")), QStringLiteral("Discord"));
  EXPECT_EQ(trayExeName(QStringLiteral("C:\\x\\noext")), QStringLiteral("noext"));
  EXPECT_EQ(trayExeName(QString()), QString());
  // pack_i32 keeps the low word and sign-extends the high one.
  EXPECT_EQ(packI32(0x201, 0), 0x201);
  EXPECT_EQ(packI32(1, 2), (2 << 16) | 1);
  EXPECT_EQ(packI32(0x12345, 1), (1 << 16) | 0x2345);
  EXPECT_EQ(packI32(5, -1), static_cast<std::int32_t>(0xFFFF0005u));
}

}  // namespace
