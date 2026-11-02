// GlobalShortcut and `wakuc shortcut`, all on the fake hotkey backend: no test registers a real
// hotkey on the machine. The kernel::Hotkeys table itself is tested in
// src/kernel/tests/test_hotkeys.cpp.
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QUuid>
#include <memory>

#include "control/control.h"
#include "host/qml/control_handler.h"
#include "host/qml/global_shortcut.h"
#include "host/testing/shell_fixtures.h"
#include "kernel/qt/hotkeys.h"
#include "kernel/testing/hotkeys_fixtures.h"

using namespace waku::host;
using waku::kernel::HotkeyCombo;
using waku::kernel::Hotkeys;
using waku::kernel::parseHotkey;
using waku::kernel::ScopedFakeHotkeys;

namespace {

// A QML object tree from `body`, with `import Quickshell`.
struct Qml {
  explicit Qml(const QByteArray& body) {
    QQmlComponent component(&engine);
    component.setData("import QtQml\nimport Quickshell\n" + body,
                      QUrl(QStringLiteral("file:///shortcuts.qml")));
    EXPECT_FALSE(component.isError()) << qPrintable(component.errorString());
    root.reset(component.create());
  }
  QQmlEngine engine;
  std::unique_ptr<QObject> root;
};

HotkeyCombo combo(const char* keys) { return parseHotkey(QString::fromLatin1(keys)).value(); }

}  // namespace

TEST(GlobalShortcutTest, AKeyPressFiresPressedAndReleased) {
  ScopedFakeHotkeys fake;
  Qml qml(R"(
Scope {
    id: root
    property int presses: 0
    property int releases: 0
    property bool sawDown: false
    GlobalShortcut {
        name: "launcher"
        appid: "waku"
        description: "Toggle the launcher"
        keys: "alt+space"
        onPressed: { root.presses++; root.sawDown = pressed }
        onReleased: root.releases++
    }
}
)");
  ASSERT_NE(qml.root, nullptr);
  auto* shortcut = qml.root->findChild<GlobalShortcut*>();
  ASSERT_NE(shortcut, nullptr);
  EXPECT_TRUE(shortcut->isRegistered());
  EXPECT_EQ(shortcut->description(), QStringLiteral("Toggle the launcher"));
  ASSERT_TRUE(fake.backend->press(combo("alt+space")));
  EXPECT_EQ(qml.root->property("presses").toInt(), 1);
  EXPECT_EQ(qml.root->property("releases").toInt(), 1);
  EXPECT_TRUE(qml.root->property("sawDown").toBool());
  EXPECT_FALSE(shortcut->isPressed());
}

TEST(GlobalShortcutTest, KeysChangeReregistersAndDestructionUnregisters) {
  ScopedFakeHotkeys fake;
  {
    GlobalShortcut shortcut;
    shortcut.setName(QStringLiteral("a"));
    shortcut.setKeys(QStringLiteral("alt+a"));
    shortcut.componentComplete();
    EXPECT_TRUE(shortcut.isRegistered());
    shortcut.setKeys(QStringLiteral("alt+b"));
    ASSERT_EQ(fake.backend->registered.size(), 1u);
    EXPECT_EQ(fake.backend->registered.begin()->second, combo("alt+b"));
    shortcut.setKeys(QStringLiteral("nonsense"));
    EXPECT_FALSE(shortcut.isRegistered());
    EXPECT_TRUE(fake.backend->registered.empty());
    shortcut.setKeys(QStringLiteral("alt+c"));
    EXPECT_TRUE(shortcut.isRegistered());
  }
  EXPECT_TRUE(fake.backend->registered.empty());
}

TEST(GlobalShortcutTest, ATakenKeyLeavesItUnregisteredButFirableByName) {
  ScopedFakeHotkeys fake;
  fake.backend->taken.insert(combo("alt+e"));
  GlobalShortcut shortcut;
  shortcut.setName(QStringLiteral("taken"));
  shortcut.setKeys(QStringLiteral("alt+e"));
  shortcut.componentComplete();
  EXPECT_FALSE(shortcut.isRegistered());
  int presses = 0;
  QObject::connect(&shortcut, &GlobalShortcut::pressed, [&] { ++presses; });
  EXPECT_TRUE(GlobalShortcut::fireByName(QStringLiteral("taken")));
  EXPECT_EQ(presses, 1);
}

TEST(GlobalShortcutTest, AHandlerThatDestroysTheShortcutDoesNotCrash) {
  ScopedFakeHotkeys fake;
  auto* shortcut = new GlobalShortcut;
  shortcut->setName(QStringLiteral("selfdestruct"));
  shortcut->componentComplete();
  int released = 0;
  QObject::connect(shortcut, &GlobalShortcut::released, [&] { ++released; });
  QObject::connect(shortcut, &GlobalShortcut::pressed, [shortcut] { delete shortcut; });
  EXPECT_TRUE(GlobalShortcut::fireByName(QStringLiteral("selfdestruct")));
  EXPECT_EQ(released, 0);
  EXPECT_FALSE(GlobalShortcut::names().contains(QStringLiteral("selfdestruct")));
}

TEST(GlobalShortcutTest, AHandlerThatDestroysAnotherShortcutOfTheSameNameIsSafe) {
  ScopedFakeHotkeys fake;
  auto* first = new GlobalShortcut;
  auto* second = new GlobalShortcut;
  for (GlobalShortcut* s : {first, second}) {
    s->setName(QStringLiteral("pair"));
    s->componentComplete();
  }
  int presses = 0;
  QObject::connect(first, &GlobalShortcut::pressed, [&] {
    ++presses;
    delete second;
  });
  QObject::connect(second, &GlobalShortcut::pressed, [&] { ++presses; });
  EXPECT_TRUE(GlobalShortcut::fireByName(QStringLiteral("pair")));
  EXPECT_EQ(presses, 1);
  delete first;
}

TEST(GlobalShortcutTest, NoKeysRegistersNothing) {
  ScopedFakeHotkeys fake;
  GlobalShortcut shortcut;
  shortcut.setName(QStringLiteral("only-wakuc"));
  shortcut.componentComplete();
  EXPECT_FALSE(shortcut.isRegistered());
  EXPECT_TRUE(fake.backend->registered.empty());
  EXPECT_EQ(fake.backend->registerCalls, 0);
}

TEST(GlobalShortcutTest, AnInertDesktopIgnoresKeys) {
  Hotkeys::instance().resetForTests();  // default: not live on the offscreen platform
  GlobalShortcut shortcut;
  shortcut.setKeys(QStringLiteral("alt+e"));
  shortcut.componentComplete();
  EXPECT_FALSE(shortcut.isRegistered());
  Hotkeys::instance().resetForTests();
}

// ---- the control verb ----

TEST(ShortcutCommand, FiresByNameAndListsTheNames) {
  ScopedFakeHotkeys fake;
  Qml qml(R"(
Scope {
    GlobalShortcut { name: "launcher"; onPressed: console.log("") }
    GlobalShortcut { name: "my pick"; keys: "ctrl+alt+p" }
    GlobalShortcut { name: "launcher" }
}
)");
  ASSERT_NE(qml.root, nullptr);
  EXPECT_EQ(answerShortcutCommand({QStringLiteral("list")}),
            QStringLiteral("ok: launcher \"my pick\""));
  EXPECT_EQ(answerShortcutCommand({QStringLiteral("launcher")}), QStringLiteral("ok"));
  EXPECT_EQ(answerShortcutCommand({QStringLiteral("my pick")}), QStringLiteral("ok"));
  EXPECT_TRUE(answerShortcutCommand({QStringLiteral("nope")}).startsWith(QStringLiteral("error:")));
  EXPECT_TRUE(answerShortcutCommand({}).startsWith(QStringLiteral("error: usage")));
  EXPECT_TRUE(answerShortcutCommand({QStringLiteral("a"), QStringLiteral("b")})
                  .startsWith(QStringLiteral("error: usage")));
}

TEST(ShortcutCommand, ListWithNoShortcutsSaysSo) {
  ScopedFakeHotkeys fake;
  EXPECT_EQ(answerShortcutCommand({QStringLiteral("list")}), QStringLiteral("ok: no shortcuts"));
}

TEST(ShortcutCommand, TheHandlerRoutesItAndWakucCarriesTheName) {
  ScopedFakeHotkeys fake;
  Harness h;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  GlobalShortcut shortcut;
  shortcut.setName(QStringLiteral("my pick"));
  shortcut.componentComplete();
  int presses = 0;
  QObject::connect(&shortcut, &GlobalShortcut::pressed, [&] { ++presses; });

  const auto handler = makeControlHandler(*h.host, false);
  EXPECT_EQ(handler(QStringLiteral("shortcut \"my pick\"")), QStringLiteral("ok"));
  EXPECT_EQ(presses, 1);
  EXPECT_TRUE(handler(QStringLiteral("shortcut \"oops")).startsWith(QStringLiteral("error:")));

  const QString wakuc =
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("wakuc.exe"));
  if (!QFile::exists(wakuc)) GTEST_SKIP() << "wakuc.exe is not built";
  QTemporaryDir home;
  ASSERT_TRUE(home.isValid());
  waku::control::ControlServer server(waku::control::controlPipeName(home.path()), handler);
  ASSERT_TRUE(server.listen());
  QProcess process;
  process.start(wakuc, {QStringLiteral("--config-home"), home.path(), QStringLiteral("shortcut"),
                        QStringLiteral("my pick")});
  ASSERT_TRUE(process.waitForStarted(5000));
  ASSERT_TRUE(QTest::qWaitFor([&] { return process.state() == QProcess::NotRunning; }, 10000));
  EXPECT_EQ(process.exitCode(), 0);
  EXPECT_EQ(presses, 2);
}
