#include <windows.h>
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <memory>

#include "features/quickshell_io/command_spec.h"
#include "features/quickshell_io/process_job.h"
#include "features/quickshell_io/qml/data_stream_parsers.h"
#include "features/quickshell_io/qml/qs_process.h"

using namespace wabar::quickshell_io;

namespace {

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 15000) {
  return QTest::qWaitFor(condition, timeoutMs);
}

QStringList cmd(const QString& line) {
  return {QStringLiteral("cmd.exe"), QStringLiteral("/c"), line};
}

QStringList sleeper() {
  return {QStringLiteral("ping.exe"), QStringLiteral("-n"), QStringLiteral("60"),
          QStringLiteral("127.0.0.1")};
}

bool processGone(qint64 pid, int timeoutMs) {
  HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
  if (!handle) return true;
  const DWORD result = WaitForSingleObject(handle, static_cast<DWORD>(timeoutMs));
  CloseHandle(handle);
  return result == WAIT_OBJECT_0;
}

// A Process with a collector on stdout.
struct Child {
  QsProcess process;
  StdioCollector out;
  StdioCollector err;
  QSignalSpy exited{&process, &QsProcess::exited};
  Child() {
    process.setStdoutParser(&out);
    process.setStderrParser(&err);
  }
  bool finish() {
    return waitFor([&] { return exited.count() >= 1; });
  }
};

}  // namespace

TEST(QuickshellProcess, CollectsStdoutAndReportsExitCode) {
  Child run;
  run.process.setCommand(cmd(QStringLiteral("echo hello")));
  QSignalSpy started(&run.process, &QsProcess::started);
  QSignalSpy finished(&run.out, &StdioCollector::streamFinished);
  run.process.setRunning(true);
  EXPECT_TRUE(run.process.running());
  ASSERT_TRUE(run.finish());
  EXPECT_EQ(started.count(), 1);
  EXPECT_EQ(finished.count(), 1);
  EXPECT_EQ(run.out.text().trimmed(), QStringLiteral("hello"));
  EXPECT_EQ(run.exited.first().at(0).toInt(), 0);
  EXPECT_FALSE(run.process.running());
  EXPECT_FALSE(run.process.processId().isValid());
}

TEST(QuickshellProcess, ReportsANonZeroExitCodeAndStderr) {
  Child run;
  run.process.setCommand(cmd(QStringLiteral("echo oops 1>&2 & exit 3")));
  run.process.setRunning(true);
  ASSERT_TRUE(run.finish());
  EXPECT_EQ(run.exited.first().at(0).toInt(), 3);
  EXPECT_EQ(run.err.text().trimmed(), QStringLiteral("oops"));
  EXPECT_TRUE(run.out.text().isEmpty());
}

TEST(QuickshellProcess, SplitParserEmitsOneReadPerLineWithoutCarriageReturns) {
  QsProcess process;
  SplitParser parser;
  process.setStdoutParser(&parser);
  QStringList lines;
  QObject::connect(&parser, &DataStreamParser::read, [&](const QString& line) { lines << line; });
  QSignalSpy exited(&process, &QsProcess::exited);
  process.setCommand(cmd(QStringLiteral("(echo one)&(echo two)")));
  process.setRunning(true);
  ASSERT_TRUE(waitFor([&] { return exited.count() == 1; }));
  EXPECT_EQ(lines, (QStringList{QStringLiteral("one"), QStringLiteral("two")}));
}

TEST(QuickshellProcess, ExecAppliesTheContextAndRunsAgain) {
  Child run;
  run.process.exec(QVariant(cmd(QStringLiteral("echo first"))));
  ASSERT_TRUE(run.finish());
  EXPECT_EQ(run.out.text().trimmed(), QStringLiteral("first"));
  run.process.exec(QVariant(cmd(QStringLiteral("echo second"))));
  ASSERT_TRUE(waitFor([&] { return run.exited.count() == 2; }));
  EXPECT_EQ(run.out.text().trimmed(), QStringLiteral("second"));  // reset between runs
}

TEST(QuickshellProcess, ExecObjectSetsEnvironmentAndWorkingDirectory) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  Child run;
  QVariantMap context;
  context.insert(QStringLiteral("command"), cmd(QStringLiteral("echo %WABAR_QS_VAR%& cd")));
  context.insert(QStringLiteral("environment"),
                 QVariantMap{{QStringLiteral("WABAR_QS_VAR"), QStringLiteral("xyz")}});
  context.insert(QStringLiteral("workingDirectory"), dir.path());
  run.process.exec(context);
  ASSERT_TRUE(run.finish());
  const QString text = run.out.text();
  EXPECT_TRUE(text.contains(QStringLiteral("xyz"))) << text.toStdString();
  EXPECT_TRUE(text.contains(QDir::toNativeSeparators(dir.path()), Qt::CaseInsensitive))
      << text.toStdString();
}

TEST(QuickshellProcess, NullEnvironmentValueRemovesAVariable) {
  qputenv("WABAR_QS_REMOVE", "present");
  Child run;
  QVariantMap context;
  context.insert(QStringLiteral("command"), cmd(QStringLiteral("echo [%WABAR_QS_REMOVE%]")));
  context.insert(QStringLiteral("environment"),
                 QVariantMap{{QStringLiteral("WABAR_QS_REMOVE"), QVariant()}});
  run.process.exec(context);
  ASSERT_TRUE(run.finish());
  qunsetenv("WABAR_QS_REMOVE");
  EXPECT_TRUE(run.out.text().contains(QStringLiteral("[%WABAR_QS_REMOVE%]")))
      << run.out.text().toStdString();
}

TEST(QuickshellProcess, ClosedStdinLetsAReaderFinish) {
  Child run;
  run.process.setCommand({QStringLiteral("findstr.exe"), QStringLiteral("^")});
  run.process.setRunning(true);
  ASSERT_TRUE(run.finish());                     // stdin is closed, findstr sees EOF
  run.process.write(QStringLiteral("ignored"));  // no process: no effect
}

TEST(QuickshellProcess, SettingRunningFalseKillsTheProcess) {
  Child run;
  run.process.setCommand(sleeper());
  QSignalSpy started(&run.process, &QsProcess::started);
  run.process.setRunning(true);
  ASSERT_TRUE(waitFor([&] { return started.count() == 1; }));
  EXPECT_TRUE(run.process.processId().isValid());
  run.process.setRunning(false);
  ASSERT_TRUE(run.finish());
  EXPECT_FALSE(run.process.running());
}

TEST(QuickshellProcess, SignalKillsOnlyForTerminatingNumbers) {
  Child run;
  run.process.setCommand(sleeper());
  QSignalSpy started(&run.process, &QsProcess::started);
  run.process.setRunning(true);
  ASSERT_TRUE(waitFor([&] { return started.count() == 1; }));
  run.process.signal(1);  // SIGHUP: ignored on Windows
  QTest::qWait(300);
  EXPECT_TRUE(run.process.running());
  run.process.signal(15);
  ASSERT_TRUE(run.finish());
}

TEST(QuickshellProcess, ProgramThatCannotStartLeavesItNotRunning) {
  QsProcess process;
  process.setCommand({QStringLiteral("definitely-not-a-program-wabar.exe")});
  QSignalSpy running(&process, &QsProcess::runningChanged);
  process.setRunning(true);
  ASSERT_TRUE(waitFor([&] { return !process.running(); }));
  EXPECT_FALSE(process.running());
}

TEST(QuickshellProcess, EmptyCommandDoesNotStart) {
  QsProcess process;
  process.setRunning(true);
  EXPECT_FALSE(process.running());
}

TEST(QuickshellProcess, DestroyingTheObjectKillsTheChild) {
  auto process = std::make_unique<QsProcess>();
  process->setCommand(sleeper());
  QSignalSpy started(process.get(), &QsProcess::started);
  process->setRunning(true);
  ASSERT_TRUE(waitFor([&] { return started.count() == 1; }));
  const qint64 pid = process->processId().toLongLong();
  ASSERT_GT(pid, 0);
  process.reset();
  EXPECT_TRUE(processGone(pid, 5000));
}

TEST(QuickshellProcess, ExitedDelivesTheStatusEnum) {
  Child run;
  run.process.setCommand(cmd(QStringLiteral("exit 0")));
  run.process.setRunning(true);
  ASSERT_TRUE(run.finish());
  EXPECT_EQ(run.exited.first().at(1).toInt(), static_cast<int>(QsProcess::NormalExit));
}

TEST(QuickshellProcess, StartDetachedLeavesRunningFalse) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString marker = dir.filePath(QStringLiteral("detached.txt"));
  QsProcess process;
  process.setCommand(cmd(QStringLiteral("echo done>") + QDir::toNativeSeparators(marker)));
  process.startDetached();
  EXPECT_FALSE(process.running());
  EXPECT_TRUE(waitFor([&] { return QFile::exists(marker); }));
}

TEST(QuickshellProcess, WorksFromQml) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import wabar.services
QtObject {
    id: root
    property string result: ""
    property int code: -1
    property var proc: Process {
        command: ["cmd.exe", "/c", "echo from-qml"]
        stdout: StdioCollector { onStreamFinished: root.result = text }
        onExited: (exitCode, exitStatus) => root.code = exitCode
    }
    Component.onCompleted: proc.running = true
}
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  ASSERT_TRUE(waitFor([&] { return root->property("code").toInt() == 0; }));
  EXPECT_EQ(root->property("result").toString().trimmed(), QStringLiteral("from-qml"));
}

TEST(QuickshellProcess, ShellProcessKeepsItsOldInterfaceUnderTheNewName) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import wabar.services
ShellProcess { program: "cmd.exe"; arguments: ["/c", "echo x"] }
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  EXPECT_EQ(root->property("program").toString(), QStringLiteral("cmd.exe"));
}

TEST(QuickshellProcessJob, TerminateKillsTheAdoptedProcess) {
  QProcess child;
  child.start(sleeper().first(), sleeper().mid(1));
  ASSERT_TRUE(child.waitForStarted(5000));
  ProcessJob job;
  ASSERT_TRUE(job.adopt(child.processId()));
  job.terminate();
  EXPECT_TRUE(child.waitForFinished(5000));
}

TEST(QuickshellProcessJob, ClosingTheJobKillsTheProcessAndReleaseSpares) {
  QProcess killed;
  killed.start(sleeper().first(), sleeper().mid(1));
  ASSERT_TRUE(killed.waitForStarted(5000));
  QProcess spared;
  spared.start(sleeper().first(), sleeper().mid(1));
  ASSERT_TRUE(spared.waitForStarted(5000));
  {
    ProcessJob job;
    ASSERT_TRUE(job.adopt(killed.processId()));
  }
  EXPECT_TRUE(killed.waitForFinished(5000));
  {
    ProcessJob job;
    ASSERT_TRUE(job.adopt(spared.processId()));
    job.release();
  }
  EXPECT_FALSE(spared.waitForFinished(500));
  spared.kill();
  spared.waitForFinished(5000);
}
