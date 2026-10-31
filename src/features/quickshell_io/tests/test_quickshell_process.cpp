#include <windows.h>
#include <gtest/gtest.h>
#include <tlhelp32.h>

#include <QCoreApplication>
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
#include "features/quickshell_io/qml/shell_process.h"

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

// The pids of the processes named `exe` whose parent is `parent`.
QList<qint64> childrenOf(qint64 parent, const wchar_t* exe) {
  QList<qint64> pids;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return pids;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
    if (entry.th32ParentProcessID == static_cast<DWORD>(parent) &&
        _wcsicmp(entry.szExeFile, exe) == 0) {
      pids << entry.th32ProcessID;
    }
  }
  CloseHandle(snapshot);
  return pids;
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
  // As a SIGTERM'd process reports in Quickshell: a crash exit with the signal number.
  EXPECT_EQ(run.exited.first().at(0).toInt(), 15);
  EXPECT_EQ(run.exited.first().at(1).toInt(), static_cast<int>(QsProcess::CrashExit));
}

TEST(QuickshellProcess, ASignalReportsItsNumberAsTheExitCode) {
  Child run;
  run.process.setCommand(sleeper());
  QSignalSpy started(&run.process, &QsProcess::started);
  run.process.setRunning(true);
  ASSERT_TRUE(waitFor([&] { return started.count() == 1; }));
  run.process.signal(9);
  ASSERT_TRUE(run.finish());
  EXPECT_EQ(run.exited.first().at(0).toInt(), 9);
  EXPECT_EQ(run.exited.first().at(1).toInt(), static_cast<int>(QsProcess::CrashExit));
}

TEST(QuickshellProcess, RunningSetBeforeTheCommandStartsWhenTheCommandArrives) {
  Child run;
  run.process.setRunning(true);  // nothing to run yet
  EXPECT_FALSE(run.process.running());
  run.process.setCommand(cmd(QStringLiteral("echo late")));
  ASSERT_TRUE(run.finish());
  EXPECT_EQ(run.out.text().trimmed(), QStringLiteral("late"));
  EXPECT_EQ(run.exited.first().at(0).toInt(), 0);
  EXPECT_FALSE(run.process.running());
  // The wish is spent: changing the command again starts nothing.
  run.process.setCommand(cmd(QStringLiteral("echo again")));
  // A start would have created the child inside setCommand(); without one no exit can come.
  EXPECT_FALSE(run.process.running());
  QCoreApplication::processEvents();
  EXPECT_EQ(run.exited.count(), 1);
}

TEST(QuickshellProcess, ExecOverARunningProcessEndsTheOldRunWithoutWaiting) {
  Child run;
  run.process.setCommand(sleeper());
  QSignalSpy started(&run.process, &QsProcess::started);
  run.process.setRunning(true);
  ASSERT_TRUE(waitFor([&] { return started.count() == 1; }));
  const qint64 oldPid = run.process.processId().toLongLong();

  run.process.exec(QVariant(cmd(QStringLiteral("echo replaced"))));
  // The old run is over at once: exited() came before exec() returned.
  ASSERT_EQ(run.exited.count(), 1);
  EXPECT_EQ(run.exited.first().at(1).toInt(), static_cast<int>(QsProcess::CrashExit));
  ASSERT_TRUE(waitFor([&] { return run.exited.count() == 2; }));
  EXPECT_EQ(run.exited.at(1).at(0).toInt(), 0);
  EXPECT_EQ(run.out.text().trimmed(), QStringLiteral("replaced"));
  EXPECT_TRUE(processGone(oldPid, 5000));
}

TEST(QuickshellProcess, SignalKillsOnlyForTerminatingNumbers) {
  Child run;
  run.process.setCommand(sleeper());
  QSignalSpy started(&run.process, &QsProcess::started);
  run.process.setRunning(true);
  ASSERT_TRUE(waitFor([&] { return started.count() == 1; }));
  run.process.signal(1);  // SIGHUP: ignored on Windows; the decision is made inside the call
  QCoreApplication::processEvents();
  EXPECT_TRUE(run.process.running());
  run.process.signal(15);
  ASSERT_TRUE(run.finish());
}

TEST(QuickshellProcess, ProgramThatCannotStartLeavesItNotRunning) {
  QsProcess process;
  process.setCommand({QStringLiteral("definitely-not-a-program-wabar.exe")});
  QList<bool> states;
  QObject::connect(&process, &QsProcess::runningChanged, [&] { states << process.running(); });
  QSignalSpy started(&process, &QsProcess::started);
  QSignalSpy exited(&process, &QsProcess::exited);
  process.setRunning(true);
  ASSERT_TRUE(waitFor([&] { return !process.running(); }));
  QCoreApplication::processEvents();  // anything the failed start still queued
  EXPECT_FALSE(process.running());
  EXPECT_EQ(states, (QList<bool>{true, false}));  // on, then off; nothing after
  EXPECT_EQ(started.count(), 0);
  EXPECT_EQ(exited.count(), 0);
  EXPECT_FALSE(process.processId().isValid());
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

TEST(ShellProcess, DestroyingKillsTheGrandchildrenToo) {
  // As for QsProcess: cmd waits a second (so the job has it), starts a hidden ping of its own and
  // runs another. ShellProcess shares the job object, so destroying it kills all three.
  const QStringList line =
      cmd(QStringLiteral("ping -n 2 127.0.0.1 >nul & start /b ping -n 60 127.0.0.1 >nul & "
                         "ping -n 60 127.0.0.1 >nul"));
  auto process = std::make_unique<ShellProcess>();
  process->setProgram(line.first());
  process->setArguments(line.mid(1));
  process->setTimeout(60000);
  process->run();
  QList<qint64> shells;
  ASSERT_TRUE(waitFor([&] {
    shells = childrenOf(QCoreApplication::applicationPid(), L"cmd.exe");
    return shells.size() == 1;
  }));
  QList<qint64> grandchildren;
  ASSERT_TRUE(waitFor([&] {
    grandchildren = childrenOf(shells.first(), L"PING.EXE");
    return grandchildren.size() >= 2;  // the one `start /b` made and the foreground one
  }));
  process.reset();
  EXPECT_TRUE(processGone(shells.first(), 5000));
  for (qint64 pid : grandchildren) EXPECT_TRUE(processGone(pid, 5000)) << pid;
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

TEST(QuickshellProcessJob, TerminateKillsTheGrandchildrenToo) {
  // cmd waits a second (so the job has it), starts a hidden ping of its own and runs another.
  QProcess child;
  const QStringList line =
      cmd(QStringLiteral("ping -n 2 127.0.0.1 >nul & start /b ping -n 60 127.0.0.1 >nul & "
                         "ping -n 60 127.0.0.1 >nul"));
  child.start(line.first(), line.mid(1));
  ASSERT_TRUE(child.waitForStarted(5000));
  ProcessJob job;
  ASSERT_TRUE(job.adopt(child.processId()));
  const qint64 cmdPid = child.processId();
  QList<qint64> grandchildren;
  ASSERT_TRUE(waitFor(
      [&] {
        grandchildren = childrenOf(cmdPid, L"PING.EXE");
        return grandchildren.size() >= 2;  // the one `start /b` made and the foreground one
      },
      10000));
  job.terminate();
  EXPECT_TRUE(child.waitForFinished(5000));
  for (qint64 pid : grandchildren) EXPECT_TRUE(processGone(pid, 5000)) << pid;
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
