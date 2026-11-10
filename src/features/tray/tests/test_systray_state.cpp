#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUuid>
#include <memory>
#include <optional>

#include "features/tray/tray_state.h"
#include "features/tray/tray_types.h"

using namespace waku::tray;

namespace {

TrayIconData makeIcon(std::uint64_t hwnd, std::uint32_t uid, const QString& exe,
                      std::optional<QUuid> guid = std::nullopt, const QString& tip = QString()) {
  TrayIconData data;
  data.messageType = kNimAdd;
  data.hWnd = hwnd;
  data.uID = uid;
  data.uFlags = kNifMessage | kNifIcon | kNifTip | (guid ? kNifGuid : 0u);
  data.uCallbackMessage = 0x8000;
  data.hIcon = 1;
  data.szTip = tip.isEmpty() ? exe + QStringLiteral(" tip") : tip;
  data.guid = guid;
  data.exe = exe;
  data.exePath = QStringLiteral("C:\\Apps\\") + exe + QStringLiteral(".exe");
  data.iconWidth = 16;
  data.iconHeight = 16;
  data.iconRgba = QByteArray(16 * 16 * 4, '\x40');
  return data;
}

}  // namespace

TEST(SystrayStateStore, WritesPythonJsonDumpsFormat) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  SystrayStateStore store(dir.path(), "S1");
  IconStateMap state;
  state.set("C:\\Users\\me\\caf\u00E9.exe", IconState{true, 0});
  state.set("7820ae75-23e3-4229-82c1-e41cb67d5b9c", IconState{false, 1});
  ASSERT_TRUE(store.save(state));
  QFile file(store.path());
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  // json.dumps(indent=2) in text mode on Windows: ASCII only (\u escapes), CRLF, no final newline.
  EXPECT_EQ(file.readAll(),
            "{\r\n"
            "  \"C:\\\\Users\\\\me\\\\caf\\u00e9.exe\": {\r\n    \"is_pinned\": true,\r\n"
            "    \"index\": 0\r\n  },\r\n"
            "  \"7820ae75-23e3-4229-82c1-e41cb67d5b9c\": {\r\n    \"is_pinned\": false,\r\n"
            "    \"index\": 1\r\n  }\r\n"
            "}");
  const IconStateMap loaded = store.load();
  ASSERT_EQ(loaded.size(), 2u);
  EXPECT_EQ(loaded.entries().front().first, "C:\\Users\\me\\caf\u00E9.exe");
  EXPECT_TRUE(loaded.entries().front().second.isPinned);
}

TEST(SystrayStateStore, LoadSkipsMalformedEntriesAndReadsLooseTypes) {
  QTemporaryDir dir;
  SystrayStateStore store(dir.path(), "S2");
  QFile file(store.path());
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"({"a": {"is_pinned": 1, "index": "3"}, "b": {"is_pinned": false}, "c": 5, )"
             R"("d": {"is_pinned": 0, "index": 2}})");
  file.close();
  const IconStateMap loaded = store.load();
  ASSERT_EQ(loaded.size(), 2u);
  EXPECT_EQ(loaded.find("a")->index, 3);
  EXPECT_TRUE(loaded.find("a")->isPinned);
  EXPECT_FALSE(loaded.find("d")->isPinned);
}

// update_current_state, worked by hand from the Python source.
TEST(SystrayUpdateState, OnScreenKeysTakeTheSavedSlots) {
  IconStateMap state;
  state.set("a", {true, 0});
  state.set("b", {true, 1});
  state.set("c", {false, 0});
  // b and a swapped on screen; d is new; c vanished from the layouts.
  updateCurrentState(state, {"b", "a"}, {"d"});
  EXPECT_EQ(*state.find("b"), (IconState{true, 0}));
  EXPECT_EQ(*state.find("a"), (IconState{true, 1}));
  // c is unpinned and not on screen: it keeps its slot, the new d goes behind it.
  EXPECT_EQ(*state.find("c"), (IconState{false, 0}));
  EXPECT_EQ(*state.find("d"), (IconState{false, 1}));
}

TEST(SystrayUpdateState, AMovedIconLeavesItsOldContainer) {
  IconStateMap state;
  state.set("a", {false, 0});
  state.set("b", {false, 1});
  updateCurrentState(state, {"a"}, {"b"});
  EXPECT_EQ(*state.find("a"), (IconState{true, 0}));
  EXPECT_EQ(*state.find("b"), (IconState{false, 0}));
}

TEST(SystrayUpdateState, EmptyLayoutsOnlyRenumber) {
  IconStateMap state;
  state.set("x", {false, 7});
  state.set("y", {false, 3});
  state.set("z", {true, 9});
  updateCurrentState(state, {}, {});
  EXPECT_EQ(*state.find("y"), (IconState{false, 0}));
  EXPECT_EQ(*state.find("x"), (IconState{false, 1}));
  EXPECT_EQ(*state.find("z"), (IconState{true, 0}));
}

TEST(SystrayUpdateState, UpdateIconDataHonoursFlags) {
  TrayIconData old = makeIcon(0x10, 1, "App", std::nullopt, "old tip");
  old.uVersion = 3;
  TrayIconData incoming;
  incoming.messageType = kNimModify;
  incoming.uFlags = kNifTip | kNifInfo;
  incoming.szTip = "new tip";
  incoming.szInfo = "body";
  incoming.szInfoTitle = "title";
  incoming.dwInfoFlags = 2;
  incoming.uVersion = 9;             // out of range: ignored
  incoming.uCallbackMessage = 1234;  // no NIF_MESSAGE: ignored
  incoming.exe = "App";
  incoming.exePath = old.exePath;
  updateIconData(old, incoming);
  EXPECT_EQ(old.szTip, "new tip");
  EXPECT_EQ(old.szInfo, "body");
  EXPECT_EQ(old.dwInfoFlags, 2u);
  EXPECT_EQ(old.uVersion, 3u);
  EXPECT_EQ(old.uCallbackMessage, 0x8000u);
  EXPECT_EQ(old.hWnd, 0x10u);  // incoming window 0 keeps the old one
  EXPECT_EQ(old.uID, 1u);
  incoming.uVersion = 4;
  updateIconData(old, incoming);
  EXPECT_EQ(old.uVersion, 4u);
  // A tooltip update carries no pixels and keeps the icon.
  EXPECT_EQ(old.iconRgba, QByteArray(16 * 16 * 4, '\x40'));
  EXPECT_EQ(old.iconWidth, 16);
}

TEST(TrayIconData, MessagesWithoutNifIconKeepThePixels) {
  TrayIconData old = makeIcon(0x10, 1, "Teams");
  TrayIconData setVersion;
  setVersion.messageType = kNimSetVersion;
  setVersion.hWnd = 0x10;
  setVersion.uID = 1;
  setVersion.uVersion = 4;
  updateIconData(old, setVersion);
  EXPECT_EQ(old.iconRgba, QByteArray(16 * 16 * 4, '\x40'));
  EXPECT_EQ(old.iconWidth, 16);
  EXPECT_EQ(old.iconHeight, 16);
}

TEST(TrayIconData, UnreadableIconKeepsTheLastGoodPixels) {
  TrayIconData old = makeIcon(0x10, 1, "WhatsApp");
  TrayIconData incoming;
  incoming.messageType = kNimModify;
  incoming.uFlags = kNifIcon;
  incoming.hIcon = 0x5678;  // the handle was there but could not be read
  updateIconData(old, incoming);
  EXPECT_EQ(old.iconRgba, QByteArray(16 * 16 * 4, '\x40'));
  EXPECT_EQ(old.hIcon, 0x5678u);

  incoming.iconWidth = 32;
  incoming.iconHeight = 32;
  incoming.iconRgba = QByteArray(32 * 32 * 4, '\x7f');
  updateIconData(old, incoming);
  EXPECT_EQ(old.iconWidth, 32);
  EXPECT_EQ(old.iconRgba, incoming.iconRgba);
}

TEST(TrayIconData, NifIconWithoutAHandleClearsThePixels) {
  TrayIconData old = makeIcon(0x10, 1, "App");
  TrayIconData incoming;
  incoming.messageType = kNimModify;
  incoming.uFlags = kNifIcon;
  incoming.hIcon = 0;
  updateIconData(old, incoming);
  EXPECT_TRUE(old.iconRgba.isEmpty());
  EXPECT_EQ(old.iconWidth, 0);
}

TEST(TrayIconData, TheHostsScaledPixelsTravelWithThePixels) {
  TrayIconData old = makeIcon(0x10, 1, "App");
  old.scaledRgba = QByteArray(32 * 32 * 4, '\x11');
  TrayIconData tip;  // no NIF_ICON: both stay
  tip.messageType = kNimModify;
  tip.uFlags = kNifTip;
  tip.szTip = QStringLiteral("busy");
  updateIconData(old, tip);
  EXPECT_EQ(old.scaledRgba, QByteArray(32 * 32 * 4, '\x11'));
  TrayIconData unreadable;  // NIF_ICON without readable pixels: both stay
  unreadable.uFlags = kNifIcon;
  unreadable.hIcon = 0x5678;
  updateIconData(old, unreadable);
  EXPECT_EQ(old.scaledRgba, QByteArray(32 * 32 * 4, '\x11'));
  TrayIconData fresh;  // new pixels: their own scaled bytes (none for a 32 px icon)
  fresh.uFlags = kNifIcon;
  fresh.hIcon = 0x5679;
  fresh.iconWidth = fresh.iconHeight = 32;
  fresh.iconRgba = QByteArray(32 * 32 * 4, '\x22');
  updateIconData(old, fresh);
  EXPECT_TRUE(old.scaledRgba.isEmpty());
}
