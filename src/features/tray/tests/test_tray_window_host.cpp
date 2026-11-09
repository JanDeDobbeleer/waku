// The window host: a Shell_TrayWnd-style window in this process that receives Shell_NotifyIcon
// messages. Nothing here touches the real tray: the window has a class of its own, never claims
// the Z-order place and never passes anything on to Explorer, so no program finds it.
#include <windows.h>
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <atomic>

#include "features/tray/tray_pipe.h"
#include "features/tray/tray_service.h"
#include "features/tray/tray_types.h"
#include "features/tray/win32/tray_window_host.h"

using namespace waku::tray;

namespace {

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
#pragma pack(pop)
static_assert(sizeof(ShellTrayData) == kShellTrayDataSize);

ShellTrayData makeTray(std::uint32_t message, std::uint32_t flags, std::uint32_t uid,
                       const char16_t* tip = u"", std::uint32_t owner = 0x1234) {
  ShellTrayData tray{};
  tray.signature = 0x34753423;
  tray.message = message;
  tray.nid.cbSize = sizeof(Nid32);
  tray.nid.hWnd = owner;
  tray.nid.uID = uid;
  tray.nid.uFlags = flags;
  tray.nid.uCallbackMessage = 0x8001;
  for (int i = 0; tip[i] && i < 127; ++i) tray.nid.szTip[i] = tip[i];
  return tray;
}

// A unique class and mutex per test, so tests neither collide nor touch a real tray.
TrayWindowHost::Options testOptions() {
  static std::atomic<int> counter{0};
  const int n = ++counter;
  TrayWindowHost::Options options;
  options.windowClass =
      L"WakuTestTrayWnd" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(n);
  options.mutexName =
      QStringLiteral("Local\\WakuTestTrayWindowHost%1_%2").arg(GetCurrentProcessId()).arg(n);
  options.claimOrder = false;
  options.forwardToExplorer = false;
  return options;
}

LRESULT sendCopyData(std::uint64_t window, ULONG_PTR dwData, const void* bytes, DWORD size) {
  COPYDATASTRUCT data{};
  data.dwData = dwData;
  data.cbData = size;
  data.lpData = const_cast<void*>(bytes);
  return SendMessageW(reinterpret_cast<HWND>(window), WM_COPYDATA, 0,
                      reinterpret_cast<LPARAM>(&data));
}

class TrayWindowHostTest : public ::testing::Test {
 protected:
  void SetUp() override { qRegisterMetaType<TrayIconData>(); }
};

}  // namespace

TEST_F(TrayWindowHostTest, ReceivesAnIconAddedThroughTheWindow) {
  TrayWindowHost host(testOptions());
  ASSERT_TRUE(host.valid());
  QSignalSpy modified(&host, &TrayHost::iconModified);
  host.start();
  ASSERT_NE(host.windowHandle(), 0u);

  const ShellTrayData tray = makeTray(kNimAdd, kNifMessage | kNifTip, 7, u"Hello tray");
  EXPECT_TRUE(sendCopyData(host.windowHandle(), 1, &tray, sizeof(tray)));

  ASSERT_TRUE(QTest::qWaitFor([&] { return modified.count() == 1; }, 5000));
  const auto data = modified.takeFirst().at(0).value<TrayIconData>();
  EXPECT_EQ(data.hWnd, 0x1234u);
  EXPECT_EQ(data.uID, 7u);
  EXPECT_EQ(data.uCallbackMessage, 0x8001u);
  EXPECT_EQ(data.szTip, QStringLiteral("Hello tray"));
}

TEST_F(TrayWindowHostTest, ReceivesAnIconDeleted) {
  TrayWindowHost host(testOptions());
  QSignalSpy deleted(&host, &TrayHost::iconDeleted);
  host.start();
  ASSERT_NE(host.windowHandle(), 0u);

  const ShellTrayData tray = makeTray(kNimDelete, 0, 9);
  EXPECT_TRUE(sendCopyData(host.windowHandle(), 1, &tray, sizeof(tray)));

  ASSERT_TRUE(QTest::qWaitFor([&] { return deleted.count() == 1; }, 5000));
  const auto data = deleted.takeFirst().at(0).value<TrayIconData>();
  EXPECT_EQ(data.hWnd, 0x1234u);
  EXPECT_EQ(data.uID, 9u);
}

TEST_F(TrayWindowHostTest, ATooShortTrayMessageIsRefusedAndRaisesNothing) {
  TrayWindowHost host(testOptions());
  QSignalSpy modified(&host, &TrayHost::iconModified);
  host.start();
  ASSERT_NE(host.windowHandle(), 0u);

  const ShellTrayData tray = makeTray(kNimAdd, kNifTip, 1, u"x");
  EXPECT_FALSE(sendCopyData(host.windowHandle(), 1, &tray, sizeof(tray) - 1));
  EXPECT_FALSE(sendCopyData(host.windowHandle(), 1, nullptr, 0));
  QTest::qWait(100);
  EXPECT_EQ(modified.count(), 0);
}

TEST_F(TrayWindowHostTest, WhatIsNotATrayMessageIsNotAnIcon) {
  TrayWindowHost host(testOptions());
  QSignalSpy modified(&host, &TrayHost::iconModified);
  QSignalSpy deleted(&host, &TrayHost::iconDeleted);
  host.start();
  ASSERT_NE(host.windowHandle(), 0u);

  // The app bar protocol (dwData 0) and the icon rectangle query (3) belong to Explorer. With
  // nothing to pass them on to, the window answers 0 and raises no icon. Any other dwData is
  // refused.
  const ShellTrayData tray = makeTray(kNimAdd, kNifTip, 1, u"x");
  EXPECT_EQ(sendCopyData(host.windowHandle(), 0, &tray, sizeof(tray)), 0);
  EXPECT_EQ(sendCopyData(host.windowHandle(), 3, &tray, sizeof(tray)), 0);
  EXPECT_FALSE(sendCopyData(host.windowHandle(), 7, &tray, sizeof(tray)));
  // Commands and other messages are not passed on or acted on.
  HWND window = reinterpret_cast<HWND>(host.windowHandle());
  EXPECT_EQ(SendMessageW(window, WM_COMMAND, 419, 0), 0);
  EXPECT_EQ(SendMessageW(window, WM_USER + 1, 1, 2), 0);
  QTest::qWait(100);
  EXPECT_EQ(modified.count(), 0);
  EXPECT_EQ(deleted.count(), 0);
}

TEST_F(TrayWindowHostTest, AMessageOfTheSetVersionKindIsAcceptedWithoutAnIconSignal) {
  TrayWindowHost host(testOptions());
  QSignalSpy modified(&host, &TrayHost::iconModified);
  host.start();
  ASSERT_NE(host.windowHandle(), 0u);

  ShellTrayData tray = makeTray(kNimSetVersion, kNifMessage, 3);
  tray.nid.uVersion = 4;
  EXPECT_TRUE(sendCopyData(host.windowHandle(), 1, &tray, sizeof(tray)));
  // SETVERSION is reported as a modification carrying the version (the model stores it).
  ASSERT_TRUE(QTest::qWaitFor([&] { return modified.count() == 1; }, 5000));
  EXPECT_EQ(modified.takeFirst().at(0).value<TrayIconData>().uVersion, 4u);
}

TEST_F(TrayWindowHostTest, ASecondHostOfTheSessionIsNotValid) {
  const TrayWindowHost::Options options = testOptions();
  TrayWindowHost first(options);
  ASSERT_TRUE(first.valid());
  TrayWindowHost second(options);
  EXPECT_FALSE(second.valid());
  second.start();  // does nothing
  EXPECT_EQ(second.windowHandle(), 0u);
}

TEST_F(TrayWindowHostTest, StopEndsTheWorkerAndTheWindowAndIsSafeTwice) {
  TrayWindowHost host(testOptions());
  host.start();
  const std::uint64_t window = host.windowHandle();
  ASSERT_NE(window, 0u);
  EXPECT_TRUE(IsWindow(reinterpret_cast<HWND>(window)));
  host.stop();
  EXPECT_EQ(host.windowHandle(), 0u);
  EXPECT_FALSE(IsWindow(reinterpret_cast<HWND>(window)));
  host.stop();
  host.start();  // a stopped host starts again
  EXPECT_NE(host.windowHandle(), 0u);
}

// A real window to name as the owner of an icon, so that replay() finds it alive.
struct OwnerWindow {
  OwnerWindow()
      : hwnd(CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr)) {}
  ~OwnerWindow() {
    if (hwnd) DestroyWindow(hwnd);
  }
  std::uint32_t id() const {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(hwnd));
  }
  HWND hwnd;
};

TEST_F(TrayWindowHostTest, ReplayAnnouncesTheKnownIconsAndDropsGoneOnes) {
  TrayWindowHost host(testOptions());
  QSignalSpy modified(&host, &TrayHost::iconModified);
  host.start();
  ASSERT_NE(host.windowHandle(), 0u);
  OwnerWindow alive;
  ASSERT_TRUE(alive.hwnd);

  // Icon A: added, then its tip changed by a modification that carries no other field.
  const ShellTrayData addA = makeTray(kNimAdd, kNifMessage | kNifTip, 7, u"First", alive.id());
  const ShellTrayData modifyA = makeTray(kNimModify, kNifTip, 7, u"Renamed", alive.id());
  // Icon B: added, then deleted. Icon C: its owner window does not exist.
  const ShellTrayData addB = makeTray(kNimAdd, kNifMessage | kNifTip, 8, u"B", alive.id());
  const ShellTrayData deleteB = makeTray(kNimDelete, 0, 8, u"", alive.id());
  const ShellTrayData addC = makeTray(kNimAdd, kNifMessage | kNifTip, 9, u"C", 0x7FFF0);
  for (const ShellTrayData* tray : {&addA, &modifyA, &addB, &deleteB, &addC}) {
    EXPECT_TRUE(sendCopyData(host.windowHandle(), 1, tray, sizeof(*tray)));
  }
  ASSERT_TRUE(QTest::qWaitFor([&] { return modified.count() == 4; }, 5000));
  modified.clear();

  host.replay();  // direct on this thread: the signals are delivered at once
  ASSERT_EQ(modified.count(), 1);
  const auto data = modified.takeFirst().at(0).value<TrayIconData>();
  EXPECT_EQ(data.uID, 7u);
  EXPECT_EQ(data.szTip, QStringLiteral("Renamed"));  // the merged record, not the last message
  EXPECT_EQ(data.uCallbackMessage, 0x8001u);         // from the first message
  EXPECT_EQ(data.hWnd, alive.id());
  EXPECT_TRUE(data.uFlags & kNifTip);
  EXPECT_TRUE(data.uFlags & kNifMessage);

  // The icon of the missing window was dropped for good, not just skipped.
  modified.clear();
  host.replay();
  EXPECT_EQ(modified.count(), 1);
}

TEST_F(TrayWindowHostTest, ClaimingNothingLeavesTheHostIdle) {
  TrayWindowHost host(testOptions());
  host.start();
  EXPECT_FALSE(host.owning());  // claimOrder is off: it never takes the place
  EXPECT_EQ(host.mode(), QStringLiteral("window"));
}

TEST(TrayHostKind, TheHookIsTheDefaultAndTheFactoryBuildsTheWindowHost) {
  EXPECT_EQ(trayHostKind(), TrayHostKind::Hook);
  int built = 0;
  TrayHostFactories factories;
  factories.makeWindow = [&]() -> std::unique_ptr<TrayHost> {
    ++built;
    return std::make_unique<TrayWindowHost>(testOptions());
  };
  auto host = createTrayHost(TrayHostKind::Window, QString(), factories);
  ASSERT_TRUE(host);
  EXPECT_EQ(host->mode(), QStringLiteral("window"));
  EXPECT_EQ(built, 1);
  // The hook kind never builds the window host.
  EXPECT_FALSE(createTrayHost(TrayHostKind::Hook, QString(), factories));
  EXPECT_EQ(built, 1);
}

TEST(TrayHostKind, ANotValidWindowHostGivesNoHost) {
  TrayHostFactories factories;
  factories.makeWindow = []() -> std::unique_ptr<TrayHost> { return nullptr; };
  EXPECT_FALSE(createTrayHost(TrayHostKind::Window, QString(), factories));
  TrayHostFactories none;
  EXPECT_FALSE(createTrayHost(TrayHostKind::Window, QString(), none));
}
