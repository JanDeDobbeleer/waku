#include <gtest/gtest.h>

#include <QByteArray>

#include "features/keymap/keymap_action.h"
#include "features/keymap/keymap_parser.h"

using namespace waku::keymap;

namespace {

Action mustParse(const QString& command) {
  const ActionParse parsed = parseAction(command);
  EXPECT_TRUE(parsed.action.has_value()) << parsed.error.toStdString();
  return parsed.action.value_or(Action());
}

}  // namespace

TEST(KeymapAction, PlainCommandIsAShellCommand) {
  const Action action = mustParse(QStringLiteral("  komorebic focus left  "));
  EXPECT_EQ(action.kind, Action::Kind::Shell);
  EXPECT_EQ(action.text, QStringLiteral("komorebic focus left"));
}

TEST(KeymapAction, ShortcutSurfaceCallReload) {
  EXPECT_EQ(mustParse(QStringLiteral("waku:shortcut launcher")).text,
            QStringLiteral("shortcut launcher"));
  EXPECT_EQ(mustParse(QStringLiteral("waku:shortcut \"my shortcut\"")).text,
            QStringLiteral("shortcut \"my shortcut\""));
  EXPECT_EQ(mustParse(QStringLiteral("waku:surface toggle launcher")).text,
            QStringLiteral("surface toggle launcher"));
  EXPECT_EQ(mustParse(QStringLiteral("WAKU:Surface Hide osd")).text,
            QStringLiteral("surface hide osd"));
  EXPECT_EQ(mustParse(QStringLiteral("waku:call volume set 30")).text,
            QStringLiteral("call volume set 30"));
  const Action reload = mustParse(QStringLiteral("waku:reload"));
  EXPECT_EQ(reload.kind, Action::Kind::Bar);
  EXPECT_EQ(reload.text, QStringLiteral("reload"));
  EXPECT_EQ(mustParse(QStringLiteral("waku:keys-reload")).kind, Action::Kind::KeysReload);
  EXPECT_EQ(mustParse(QStringLiteral("waku:keys-stop")).kind, Action::Kind::KeysStop);
}

TEST(KeymapAction, ActivateOrLaunchTakesATargetAndTheRestAsCommand) {
  const Action action =
      mustParse(QStringLiteral("waku:activate-or-launch firefox.exe start firefox -new-window"));
  EXPECT_EQ(action.kind, Action::Kind::ActivateOrLaunch);
  EXPECT_EQ(action.target, QStringLiteral("firefox.exe"));
  EXPECT_EQ(action.text, QStringLiteral("start firefox -new-window"));

  const Action quoted =
      mustParse(QStringLiteral("waku:activate-or-launch \"Windows Terminal\" wt"));
  EXPECT_EQ(quoted.target, QStringLiteral("Windows Terminal"));
  EXPECT_EQ(quoted.text, QStringLiteral("wt"));
}

TEST(KeymapAction, BadWakuActionsGiveAnError) {
  const QStringList bad = {
      QStringLiteral("waku:"),
      QStringLiteral("waku:nope"),
      QStringLiteral("waku:shortcut"),
      QStringLiteral("waku:shortcut a b"),
      QStringLiteral("waku:surface show"),
      QStringLiteral("waku:surface explode x"),
      QStringLiteral("waku:call onlytarget"),
      QStringLiteral("waku:reload now"),
      QStringLiteral("waku:keys-stop x"),
      QStringLiteral("waku:activate-or-launch firefox"),
      QStringLiteral("waku:activate-or-launch"),
      QStringLiteral("waku:shortcut \"open"),
      QString(),
  };
  for (const QString& command : bad) {
    const ActionParse parsed = parseAction(command);
    EXPECT_FALSE(parsed.action.has_value()) << command.toStdString();
    EXPECT_FALSE(parsed.error.isEmpty()) << command.toStdString();
  }
}

TEST(KeymapShellInvocation, CmdUsesNativeArguments) {
  const ShellInvocation invocation =
      shellInvocation(Shell::Cmd, QStringLiteral("echo \"a b\" & dir"));
  EXPECT_EQ(invocation.program, QStringLiteral("cmd.exe"));
  EXPECT_TRUE(invocation.args.isEmpty());
  EXPECT_EQ(invocation.nativeArgs, QStringLiteral("/c \"echo \"a b\" & dir\""));
}

TEST(KeymapShellInvocation, PowerShellGetsAnEncodedCommand) {
  const QString command = QStringLiteral("Write-Output \"é 'x'\"");
  for (const Shell shell : {Shell::Powershell, Shell::Pwsh}) {
    const ShellInvocation invocation = shellInvocation(shell, command);
    EXPECT_EQ(invocation.program,
              shell == Shell::Pwsh ? QStringLiteral("pwsh.exe") : QStringLiteral("powershell.exe"));
    ASSERT_GE(invocation.args.size(), 2);
    EXPECT_TRUE(invocation.args.contains(QStringLiteral("-NoProfile")));
    EXPECT_TRUE(invocation.args.contains(QStringLiteral("Hidden")));
    EXPECT_EQ(invocation.args.at(invocation.args.size() - 2), QStringLiteral("-EncodedCommand"));
    const QByteArray raw = QByteArray::fromBase64(invocation.args.last().toLatin1());
    const QString decoded(reinterpret_cast<const QChar*>(raw.constData()), raw.size() / 2);
    EXPECT_EQ(decoded, command);
    EXPECT_TRUE(invocation.nativeArgs.isEmpty());
  }
}

TEST(KeymapForeground, PlainBindingRunsEverywhere) {
  const Binding binding = parseKeymap(QStringLiteral("alt + a : x")).keymap.bindings.at(0);
  EXPECT_EQ(commandForForeground(binding, {}, QStringLiteral("anything.exe")), QStringLiteral("x"));
  EXPECT_EQ(commandForForeground(binding, {}, QString()), QStringLiteral("x"));
}

TEST(KeymapForeground, BlacklistSilencesEveryBinding) {
  const Binding binding = parseKeymap(QStringLiteral("alt + a : x")).keymap.bindings.at(0);
  QString reason;
  EXPECT_TRUE(commandForForeground(binding, {QStringLiteral("Game.exe")},
                                   QStringLiteral("game.EXE"), &reason)
                  .isEmpty());
  EXPECT_FALSE(reason.isEmpty());
  // The name may be written without the extension; an app not on the list is not affected.
  EXPECT_TRUE(commandForForeground(binding, {QStringLiteral("game")}, QStringLiteral("game.exe"))
                  .isEmpty());
  EXPECT_EQ(commandForForeground(binding, {QStringLiteral("game")}, QStringLiteral("other.exe")),
            QStringLiteral("x"));
}

TEST(KeymapForeground, BlockPicksTheRuleOfTheForegroundApp) {
  const Binding binding =
      parseKeymap(QStringLiteral("alt + n [\n Firefox : ff\n Google Chrome : chrome\n"
                                 " Default : other\n Notepad : Ignore\n]\n"))
          .keymap.bindings.at(0);
  EXPECT_EQ(commandForForeground(binding, {}, QStringLiteral("firefox.exe")), QStringLiteral("ff"));
  // The rule name is a process name ("Google Chrome" is not "chrome"), so Default applies.
  EXPECT_EQ(commandForForeground(binding, {}, QStringLiteral("chrome.exe")),
            QStringLiteral("other"));
  EXPECT_EQ(commandForForeground(binding, {}, QStringLiteral("code.exe")), QStringLiteral("other"));
  EXPECT_EQ(commandForForeground(binding, {}, QString()), QStringLiteral("other"));
  QString reason;
  EXPECT_TRUE(commandForForeground(binding, {}, QStringLiteral("notepad.exe"), &reason).isEmpty());
  EXPECT_FALSE(reason.isEmpty());
}

TEST(KeymapForeground, BlockWithoutDefaultDoesNothingElsewhere) {
  const Binding binding =
      parseKeymap(QStringLiteral("alt + n [\n Firefox : ff\n]\n")).keymap.bindings.at(0);
  EXPECT_TRUE(commandForForeground(binding, {}, QStringLiteral("code.exe")).isEmpty());
}
