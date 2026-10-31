#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QTest>
#include <functional>
#include <memory>

#include "features/quickshell_io/qml/shell_process.h"

using namespace wabar::quickshell_io;

namespace {

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 10000) {
  return QTest::qWaitFor(condition, timeoutMs);
}

ShellProcess* shell(const QString& commandLine, QObject* parent = nullptr) {
  auto* process = new ShellProcess(parent);
  process->setProgram(QStringLiteral("cmd.exe"));
  process->setArguments({QStringLiteral("/c"), commandLine});
  return process;
}

}  // namespace

TEST(ShellProcess, CapturesTrimmedOutputAndExitCode) {
  std::unique_ptr<ShellProcess> process(shell(QStringLiteral("echo hello")));
  QSignalSpy finished(process.get(), &ShellProcess::finished);
  process->run();
  EXPECT_TRUE(process->running());
  ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
  EXPECT_FALSE(process->running());
  EXPECT_EQ(process->output(), QStringLiteral("hello"));
  EXPECT_EQ(process->exitCode(), 0);
}

TEST(ShellProcess, ReportsANonZeroExitCode) {
  std::unique_ptr<ShellProcess> process(shell(QStringLiteral("exit 3")));
  QSignalSpy finished(process.get(), &ShellProcess::finished);
  process->run();
  ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
  EXPECT_EQ(process->exitCode(), 3);
}

TEST(ShellProcess, CapturesStandardError) {
  std::unique_ptr<ShellProcess> process(shell(QStringLiteral("echo oops 1>&2")));
  QSignalSpy finished(process.get(), &ShellProcess::finished);
  process->run();
  ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
  EXPECT_EQ(process->errorOutput(), QStringLiteral("oops"));
  EXPECT_TRUE(process->output().isEmpty());
}

TEST(ShellProcess, ProgramThatCannotStartFinishesWithMinusOne) {
  ShellProcess process;
  process.setProgram(QStringLiteral("definitely-not-a-program-wabar.exe"));
  QSignalSpy finished(&process, &ShellProcess::finished);
  process.run();
  ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
  EXPECT_EQ(process.exitCode(), -1);
  EXPECT_FALSE(process.running());
}

TEST(ShellProcess, EmptyProgramDoesNothing) {
  ShellProcess process;
  QSignalSpy finished(&process, &ShellProcess::finished);
  process.run();
  // No child and no timer: no finished() can come later.
  EXPECT_FALSE(process.running());
  EXPECT_FALSE(process.rerunScheduled());
  QCoreApplication::processEvents();
  EXPECT_EQ(finished.count(), 0);
}

TEST(ShellProcess, RunWhileRunningIsIgnored) {
  std::unique_ptr<ShellProcess> process(shell(QStringLiteral("ping -n 2 127.0.0.1 >nul")));
  QSignalSpy finished(process.get(), &ShellProcess::finished);
  process->run();
  process->run();
  ASSERT_TRUE(waitFor([&] { return finished.count() >= 1; }));
  // The second run() started no child of its own: nothing is left to finish.
  EXPECT_FALSE(process->running());
  EXPECT_FALSE(process->rerunScheduled());
  QCoreApplication::processEvents();
  EXPECT_EQ(finished.count(), 1);
}

TEST(ShellProcess, TimeoutKillsALongRun) {
  std::unique_ptr<ShellProcess> process(shell(QStringLiteral("ping -n 30 127.0.0.1 >nul")));
  process->setTimeout(300);
  QSignalSpy finished(process.get(), &ShellProcess::finished);
  process->run();
  ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }, 5000));
  EXPECT_NE(process->exitCode(), 0);
}

TEST(ShellProcess, IntervalRerunsAfterTheComponentIsComplete) {
  std::unique_ptr<ShellProcess> process(shell(QStringLiteral("echo tick")));
  process->setInterval(200);
  QSignalSpy finished(process.get(), &ShellProcess::finished);
  // Nothing runs before componentComplete(): no child, no timer.
  EXPECT_FALSE(process->running());
  EXPECT_FALSE(process->rerunScheduled());
  EXPECT_EQ(finished.count(), 0);
  process->componentComplete();
  EXPECT_TRUE(process->rerunScheduled());
  ASSERT_TRUE(waitFor([&] { return finished.count() >= 3; }));
  EXPECT_EQ(process->output(), QStringLiteral("tick"));
}

TEST(ShellProcess, NoIntervalMeansOnlyExplicitRuns) {
  std::unique_ptr<ShellProcess> process(shell(QStringLiteral("echo once")));
  QSignalSpy finished(process.get(), &ShellProcess::finished);
  process->componentComplete();
  EXPECT_FALSE(process->running());
  EXPECT_FALSE(process->rerunScheduled());
  QCoreApplication::processEvents();
  EXPECT_EQ(finished.count(), 0);
}

TEST(ShellProcess, DestroyingWhileRunningKillsTheChild) {
  auto process = std::unique_ptr<ShellProcess>(shell(QStringLiteral("ping -n 30 127.0.0.1 >nul")));
  process->run();
  ASSERT_TRUE(process->running());
  process.reset();  // must not crash or leave a signal pointing at the dead object
  // Deliver whatever the kill queued (the QProcess's own finish notifications).
  QCoreApplication::sendPostedEvents();
  QCoreApplication::processEvents();
  SUCCEED();
}
