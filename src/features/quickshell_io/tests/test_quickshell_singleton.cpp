#include <gtest/gtest.h>

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <optional>

#include "features/quickshell_io/command_spec.h"
#include "features/quickshell_io/qml/clipboard_text.h"
#include "features/quickshell_io/qml/data_stream_parsers.h"
#include "features/quickshell_io/qml/quickshell_singleton.h"

using namespace waku::quickshell_io;

namespace {

struct Fixture {
  QTemporaryDir dir;
  std::optional<CommandSpec> launched;
  bool launchResult = true;
  std::unique_ptr<QuickshellSingleton> qs;

  Fixture() {
    QuickshellSingleton::Dirs dirs{
        dir.filePath(QStringLiteral("shell")), dir.filePath(QStringLiteral("data")),
        dir.filePath(QStringLiteral("state")), dir.filePath(QStringLiteral("cache"))};
    qs = std::make_unique<QuickshellSingleton>(
        dirs,
        [this](const CommandSpec& spec) {
          launched = spec;
          return launchResult;
        },
        nullptr);
  }
};

}  // namespace

TEST(QuickshellCommandSpec, ParsesAListAndAnObject) {
  const auto list =
      commandSpecFrom(QVariant(QStringList{QStringLiteral("a"), QStringLiteral("b")}));
  ASSERT_TRUE(list);
  EXPECT_EQ(list->command, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));

  QVariantMap map;
  map.insert(QStringLiteral("command"), QVariantList{QStringLiteral("x"), 5});
  map.insert(QStringLiteral("clearEnvironment"), true);
  map.insert(QStringLiteral("workingDirectory"), QStringLiteral("C:/w"));
  const auto object = commandSpecFrom(map);
  ASSERT_TRUE(object);
  EXPECT_EQ(object->command, (QStringList{QStringLiteral("x"), QStringLiteral("5")}));
  EXPECT_TRUE(object->clearEnvironment);
  EXPECT_EQ(object->workingDirectory, QStringLiteral("C:/w"));
}

TEST(QuickshellCommandSpec, RejectsEmptyAndWrongShapes) {
  EXPECT_FALSE(commandSpecFrom(QVariant()));
  EXPECT_FALSE(commandSpecFrom(QVariant(QStringLiteral("cmd /c echo"))));
  EXPECT_FALSE(commandSpecFrom(QVariant(QStringList{})));
  EXPECT_FALSE(commandSpecFrom(QVariantMap{}));
}

TEST(QuickshellCommandSpec, ElevatedIsOptInAndOnlyFromAnObject) {
  const auto list = commandSpecFrom(QVariant(QStringList{QStringLiteral("wt.exe")}));
  ASSERT_TRUE(list);
  EXPECT_FALSE(list->elevated);

  QVariantMap map;
  map.insert(QStringLiteral("command"), QVariantList{QStringLiteral("wt.exe")});
  const auto plain = commandSpecFrom(map);
  ASSERT_TRUE(plain);
  EXPECT_FALSE(plain->elevated);

  map.insert(QStringLiteral("elevated"), true);
  const auto elevated = commandSpecFrom(map);
  ASSERT_TRUE(elevated);
  EXPECT_TRUE(elevated->elevated);
}

TEST(QuickshellCommandSpec, WindowsArgumentsQuoteWhatNeedsIt) {
  EXPECT_EQ(windowsArguments({}), QString());
  EXPECT_EQ(windowsArguments({QStringLiteral("-d"), QStringLiteral("C:\\x")}),
            QStringLiteral("-d C:\\x"));
  EXPECT_EQ(windowsArguments({QStringLiteral("two words"), QString()}),
            QStringLiteral("\"two words\" \"\""));
  // A quote is escaped, and backslashes before it or before the closing quote are doubled.
  EXPECT_EQ(windowsArguments({QStringLiteral("say \"hi\"")}), QStringLiteral("\"say \\\"hi\\\"\""));
  EXPECT_EQ(windowsArguments({QStringLiteral("C:\\My Dir\\")}),
            QStringLiteral("\"C:\\My Dir\\\\\""));
}

TEST(QuickshellCommandSpec, EnvironmentSetsRemovesAndClears) {
  qputenv("WAKU_QS_KEEP", "1");
  CommandSpec spec;
  spec.command = {QStringLiteral("x")};
  spec.environment = {{QStringLiteral("WAKU_QS_NEW"), QStringLiteral("v")},
                      {QStringLiteral("WAKU_QS_KEEP"), QVariant()}};
  QProcessEnvironment env = makeEnvironment(spec);
  EXPECT_EQ(env.value(QStringLiteral("WAKU_QS_NEW")), QStringLiteral("v"));
  EXPECT_FALSE(env.contains(QStringLiteral("WAKU_QS_KEEP")));

  spec.clearEnvironment = true;
  env = makeEnvironment(spec);
  EXPECT_EQ(env.value(QStringLiteral("WAKU_QS_NEW")), QStringLiteral("v"));
  // With clearEnvironment a null entry passes the system value through.
  EXPECT_EQ(env.value(QStringLiteral("WAKU_QS_KEEP")), QStringLiteral("1"));
  EXPECT_FALSE(env.contains(QStringLiteral("PATH")));
  qunsetenv("WAKU_QS_KEEP");
}

TEST(QuickshellParsers, SplitParserJoinsChunksAndFlushesTheTail) {
  SplitParser parser;
  QStringList lines;
  QObject::connect(&parser, &DataStreamParser::read, [&](const QString& l) { lines << l; });
  parser.feed("a\r\nb");
  parser.feed("c\nd");
  EXPECT_EQ(lines, (QStringList{QStringLiteral("a"), QStringLiteral("bc")}));
  parser.streamEnded();
  EXPECT_EQ(lines.last(), QStringLiteral("d"));
}

TEST(QuickshellParsers, SplitParserHonoursACustomAndAnEmptyMarker) {
  SplitParser parser;
  QStringList lines;
  QObject::connect(&parser, &DataStreamParser::read, [&](const QString& l) { lines << l; });
  parser.setSplitMarker(QStringLiteral(";;"));
  parser.feed("a;;b;");
  parser.feed(";c");
  EXPECT_EQ(lines, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
  lines.clear();
  parser.reset();
  parser.setSplitMarker(QString());
  parser.feed("xyz");
  EXPECT_EQ(lines, (QStringList{QStringLiteral("xyz")}));
}

TEST(QuickshellParsers, StdioCollectorWaitsForTheEndByDefault) {
  StdioCollector collector;
  QSignalSpy finished(&collector, &StdioCollector::streamFinished);
  collector.feed("hel");
  collector.feed("lo");
  EXPECT_TRUE(collector.text().isEmpty());
  collector.streamEnded();
  EXPECT_EQ(collector.text(), QStringLiteral("hello"));
  EXPECT_EQ(collector.data(), QByteArray("hello"));
  EXPECT_EQ(finished.count(), 1);
}

TEST(QuickshellParsers, StdioCollectorStreamsWhenWaitForEndIsOff) {
  StdioCollector collector;
  collector.setWaitForEnd(false);
  collector.feed("ab");
  EXPECT_EQ(collector.text(), QStringLiteral("ab"));
  collector.reset();
  EXPECT_TRUE(collector.text().isEmpty());
}

TEST(QuickshellParsers, AStreamingCollectorKeepsAUtf8SequenceSplitAcrossChunks) {
  StdioCollector collector;
  collector.setWaitForEnd(false);
  QSignalSpy text(&collector, &StdioCollector::textChanged);
  const QByteArray euro = QStringLiteral("a€b").toUtf8();  // a, three bytes, b
  collector.feed(euro.left(2));                            // "a" and the first byte
  EXPECT_EQ(collector.text(), QStringLiteral("a"));
  collector.feed(euro.mid(2, 2));  // the rest of the euro sign
  collector.feed(euro.mid(4));     // "b"
  EXPECT_EQ(collector.text(), QStringLiteral("a€b"));
  EXPECT_EQ(collector.data(), euro);
  EXPECT_EQ(text.count(), 3);
  // A sequence cut off by the end of the stream shows as fromUtf8 shows it.
  collector.feed(QByteArray("\xE2\x82", 2));
  collector.streamEnded();
  EXPECT_EQ(collector.text(), QString::fromUtf8(euro + QByteArray("\xE2\x82", 2)));
}

TEST(QuickshellParsers, AStreamingCollectorTakesWhatCameWhileItWaited) {
  StdioCollector collector;
  collector.feed("one ");  // waitForEnd: held back
  collector.setWaitForEnd(false);
  collector.feed("two");
  EXPECT_EQ(collector.text(), QStringLiteral("one two"));
  collector.streamEnded();
  EXPECT_EQ(collector.data(), QByteArray("one two"));
}

TEST(QuickshellParsers, AStreamingCollectorScalesWithTheOutput) {
  StdioCollector collector;
  collector.setWaitForEnd(false);
  const QByteArray line(100, 'x');
  for (int i = 0; i < 20000; ++i) collector.feed(line);  // 2 MB in 100-byte chunks
  EXPECT_EQ(collector.text().size(), 2000000);
  EXPECT_EQ(collector.data().size(), 2000000);
}

TEST(QuickshellSingleton, EnvReturnsTheValueOrNull) {
  Fixture f;
  qputenv("WAKU_QS_ENV", "value");
  EXPECT_EQ(f.qs->env(QStringLiteral("WAKU_QS_ENV")).toString(), QStringLiteral("value"));
  qunsetenv("WAKU_QS_ENV");
  EXPECT_TRUE(f.qs->env(QStringLiteral("WAKU_QS_ENV")).isNull());
}

TEST(QuickshellSingleton, ExecDetachedPassesTheSpecToTheLauncher) {
  Fixture f;
  f.qs->execDetached(QVariant(QStringList{QStringLiteral("notepad.exe"), QStringLiteral("a.txt")}));
  ASSERT_TRUE(f.launched);
  EXPECT_EQ(f.launched->command,
            (QStringList{QStringLiteral("notepad.exe"), QStringLiteral("a.txt")}));

  f.launched.reset();
  QVariantMap object{{QStringLiteral("command"), QStringList{QStringLiteral("x")}},
                     {QStringLiteral("workingDirectory"), QStringLiteral("C:/w")}};
  f.qs->execDetached(object);
  ASSERT_TRUE(f.launched);
  EXPECT_EQ(f.launched->workingDirectory, QStringLiteral("C:/w"));

  f.launched.reset();
  f.qs->execDetached(QVariant(QStringLiteral("not a list")));
  EXPECT_FALSE(f.launched);
}

TEST(QuickshellSingleton, PathsJoinTheFourFolders) {
  Fixture f;
  const QString root = QDir::cleanPath(f.dir.path());
  EXPECT_EQ(f.qs->shellDir(), root + QStringLiteral("/shell"));
  EXPECT_EQ(f.qs->shellPath(QStringLiteral("a/b.qml")), root + QStringLiteral("/shell/a/b.qml"));
  EXPECT_EQ(f.qs->statePath(QStringLiteral("s.json")), root + QStringLiteral("/state/s.json"));
  EXPECT_EQ(f.qs->cachePath(QStringLiteral("c")), root + QStringLiteral("/cache/c"));
  EXPECT_EQ(f.qs->dataPath(QStringLiteral("d")), root + QStringLiteral("/data/d"));
  EXPECT_EQ(f.qs->shellPath(QStringLiteral("C:/abs/x")), QStringLiteral("C:/abs/x"));
  EXPECT_FALSE(QFileInfo::exists(f.qs->stateDir()));  // nothing is created
}

TEST(QuickshellSingleton, SetShellDirNotifies) {
  Fixture f;
  QSignalSpy spy(f.qs.get(), &QuickshellSingleton::shellDirChanged);
  f.qs->setShellDir(QStringLiteral("C:/cfg"));
  EXPECT_EQ(spy.count(), 1);
  EXPECT_EQ(f.qs->shellPath(QStringLiteral("x")), QStringLiteral("C:/cfg/x"));
}

TEST(QuickshellSingleton, ReloadEmitsTheHostSignal) {
  Fixture f;
  QSignalSpy spy(f.qs.get(), &QuickshellSingleton::reloadRequested);
  f.qs->reload(true);
  f.qs->reload();
  ASSERT_EQ(spy.count(), 2);
  EXPECT_TRUE(spy.at(0).at(0).toBool());
  EXPECT_FALSE(spy.at(1).at(0).toBool());
}

TEST(QuickshellSingleton, IconPathStubFallsBackToTheName) {
  Fixture f;
  EXPECT_EQ(f.qs->iconPath(QStringLiteral("folder")), QStringLiteral("folder"));
  EXPECT_EQ(f.qs->iconPath(QStringLiteral("folder"), QVariant(true)), QString());
  EXPECT_EQ(f.qs->iconPath(QStringLiteral("folder"), QVariant(QStringLiteral("fallback"))),
            QStringLiteral("fallback"));
  EXPECT_EQ(f.qs->iconPath(QStringLiteral("C:/i.png")), QStringLiteral("C:/i.png"));
  EXPECT_EQ(f.qs->iconPath(QStringLiteral("file:///i.png")), QStringLiteral("file:///i.png"));
}

TEST(QuickshellSingleton, VersionChecksEmulateQuickshell03) {
  Fixture f;
  EXPECT_TRUE(f.qs->hasVersion(0, 3));
  EXPECT_TRUE(f.qs->hasVersion(0, 2));
  EXPECT_FALSE(f.qs->hasVersion(0, 4));
  EXPECT_FALSE(f.qs->hasVersion(1, 0));
  EXPECT_TRUE(f.qs->hasQtVersion(6, 0));
  EXPECT_FALSE(f.qs->hasQtVersion(99, 0));
}

TEST(QuickshellSingleton, ProcessIdAndClipboardRoundTrip) {
  Fixture f;
  EXPECT_GT(f.qs->processId(), 0);
  f.qs->setClipboardText(QStringLiteral("waku clip"));
  EXPECT_EQ(f.qs->clipboardText(), QStringLiteral("waku clip"));
}

TEST(QuickshellSingleton, ReadsTheClipboardOnFirstUseAndCachesUntilItChanges) {
  QGuiApplication::clipboard()->setText(QStringLiteral("before"));
  const int start = ClipboardText::readsForTesting();
  Fixture f;
  EXPECT_EQ(ClipboardText::readsForTesting(), start) << "construction must not read";

  QSignalSpy changed(f.qs.get(), &QuickshellSingleton::clipboardTextChanged);
  EXPECT_EQ(f.qs->clipboardText(), QStringLiteral("before"));
  EXPECT_EQ(f.qs->clipboardText(), QStringLiteral("before"));
  EXPECT_EQ(ClipboardText::readsForTesting(), start + 1);

  // Another writer changes the clipboard: the cache is dropped and readers are told.
  QGuiApplication::clipboard()->setText(QStringLiteral("after"));
  EXPECT_GE(changed.count(), 1);
  EXPECT_EQ(f.qs->clipboardText(), QStringLiteral("after"));
  EXPECT_EQ(ClipboardText::readsForTesting(), start + 2);
}

TEST(QuickshellSingleton, ClipboardChangesBeforeAReadAreSilent) {
  Fixture f;
  QSignalSpy changed(f.qs.get(), &QuickshellSingleton::clipboardTextChanged);
  const int start = ClipboardText::readsForTesting();
  QGuiApplication::clipboard()->setText(QStringLiteral("unread"));
  EXPECT_EQ(changed.count(), 0);
  EXPECT_EQ(ClipboardText::readsForTesting(), start);
}

TEST(QuickshellSingleton, ElapsedTimerReportsSecondsAndRestarts) {
  ElapsedTimer timer;
  QTest::qWait(30);
  EXPECT_GT(timer.elapsedMs(), 20.0);
  EXPECT_NEAR(timer.elapsed() * 1000.0, timer.elapsedMs(), 50.0);
  const double before = timer.restart();
  EXPECT_GT(before, 0.02);
  EXPECT_LT(timer.elapsed(), before);
}

TEST(QuickshellSingleton, IsUsableFromQml) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import waku.services
QtObject {
    property real seconds: stopwatch.elapsed()
    property var stopwatch: ElapsedTimer {}
    property int pid: QuickshellSingleton.processId
    property var missing: QuickshellSingleton.env("WAKU_QS_SURELY_UNSET_VARIABLE")
    property bool v: QuickshellSingleton.hasVersion(0, 3)
}
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  EXPECT_GT(root->property("pid").toInt(), 0);
  EXPECT_TRUE(root->property("v").toBool());
  EXPECT_TRUE(root->property("missing").isNull());
}

TEST(QuickshellIoModule, ImportQuickshellIoExposesTheTypes) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import Quickshell.Io
QtObject {
    property var proc: Process { command: ["cmd.exe"] }
    property var view: FileView { path: "" }
    property var collector: StdioCollector {}
    property var parser: SplitParser { splitMarker: "|" }
    property int err: FileViewError.NotAFile
}
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  EXPECT_TRUE(root->property("proc").value<QObject*>());
  EXPECT_TRUE(root->property("view").value<QObject*>());
  EXPECT_EQ(root->property("parser").value<QObject*>()->property("splitMarker").toString(),
            QStringLiteral("|"));
}
