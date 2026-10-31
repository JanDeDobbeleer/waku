// Tray hook pipe protocol and the SystrayHook session loop. Ports the cases of
// yasb:tests/widgets/test_systray_hook.py. Nothing here touches Explorer or the real tray: the
// "DLL" is a test client writing to a private named pipe, and injection is faked.
#include <windows.h>
#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "features/tray/tray_pipe.h"
#include "features/tray/tray_types.h"
#include "features/tray/win32/tray_hook.h"
#include "features/tray/win32/tray_win.h"

using namespace wabar::tray;
using namespace std::chrono_literals;

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

// --- SystrayHook session ---------------------------------------------------------------------

DWORD handleCount() {
  DWORD count = 0;
  GetProcessHandleCount(GetCurrentProcess(), &count);
  return count;
}

QString uniqueSuffix() { return QUuid::createUuid().toString(QUuid::Id128); }

class FakeInjector : public TrayInjector {
 public:
  std::optional<std::uint32_t> explorerPid() override {
    ++pidCalls;
    return pid;
  }
  bool isHookLoaded(std::uint32_t) override { return loaded; }
  std::uintptr_t inject(std::uint32_t) override {
    if (blockInject) blockInject();
    std::lock_guard lock(mutex);
    if (!hookToReturn) return 0;
    injected.push_back(hookToReturn);
    return hookToReturn;
  }
  void release(std::uintptr_t hook) override {
    std::lock_guard lock(mutex);
    released.push_back(hook);
    cv.notify_all();
  }
  void setStopCheck(std::function<bool()> stopping) override { stopCheck = std::move(stopping); }
  bool waitReleased(std::size_t count) {
    std::unique_lock lock(mutex);
    return cv.wait_for(lock, 5s, [&] { return released.size() >= count; });
  }

  std::optional<std::uint32_t> pid = 1;
  bool loaded = true;
  std::uintptr_t hookToReturn = 0;
  std::function<void()> blockInject;
  std::function<bool()> stopCheck;
  std::atomic<int> pidCalls{0};
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::uintptr_t> injected, released;
};

class RecordingHook : public SystrayHook {
 public:
  using SystrayHook::SystrayHook;
  ~RecordingHook() override { stop(); }

  bool waitFor(std::size_t count) {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, 5s, [&] { return messages_.size() >= count; });
  }
  QByteArray message(std::size_t index) {
    std::lock_guard lock(mutex_);
    return messages_.at(index);
  }

 protected:
  bool handleMessage(const QByteArray& message) override {
    std::lock_guard lock(mutex_);
    messages_.push_back(message);
    cv_.notify_all();
    return true;
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<QByteArray> messages_;
};

class HookSession : public testing::Test {
 protected:
  SystrayHook::Options options(FakeInjector* injector = nullptr) {
    SystrayHook::Options result;
    result.pipeName = QStringLiteral("\\\\.\\pipe\\wabar-test-") + uniqueSuffix();
    result.mutexName = QStringLiteral("Local\\WabarTrayHookTest-") + uniqueSuffix();
    if (injector) {
      injectorPtr_ = injector;
      result.injector = std::unique_ptr<TrayInjector>(new Forward(injector));
    }
    result.afterSessionDelay = 50ms;
    pipeName_ = result.pipeName;
    return result;
  }

  // A TrayInjector that forwards to a fixture-owned FakeInjector, so tests can inspect it after
  // the hook (which owns its injector) is gone.
  class Forward : public TrayInjector {
   public:
    explicit Forward(FakeInjector* target) : target_(target) {}
    std::optional<std::uint32_t> explorerPid() override { return target_->explorerPid(); }
    bool isHookLoaded(std::uint32_t pid) override { return target_->isHookLoaded(pid); }
    std::uintptr_t inject(std::uint32_t pid) override { return target_->inject(pid); }
    void release(std::uintptr_t hook) override { target_->release(hook); }
    void setStopCheck(std::function<bool()> stopping) override {
      target_->setStopCheck(std::move(stopping));
    }

   private:
    FakeInjector* target_;
  };

  // Opens the "DLL" side of the pipe, retrying while the server instance is busy.
  HANDLE openClient(std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
      HANDLE client = CreateFileW(reinterpret_cast<LPCWSTR>(pipeName_.utf16()), GENERIC_WRITE, 0,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (client != INVALID_HANDLE_VALUE) return client;
      if (std::chrono::steady_clock::now() > deadline) return INVALID_HANDLE_VALUE;
      WaitNamedPipeW(reinterpret_cast<LPCWSTR>(pipeName_.utf16()), 100);
      std::this_thread::sleep_for(10ms);
    }
  }

  static bool writeAll(HANDLE client, const QByteArray& bytes) {
    DWORD written = 0;
    return WriteFile(client, bytes.constData(), static_cast<DWORD>(bytes.size()), &written,
                     nullptr) &&
           written == static_cast<DWORD>(bytes.size());
  }

  void SetUp() override {
    // Warm up lazily created process handles so the leak checks compare like with like.
    {
      RecordingHook warmup(options());
    }
    baseline_ = handleCount();
  }

  FakeInjector* injectorPtr_ = nullptr;
  QString pipeName_;
  DWORD baseline_ = 0;
};

class HookSessionSizes : public HookSession, public testing::WithParamInterface<int> {};

TEST_P(HookSessionSizes, ReadsCompleteBinaryMessages) {
  FakeInjector injector;
  RecordingHook hook(options(&injector));
  ASSERT_TRUE(hook.valid());
  // Connecting before run() also exercises ERROR_PIPE_CONNECTED.
  HANDLE client = openClient();
  ASSERT_NE(client, INVALID_HANDLE_VALUE);
  hook.start();

  QByteArray payload(GetParam(), '\0');
  for (int i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(i % 256);
  ASSERT_TRUE(writeAll(client, payload));
  ASSERT_TRUE(hook.waitFor(1));
  EXPECT_EQ(hook.message(0), payload);
  ASSERT_TRUE(writeAll(client, QByteArray("next message")));
  ASSERT_TRUE(hook.waitFor(2));
  EXPECT_EQ(hook.message(1), QByteArray("next message"));

  hook.stop();
  CloseHandle(client);
}

INSTANTIATE_TEST_SUITE_P(Sizes, HookSessionSizes,
                         testing::Values(17, kHookPipeBufferSize, 2 * kHookPipeBufferSize + 17));

TEST_F(HookSession, ParsedIconMessagesBecomeSignals) {
  FakeInjector injector;
  SystrayHook hook(options(&injector));
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<TrayIconData> modified, deleted;
  int updates = 0;
  QObject::connect(
      &hook, &TrayHost::iconModified, &hook,
      [&](const TrayIconData& data) {
        std::lock_guard lock(mutex);
        modified.push_back(data);
        cv.notify_all();
      },
      Qt::DirectConnection);
  QObject::connect(
      &hook, &TrayHost::iconDeleted, &hook,
      [&](const TrayIconData& data) {
        std::lock_guard lock(mutex);
        deleted.push_back(data);
        cv.notify_all();
      },
      Qt::DirectConnection);
  QObject::connect(
      &hook, &TrayHost::updateIcons, &hook,
      [&] {
        std::lock_guard lock(mutex);
        ++updates;
        cv.notify_all();
      },
      Qt::DirectConnection);

  HANDLE client = openClient();
  ASSERT_NE(client, INVALID_HANDLE_VALUE);
  hook.start();

  ShellTrayData add = makeTray(kNimAdd, kNifTip);
  putText(add.nid.szTip, 128, QStringLiteral("hello"));
  ASSERT_TRUE(writeAll(client, textMessage("[DLL] hi")));
  ASSERT_TRUE(writeAll(client, pipeMessage(add)));
  ASSERT_TRUE(writeAll(client, pipeMessage(makeTray(kNimDelete, 0))));
  {
    std::unique_lock lock(mutex);
    ASSERT_TRUE(cv.wait_for(lock, 5s, [&] { return !modified.empty() && !deleted.empty(); }));
    EXPECT_EQ(updates, 1);  // asked once, when the DLL connected
    EXPECT_EQ(modified.front().szTip, QStringLiteral("hello"));
    EXPECT_EQ(deleted.front().hWnd, 0x1234u);
  }
  hook.stop();
  CloseHandle(client);
}

class HookSessionPhase : public HookSession, public testing::WithParamInterface<bool> {};

TEST_P(HookSessionPhase, ShutdownDrainsPendingIoAndClosesHandles) {
  const bool connected = GetParam();
  {
    FakeInjector injector;
    RecordingHook hook(options(&injector));
    ASSERT_TRUE(hook.valid());
    // A connected client with no data makes the worker wait on a pending read instead of a connect.
    HANDLE client = connected ? openClient() : INVALID_HANDLE_VALUE;
    hook.start();
    std::this_thread::sleep_for(200ms);

    const auto begin = std::chrono::steady_clock::now();
    hook.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 2s);
    if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
  }
  // Pipe, watchdog mutex, completion event and the worker thread are all released.
  EXPECT_LE(handleCount(), baseline_);
}

INSTANTIATE_TEST_SUITE_P(Phases, HookSessionPhase, testing::Values(false, true));

TEST_F(HookSession, ReconnectsAfterTheClientDisconnects) {
  FakeInjector injector;
  RecordingHook hook(options(&injector));
  HANDLE client = openClient();
  ASSERT_NE(client, INVALID_HANDLE_VALUE);
  hook.start();
  ASSERT_TRUE(writeAll(client, QByteArray("first connection")));
  ASSERT_TRUE(hook.waitFor(1));
  EXPECT_EQ(hook.message(0), QByteArray("first connection"));
  CloseHandle(client);

  client = openClient();
  ASSERT_NE(client, INVALID_HANDLE_VALUE);
  ASSERT_TRUE(writeAll(client, QByteArray("second connection")));
  ASSERT_TRUE(hook.waitFor(2));
  EXPECT_EQ(hook.message(1), QByteArray("second connection"));
  hook.stop();
  CloseHandle(client);
}

TEST_F(HookSession, MutexCreationFailureLeavesNoHandles) {
  const DWORD before = handleCount();
  {
    SystrayHook::Options bad = options();
    bad.mutexName = QStringLiteral("Local\\no\\such\\namespace");  // backslashes are not allowed
    SystrayHook hook(std::move(bad));
    EXPECT_FALSE(hook.valid());
    hook.start();  // nothing to run
    hook.run();    // returns at once
    hook.stop();
  }
  EXPECT_LE(handleCount(), before);
}

TEST_F(HookSession, PipeCreationFailureClosesTheWatchdogMutex) {
  const DWORD before = handleCount();
  {
    SystrayHook::Options bad = options();
    bad.pipeName = QStringLiteral("not-a-pipe-name");
    SystrayHook hook(std::move(bad));
    EXPECT_FALSE(hook.valid());
    hook.run();
    hook.stop();
  }
  EXPECT_LE(handleCount(), before);
}

TEST_F(HookSession, RunCannotRestartOnceStopped) {
  RecordingHook hook(options());
  hook.stop();
  EXPECT_FALSE(hook.valid());
  hook.start();
  hook.run();
}

struct RetryCase {
  const char* name;
  std::chrono::milliseconds expected;
};

class HookRetry : public HookSession, public testing::WithParamInterface<RetryCase> {};

TEST_P(HookRetry, ShutdownInterruptsTheRetryDelay) {
  const RetryCase retry = GetParam();
  FakeInjector injector;
  SystrayHook::Options opts = options(&injector);
  opts.afterSessionDelay = kAfterSessionDelay;  // the real delay: stop() must cut it short
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::chrono::milliseconds> delays;
  opts.retryObserver = [&](std::chrono::milliseconds delay) {
    std::lock_guard lock(mutex);
    delays.push_back(delay);
    cv.notify_all();
  };
  HANDLE client = INVALID_HANDLE_VALUE;
  if (std::string(retry.name) == "explorer") {
    injector.pid.reset();
  } else if (std::string(retry.name) == "injection") {
    injector.loaded = false;  // injection fails: hookToReturn stays 0
  } else {
    // The DLL connects and drops again: the session ends and the worker pauses before retrying.
  }
  SystrayHook hook(std::move(opts));
  if (std::string(retry.name) == "disconnect") {
    client = openClient();
    ASSERT_NE(client, INVALID_HANDLE_VALUE);
  }
  hook.start();
  if (client != INVALID_HANDLE_VALUE) CloseHandle(client);

  {
    std::unique_lock lock(mutex);
    ASSERT_TRUE(cv.wait_for(lock, 5s, [&] { return !delays.empty(); }));
  }
  const auto begin = std::chrono::steady_clock::now();
  hook.stop();
  EXPECT_LT(std::chrono::steady_clock::now() - begin, 500ms);
  std::lock_guard lock(mutex);
  ASSERT_EQ(delays.size(), 1u);
  EXPECT_EQ(delays.front(), retry.expected);
}

INSTANTIATE_TEST_SUITE_P(Phases, HookRetry,
                         testing::Values(RetryCase{"explorer", kNoExplorerDelay},
                                         RetryCase{"injection", kInjectionFailedDelay},
                                         RetryCase{"disconnect", kAfterSessionDelay}),
                         [](const testing::TestParamInfo<RetryCase>& info) {
                           return std::string(info.param.name);
                         });

TEST_F(HookSession, ReleasesTheInjectionHookOnceTheDllConnects) {
  FakeInjector injector;
  injector.loaded = false;
  injector.hookToReturn = 123;
  RecordingHook hook(options(&injector));
  hook.start();
  HANDLE client = openClient();
  ASSERT_NE(client, INVALID_HANDLE_VALUE);
  ASSERT_TRUE(writeAll(client, QByteArray("ping")));
  ASSERT_TRUE(hook.waitFor(1));
  ASSERT_TRUE(injector.waitReleased(1));
  hook.stop();
  CloseHandle(client);
  std::lock_guard lock(injector.mutex);
  EXPECT_EQ(injector.injected, std::vector<std::uintptr_t>{123});
  EXPECT_EQ(injector.released, std::vector<std::uintptr_t>{123});
}

TEST_F(HookSession, StopReleasesAHookThatNeverConnected) {
  FakeInjector injector;
  injector.loaded = false;
  injector.hookToReturn = 77;
  RecordingHook hook(options(&injector));
  hook.start();
  std::this_thread::sleep_for(200ms);
  hook.stop();
  ASSERT_TRUE(injector.waitReleased(1));
  std::lock_guard lock(injector.mutex);
  EXPECT_EQ(injector.released.front(), 77u);
}

TEST_F(HookSession, ShutdownDuringInjectionReleasesTheNewHook) {
  FakeInjector injector;
  injector.loaded = false;
  injector.hookToReturn = 123;
  std::mutex mutex;
  std::condition_variable cv;
  bool injecting = false;
  bool finish = false;
  injector.blockInject = [&] {
    std::unique_lock lock(mutex);
    injecting = true;
    cv.notify_all();
    cv.wait_for(lock, 5s, [&] { return finish; });
  };
  RecordingHook hook(options(&injector));
  // Count pipe connections the worker would still make after the shutdown.
  hook.start();
  {
    std::unique_lock lock(mutex);
    ASSERT_TRUE(cv.wait_for(lock, 5s, [&] { return injecting; }));
  }
  std::thread stopper([&] { hook.stop(); });
  std::this_thread::sleep_for(100ms);
  {
    std::lock_guard lock(mutex);
    finish = true;
    cv.notify_all();
  }
  stopper.join();
  ASSERT_TRUE(injector.waitReleased(1));
  std::lock_guard lock(injector.mutex);
  EXPECT_EQ(injector.released, std::vector<std::uintptr_t>{123});
}

TEST(HookDll, MissingDllIsReportedNotThrown) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  QString reason;
  EXPECT_TRUE(findTrayHookDll(dir.path(), &reason).isEmpty());
  EXPECT_FALSE(reason.isEmpty());

  // With a file of the right name present the path is found (unless emulation rules it out).
  const QString name = trayHookDllName();
  if (!name.isEmpty()) {
    QFile file(QDir(dir.path()).filePath(name));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();
    reason.clear();
    const QString found = findTrayHookDll(dir.path(), &reason);
    if (found.isEmpty())
      EXPECT_FALSE(reason.isEmpty());
    else
      EXPECT_TRUE(found.endsWith(name));
  }
}

}  // namespace

// The real injector waits up to 3.5 s for Explorer's tray window while Explorer restarts; it
// polls the host's stop check, so stop() (a reload, the exit) does not wait that out.
TEST_F(HookSession, StopEndsAnInjectionThatWaitsForExplorer) {
  FakeInjector injector;
  injector.loaded = false;
  std::atomic<bool> waiting{false};
  injector.blockInject = [&] {
    waiting = true;
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!(injector.stopCheck && injector.stopCheck()) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(50ms);
    }
  };
  SystrayHook hook(options(&injector));
  ASSERT_TRUE(hook.valid());
  ASSERT_TRUE(injector.stopCheck);
  EXPECT_FALSE(injector.stopCheck());
  hook.start();
  const auto started = std::chrono::steady_clock::now();
  while (!waiting && std::chrono::steady_clock::now() - started < 5s)
    std::this_thread::sleep_for(10ms);
  ASSERT_TRUE(waiting);
  const auto begin = std::chrono::steady_clock::now();
  hook.stop();
  EXPECT_LT(std::chrono::steady_clock::now() - begin, 1s);
  EXPECT_TRUE(injector.stopCheck());
}