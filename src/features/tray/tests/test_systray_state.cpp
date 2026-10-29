#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUuid>
#include <memory>
#include <optional>

#include "features/tray/tray_service.h"
#include "features/tray/tray_state.h"
#include "features/tray/tray_types.h"

using namespace wabar::tray;

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
  state.set("C:\\Users\\jande\\caf\u00E9.exe", IconState{true, 0});
  state.set("7820ae75-23e3-4229-82c1-e41cb67d5b9c", IconState{false, 1});
  ASSERT_TRUE(store.save(state));
  QFile file(store.path());
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  // json.dumps(indent=2) in text mode on Windows: ASCII only (\u escapes), CRLF, no final newline.
  EXPECT_EQ(file.readAll(),
            "{\r\n"
            "  \"C:\\\\Users\\\\jande\\\\caf\\u00e9.exe\": {\r\n    \"is_pinned\": true,\r\n"
            "    \"index\": 0\r\n  },\r\n"
            "  \"7820ae75-23e3-4229-82c1-e41cb67d5b9c\": {\r\n    \"is_pinned\": false,\r\n"
            "    \"index\": 1\r\n  }\r\n"
            "}");
  const IconStateMap loaded = store.load();
  ASSERT_EQ(loaded.size(), 2u);
  EXPECT_EQ(loaded.entries().front().first, "C:\\Users\\jande\\caf\u00E9.exe");
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
}

// --- host selection (the hook or no host: there is no legacy window) ----------------------------

namespace {

struct CountingHost : public TrayHost {
  explicit CountingHost(QString name) : name_(std::move(name)) {}
  void start() override {}
  void stop() override {}
  QString mode() const override { return name_; }
  QString name_;
};

struct FactoryProbe {
  int hooksMade = 0;
  bool hookStarts = true;
  QString dll = "C:\\fake\\hook.dll";
  QString reason;
  TrayHostFactories factories() {
    TrayHostFactories f;
    f.findHookDll = [this](const QString&, QString* why) {
      if (why) *why = reason;
      return dll;
    };
    f.makeHook = [this](const QString&) -> std::unique_ptr<TrayHost> {
      ++hooksMade;
      if (!hookStarts) return nullptr;
      return std::make_unique<CountingHost>("hook");
    };
    return f;
  }
};

}  // namespace

TEST(SystrayHostChoice, HookWhenUsable) {
  FactoryProbe probe;
  auto host = createTrayHost(true, "app", probe.factories());
  ASSERT_NE(host, nullptr);
  EXPECT_EQ(host->mode(), "hook");
}

TEST(SystrayHostChoice, UseHookFalseNeverTouchesTheHook) {
  FactoryProbe probe;
  EXPECT_EQ(createTrayHost(false, "app", probe.factories()), nullptr);
  EXPECT_EQ(probe.hooksMade, 0);
}

TEST(SystrayHostChoice, MissingDllMeansNoHost) {
  FactoryProbe probe;
  probe.dll.clear();
  probe.reason = "YASBNativeTrayHook.dll not found next to the executable";
  EXPECT_EQ(createTrayHost(true, "app", probe.factories()), nullptr);
  EXPECT_EQ(probe.hooksMade, 0);
}

TEST(SystrayHostChoice, EmulationAndFailedHookMeanNoHost) {
  FactoryProbe emulated;
  emulated.dll.clear();
  emulated.reason = "running under emulation";
  EXPECT_EQ(createTrayHost(true, "app", emulated.factories()), nullptr);

  FactoryProbe broken;
  broken.hookStarts = false;
  EXPECT_EQ(createTrayHost(true, "app", broken.factories()), nullptr);
  EXPECT_EQ(broken.hooksMade, 1);
}

TEST(SystrayHostChoice, ServiceHandsTheRefreshSignalToOneWidget) {
  FactoryProbe probe;
  SystrayService service(true, "app", probe.factories());
  EXPECT_TRUE(service.claimRefreshSignal());
  EXPECT_FALSE(service.claimRefreshSignal());
  ASSERT_NE(service.windowApi(), nullptr);
  service.stop();
}
