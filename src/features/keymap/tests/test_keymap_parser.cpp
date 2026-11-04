#include <gtest/gtest.h>

#include <QFile>

#include "features/keymap/key_names.h"
#include "features/keymap/keymap_parser.h"
#include "features/keymap/testing/whkdrc_sample.h"

using namespace waku::keymap;
using waku::kernel::HotkeyCombo;

namespace {

constexpr unsigned kAlt = 1;
constexpr unsigned kCtrl = 2;
constexpr unsigned kShift = 4;
constexpr unsigned kWin = 8;

const Binding* findBinding(const KeymapParseResult& result, unsigned modifiers, unsigned key) {
  for (const Binding& binding : result.keymap.bindings) {
    if (binding.combo.modifiers == modifiers && binding.combo.virtualKey == key) return &binding;
  }
  return nullptr;
}

QString allIssues(const KeymapParseResult& result) {
  QStringList lines;
  for (const KeymapIssue& issue : result.issues) lines << issueText(issue);
  return lines.join(QLatin1Char('\n'));
}

}  // namespace

TEST(KeymapParserSample, EveryBindingOfTheRealWhkdrcLoads) {
  const KeymapParseResult result = parseKeymap(QString::fromUtf8(kWhkdrcSample));
  EXPECT_TRUE(result.issues.empty()) << allIssues(result).toStdString();
  EXPECT_EQ(result.keymap.bindings.size(), 53u);
  EXPECT_EQ(result.keymap.shell, Shell::Powershell);
  EXPECT_TRUE(result.keymap.blacklist.isEmpty());
}

TEST(KeymapParserSample, KeysAndCommandsAreRight) {
  const KeymapParseResult result = parseKeymap(QString::fromUtf8(kWhkdrcSample));
  const Binding* focus = findBinding(result, kAlt, 0x25);  // alt + left
  ASSERT_NE(focus, nullptr);
  EXPECT_EQ(focus->command, QStringLiteral("komorebic focus left"));
  // A trailing comment ("# oem_4 is [") is not part of the command.
  const Binding* cycle = findBinding(result, kAlt | kShift, 0xDB);
  ASSERT_NE(cycle, nullptr);
  EXPECT_EQ(cycle->command, QStringLiteral("komorebic cycle-focus previous"));
  const Binding* stack = findBinding(result, kCtrl | kAlt | kShift, 0x27);
  ASSERT_NE(stack, nullptr);
  EXPECT_EQ(stack->command, QStringLiteral("komorebic stack right"));
  const Binding* grow = findBinding(result, kAlt, 0xBB);  // alt + oem_plus
  ASSERT_NE(grow, nullptr);
  EXPECT_EQ(grow->command, QStringLiteral("komorebic resize-axis horizontal increase"));
  const Binding* workspace = findBinding(result, kAlt, '1');
  ASSERT_NE(workspace, nullptr);
  EXPECT_EQ(workspace->command, QStringLiteral("komorebic focus-workspace 0"));
}

TEST(KeymapParserSample, CommandsKeepQuotesSemicolonsAndPipelines) {
  const KeymapParseResult result = parseKeymap(QString::fromUtf8(kWhkdrcSample));
  const Binding* reload = findBinding(result, kAlt, 'O');
  ASSERT_NE(reload, nullptr);
  EXPECT_TRUE(reload->command.contains(QStringLiteral("-Filter \"Name = 'whkd.exe'\"")));
  EXPECT_TRUE(
      reload->command.contains(QStringLiteral("\"$env:USERPROFILE\\.config\\komorebi\\whkdrc\"")));
  EXPECT_TRUE(reload->command.endsWith(QStringLiteral("-WindowStyle hidden")));
  const Binding* both = findBinding(result, kAlt | kShift, 'F');
  ASSERT_NE(both, nullptr);
  EXPECT_EQ(both->command, QStringLiteral("komorebic toggle-float; komorebic toggle-maximize"));
}

TEST(KeymapParserSample, CommentedBindingsAreNotLoaded) {
  const KeymapParseResult result = parseKeymap(QString::fromUtf8(kWhkdrcSample));
  EXPECT_NE(findBinding(result, kAlt, 'F'), nullptr);  // "alt + f : komorebic toggle-float"
  EXPECT_EQ(findBinding(result, kAlt, 'B'), nullptr);  // the commented Chrome binding
}

TEST(KeymapParserSample, CrLfLineEndingsGiveTheSameResult) {
  QString text = QString::fromUtf8(kWhkdrcSample);
  text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
  const KeymapParseResult result = parseKeymap(text);
  EXPECT_TRUE(result.issues.empty()) << allIssues(result).toStdString();
  EXPECT_EQ(result.keymap.bindings.size(), 53u);
  const Binding* focus = findBinding(result, kAlt, 0x25);
  ASSERT_NE(focus, nullptr);
  EXPECT_EQ(focus->command, QStringLiteral("komorebic focus left"));
}

TEST(KeymapParserBindings, LineNumbersAreOneBased) {
  const KeymapParseResult result =
      parseKeymap(QStringLiteral("# first\n\nalt + a : one\nalt + b : two\n"));
  ASSERT_EQ(result.keymap.bindings.size(), 2u);
  EXPECT_EQ(result.keymap.bindings[0].line, 3);
  EXPECT_EQ(result.keymap.bindings[1].line, 4);
}

TEST(KeymapParserBindings, ModifierOrderCaseAndSpacingDoNotMatter) {
  const KeymapParseResult result = parseKeymap(
      QStringLiteral("SHIFT+Alt+Q:one\nctrl   +   win + F12 : two\nalt + Space : three"));
  ASSERT_TRUE(result.issues.empty()) << allIssues(result).toStdString();
  EXPECT_NE(findBinding(result, kAlt | kShift, 'Q'), nullptr);
  EXPECT_NE(findBinding(result, kCtrl | kWin, 0x7B), nullptr);
  EXPECT_NE(findBinding(result, kAlt, 0x20), nullptr);
}

TEST(KeymapParserBindings, ACommandMayContainColonsAndHashesInQuotes) {
  const KeymapParseResult result =
      parseKeymap(QStringLiteral("alt + a : Start-Process \"http://example.com/#top\" # open it\n"
                                 "alt + b : cmd /c echo a:b\n"));
  ASSERT_TRUE(result.issues.empty()) << allIssues(result).toStdString();
  EXPECT_EQ(result.keymap.bindings[0].command,
            QStringLiteral("Start-Process \"http://example.com/#top\""));
  EXPECT_EQ(result.keymap.bindings[1].command, QStringLiteral("cmd /c echo a:b"));
}

TEST(KeymapParserBindings, ACommandEndingInABracketIsNotABlock) {
  const KeymapParseResult result = parseKeymap(QStringLiteral("alt + a : echo [\nalt + b : x\n"));
  EXPECT_TRUE(result.issues.empty()) << allIssues(result).toStdString();
  EXPECT_EQ(result.keymap.bindings.size(), 2u);
}

TEST(KeymapParserErrors, UnknownKeyIsReportedWithItsLine) {
  const KeymapParseResult result = parseKeymap(QStringLiteral("alt + a : ok\nalt + banana : no\n"));
  EXPECT_EQ(result.keymap.bindings.size(), 1u);
  ASSERT_EQ(result.issues.size(), 1u);
  EXPECT_EQ(result.issues[0].line, 2);
  EXPECT_TRUE(result.issues[0].message.contains(QStringLiteral("banana")));
}

TEST(KeymapParserErrors, AGoodLineAfterABadOneStillLoads) {
  const KeymapParseResult result = parseKeymap(
      QStringLiteral("alt + a : one\nthis is not a binding\nalt + b : \nalt + c : three\n"));
  ASSERT_EQ(result.keymap.bindings.size(), 2u);
  EXPECT_EQ(result.keymap.bindings[1].command, QStringLiteral("three"));
  ASSERT_EQ(result.issues.size(), 2u);
  EXPECT_EQ(result.issues[0].line, 2);
  EXPECT_EQ(result.issues[1].line, 3);
  EXPECT_TRUE(result.issues[1].message.contains(QStringLiteral("empty command")));
}

TEST(KeymapParserErrors, TwoKeysAndNoKeyAreErrors) {
  const KeymapParseResult result =
      parseKeymap(QStringLiteral("alt + a + b : x\nalt + shift : y\n + a : z\n"));
  EXPECT_TRUE(result.keymap.bindings.empty());
  EXPECT_EQ(result.issues.size(), 3u);
}

TEST(KeymapParserErrors, DuplicateBindingKeepsTheFirstAndReportsTheSecond) {
  const KeymapParseResult result =
      parseKeymap(QStringLiteral("alt + a : first\nshift + alt + b : x\nalt + A : second\n"));
  ASSERT_EQ(result.keymap.bindings.size(), 2u);
  EXPECT_EQ(result.keymap.bindings[0].command, QStringLiteral("first"));
  ASSERT_EQ(result.issues.size(), 1u);
  EXPECT_EQ(result.issues[0].line, 3);
  EXPECT_TRUE(result.issues[0].message.contains(QStringLiteral("line 1")));
}

TEST(KeymapParserErrors, UnknownWakuActionIsAnErrorOfThatLine) {
  const KeymapParseResult result =
      parseKeymap(QStringLiteral("alt + a : waku:bogus\nalt + b : waku:reload\n"));
  EXPECT_EQ(result.keymap.bindings.size(), 1u);
  ASSERT_EQ(result.issues.size(), 1u);
  EXPECT_EQ(result.issues[0].line, 1);
}

TEST(KeymapParserErrors, EmptyAndCommentOnlyFilesAreFine) {
  EXPECT_TRUE(parseKeymap(QString()).issues.empty());
  const KeymapParseResult result = parseKeymap(QStringLiteral("\n# nothing\n   \n"));
  EXPECT_TRUE(result.issues.empty());
  EXPECT_TRUE(result.keymap.bindings.empty());
}

TEST(KeymapParserDirectives, ShellIsRead) {
  EXPECT_EQ(parseKeymap(QStringLiteral(".shell cmd")).keymap.shell, Shell::Cmd);
  EXPECT_EQ(parseKeymap(QStringLiteral(".shell pwsh # comment")).keymap.shell, Shell::Pwsh);
  EXPECT_EQ(parseKeymap(QStringLiteral(".SHELL PowerShell")).keymap.shell, Shell::Powershell);
  const KeymapParseResult bad = parseKeymap(QStringLiteral(".shell bash"));
  EXPECT_EQ(bad.issues.size(), 1u);
  EXPECT_EQ(bad.keymap.shell, Shell::Powershell);
}

TEST(KeymapParserDirectives, UnknownDirectiveIsReported) {
  const KeymapParseResult result = parseKeymap(QStringLiteral(".frobnicate 1\n"));
  ASSERT_EQ(result.issues.size(), 1u);
  EXPECT_EQ(result.issues[0].line, 1);
}

TEST(KeymapParserDirectives, PauseAndHook) {
  const KeymapParseResult result = parseKeymap(
      QStringLiteral(".pause alt + shift + p # toggles\n.pause_hook echo \"paused\"\n"));
  EXPECT_TRUE(result.issues.empty());
  ASSERT_TRUE(result.keymap.pause.has_value());
  EXPECT_EQ(result.keymap.pause->modifiers, kAlt | kShift);
  EXPECT_EQ(result.keymap.pause->virtualKey, static_cast<unsigned>('P'));
  EXPECT_EQ(result.keymap.pauseHook, QStringLiteral("echo \"paused\""));
}

TEST(KeymapParserDirectives, PauseOnABoundComboIsDropped) {
  const KeymapParseResult result = parseKeymap(QStringLiteral(".pause alt + a\nalt + a : x\n"));
  EXPECT_FALSE(result.keymap.pause.has_value());
  EXPECT_EQ(result.keymap.bindings.size(), 1u);
  EXPECT_EQ(result.issues.size(), 1u);
}

TEST(KeymapParserDirectives, BlacklistOnOneLine) {
  const KeymapParseResult result =
      parseKeymap(QStringLiteral(".blacklist [ \"Game.exe\", \"vlc.exe\" ]\nalt + a : x\n"));
  EXPECT_TRUE(result.issues.empty());
  EXPECT_EQ(result.keymap.blacklist,
            (QStringList{QStringLiteral("Game.exe"), QStringLiteral("vlc.exe")}));
}

TEST(KeymapParserDirectives, BlacklistOverSeveralLines) {
  const KeymapParseResult result = parseKeymap(
      QStringLiteral(".blacklist [\n  \"a.exe\", # first\n  \"b.exe\"\n]\nalt + a : x\n"));
  EXPECT_TRUE(result.issues.empty()) << allIssues(result).toStdString();
  EXPECT_EQ(result.keymap.blacklist,
            (QStringList{QStringLiteral("a.exe"), QStringLiteral("b.exe")}));
  EXPECT_EQ(result.keymap.bindings.size(), 1u);
}

TEST(KeymapParserDirectives, BlacklistWithoutAListIsAnError) {
  EXPECT_EQ(parseKeymap(QStringLiteral(".blacklist a.exe")).issues.size(), 1u);
  EXPECT_EQ(parseKeymap(QStringLiteral(".blacklist [ \"a.exe\"")).issues.size(), 1u);
}

TEST(KeymapParserBlocks, PerAppBlockWithDefaultAndIgnore) {
  const KeymapParseResult result =
      parseKeymap(QStringLiteral(".shell pwsh\n"
                                 "alt + n [\n"
                                 "    # ProcessName as shown by `Get-Process`\n"
                                 "    Firefox       : echo \"hello firefox\"\n"
                                 "\n"
                                 "    Google Chrome : echo \"hello chrome\"\n"
                                 "    Default       : komorebic close\n"
                                 "    Notepad       : Ignore\n"
                                 "]\n"
                                 "alt + h : after\n"));
  ASSERT_TRUE(result.issues.empty()) << allIssues(result).toStdString();
  ASSERT_EQ(result.keymap.bindings.size(), 2u);
  const Binding& block = result.keymap.bindings[0];
  EXPECT_TRUE(block.command.isEmpty());
  ASSERT_EQ(block.rules.size(), 4u);
  EXPECT_EQ(block.rules[0].process, QStringLiteral("Firefox"));
  EXPECT_EQ(block.rules[0].command, QStringLiteral("echo \"hello firefox\""));
  EXPECT_EQ(block.rules[1].process, QStringLiteral("Google Chrome"));
  EXPECT_TRUE(block.rules[2].isDefault);
  EXPECT_TRUE(block.rules[3].ignore);
  EXPECT_EQ(result.keymap.bindings[1].command, QStringLiteral("after"));
}

TEST(KeymapParserBlocks, BadRuleKeepsTheRestOfTheBlock) {
  const KeymapParseResult result =
      parseKeymap(QStringLiteral("alt + n [\n  oops\n  Firefox : x\n]\n"));
  ASSERT_EQ(result.keymap.bindings.size(), 1u);
  EXPECT_EQ(result.keymap.bindings[0].rules.size(), 1u);
  ASSERT_EQ(result.issues.size(), 1u);
  EXPECT_EQ(result.issues[0].line, 2);
}

TEST(KeymapParserBlocks, BadKeysDropTheBlockButNotTheNextBinding) {
  const KeymapParseResult result =
      parseKeymap(QStringLiteral("alt + nope [\n  Firefox : x\n]\nalt + a : ok\n"));
  EXPECT_EQ(result.keymap.bindings.size(), 1u);
  ASSERT_EQ(result.issues.size(), 1u);
  EXPECT_EQ(result.issues[0].line, 1);
}

TEST(KeymapParserBlocks, UnclosedBlockIsDropped) {
  const KeymapParseResult result = parseKeymap(QStringLiteral("alt + n [\n  Firefox : x\n"));
  EXPECT_TRUE(result.keymap.bindings.empty());
  ASSERT_EQ(result.issues.size(), 1u);
  EXPECT_EQ(result.issues[0].line, 1);
}

TEST(KeymapParserKeys, NamesOfTheKeyTable) {
  const struct {
    const char* name;
    unsigned vk;
  } cases[] = {
      {"a", 'A'},          {"Z", 'Z'},          {"0", '0'},           {"9", '9'},
      {"f1", 0x70},        {"F24", 0x87},       {"left", 0x25},       {"up", 0x26},
      {"right", 0x27},     {"down", 0x28},      {"tab", 0x09},        {"space", 0x20},
      {"return", 0x0D},    {"enter", 0x0D},     {"escape", 0x1B},     {"home", 0x24},
      {"end", 0x23},       {"pageup", 0x21},    {"prior", 0x21},      {"pagedown", 0x22},
      {"next", 0x22},      {"insert", 0x2D},    {"delete", 0x2E},     {"numpad0", 0x60},
      {"numpad9", 0x69},   {"multiply", 0x6A},  {"oem_1", 0xBA},      {"oem_plus", 0xBB},
      {"oem_comma", 0xBC}, {"oem_minus", 0xBD}, {"oem_period", 0xBE}, {"oem_2", 0xBF},
      {"oem_3", 0xC0},     {"oem_4", 0xDB},     {"oem_5", 0xDC},      {"oem_6", 0xDD},
      {"oem_7", 0xDE},     {"oem_8", 0xDF},     {"volume_up", 0xAF},  {"media_play_pause", 0xB3}};
  for (const auto& entry : cases) {
    const std::optional<unsigned> key = virtualKeyForName(QString::fromLatin1(entry.name));
    ASSERT_TRUE(key.has_value()) << entry.name;
    EXPECT_EQ(*key, entry.vk) << entry.name;
  }
}

TEST(KeymapParserKeys, UnknownNamesAndModifiersAreNotKeys) {
  EXPECT_FALSE(virtualKeyForName(QStringLiteral("banana")).has_value());
  EXPECT_FALSE(virtualKeyForName(QStringLiteral("f25")).has_value());
  EXPECT_FALSE(virtualKeyForName(QStringLiteral("alt")).has_value());
  EXPECT_FALSE(virtualKeyForName(QString()).has_value());
  EXPECT_FALSE(virtualKeyForName(QStringLiteral("!")).has_value());
}

TEST(KeymapParserKeys, ModifierAliases) {
  EXPECT_EQ(modifierForName(QStringLiteral("LAlt")), 1u);
  EXPECT_EQ(modifierForName(QStringLiteral("control")), 2u);
  EXPECT_EQ(modifierForName(QStringLiteral("rshift")), 4u);
  EXPECT_EQ(modifierForName(QStringLiteral("win")), 8u);
  EXPECT_FALSE(modifierForName(QStringLiteral("a")).has_value());
}

TEST(KeymapParserComments, StripComment) {
  EXPECT_EQ(stripComment(QStringLiteral("abc # x")), QStringLiteral("abc"));
  EXPECT_EQ(stripComment(QStringLiteral("# x")), QString());
  EXPECT_EQ(stripComment(QStringLiteral("a#b")), QStringLiteral("a#b"));
  EXPECT_EQ(stripComment(QStringLiteral("\"a # b\" # c")), QStringLiteral("\"a # b\""));
  EXPECT_EQ(stripComment(QStringLiteral("'a # b'")), QStringLiteral("'a # b'"));
}

// The key file the repository ships (examples/config/keys.conf) loads without an issue and binds
// every command a user needs to replace komorebi and whkd.
TEST(KeymapParserShipped, TheExampleKeyFileLoadsWithoutIssues) {
  QFile file(QStringLiteral(WAKU_EXAMPLES_DIR "/config/keys.conf"));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
  const KeymapParseResult result = parseKeymap(QString::fromUtf8(file.readAll()));
  EXPECT_TRUE(result.issues.empty()) << allIssues(result).toStdString();
  EXPECT_EQ(result.keymap.shell, Shell::Cmd);
  // Counted so a lost line shows.
  EXPECT_EQ(result.keymap.bindings.size(), 51u);
  QStringList commands;
  for (const Binding& binding : result.keymap.bindings) commands << binding.command;
  for (const char* wanted :
       {"wakuc tile close", "wakuc tile focus left", "wakuc tile move down", "wakuc tile promote",
        "wakuc tile resize horizontal increase", "wakuc tile monocle", "wakuc tile pause",
        "wakuc tile flip vertical", "wakuc desktop goto 8", "wakuc desktop move 1",
        "wakuc desktop previous", "wakuc desktop next"}) {
    EXPECT_TRUE(commands.contains(QString::fromLatin1(wanted))) << wanted;
  }
}
