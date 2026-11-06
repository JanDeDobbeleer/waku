// `wakuc desktop ...`: the parser of the `desktop` verb on a fake DesktopControl, and the service
// side of the commands on a fake VirtualDesktopSystem.
#include <gtest/gtest.h>

#include <memory>

#include "features/workspaces/desktop_command.h"
#include "features/workspaces/qml/virtual_desktops_service.h"
#include "features/workspaces/virtual_desktops.h"

using namespace waku::workspaces;

namespace {

class FakeControl final : public DesktopControl {
 public:
  QString controlGoto(int number) override {
    calls << QStringLiteral("goto|%1").arg(number);
    return QStringLiteral("ok: fake");
  }
  QString controlMove(int number) override {
    calls << QStringLiteral("move|%1").arg(number);
    return QStringLiteral("ok: fake");
  }
  QString controlStep(int direction) override {
    calls << QStringLiteral("step|%1").arg(direction);
    return QStringLiteral("ok: fake");
  }
  QString controlStatus() override {
    calls << QStringLiteral("status");
    return QStringLiteral("ok: fake");
  }
  QStringList calls;
};

QStringList words(std::initializer_list<const char*> list) {
  QStringList out;
  for (const char* word : list) out << QString::fromLatin1(word);
  return out;
}

class FakeSystem final : public vdesk::VirtualDesktopSystem {
 public:
  std::optional<vdesk::State> read() override { return state; }
  bool jumpTo(int index) override {
    jumps.push_back(index);
    return jumpWorks;
  }
  int create() override {
    ++created;
    return -1;
  }
  MoveResult moveForegroundWindow(int index) override {
    moves.push_back(index);
    return moveResult;
  }
  MoveResult moveWindow(quint64 hwnd, int index) override {
    windowMoves.emplace_back(hwnd, index);
    return moveResult;
  }

  std::optional<vdesk::State> state;
  std::vector<std::pair<quint64, int>> windowMoves;
  std::vector<int> jumps;
  std::vector<int> moves;
  int created = 0;
  bool jumpWorks = true;
  MoveResult moveResult = MoveResult::Moved;
};

struct Harness {
  explicit Harness(int current = 1) {
    auto fake = std::make_unique<FakeSystem>();
    system = fake.get();
    vdesk::State state;
    state.desktops = {{QStringLiteral("{A}"), QString(), {}},
                      {QStringLiteral("{B}"), QString(), {}},
                      {QStringLiteral("{C}"), QString(), {}}};
    state.current = current;
    system->state = state;
    desktops = std::make_unique<VirtualDesktops>(std::move(fake), nullptr);
  }
  FakeSystem* system = nullptr;
  std::unique_ptr<VirtualDesktops> desktops;
};

}  // namespace

TEST(DesktopCommand, EachVerbReachesTheRightFunction) {
  FakeControl control;
  answerDesktopCommand(words({"goto", "3"}), &control);
  answerDesktopCommand(words({"MOVE", "2"}), &control);
  answerDesktopCommand(words({"next"}), &control);
  answerDesktopCommand(words({"previous"}), &control);
  answerDesktopCommand(words({"status"}), &control);
  EXPECT_EQ(control.calls, (QStringList{QStringLiteral("goto|3"), QStringLiteral("move|2"),
                                        QStringLiteral("step|1"), QStringLiteral("step|-1"),
                                        QStringLiteral("status")}));
}

TEST(DesktopCommand, BadUsageAnswersAnErrorAndCallsNothing) {
  FakeControl control;
  for (const QStringList& bad :
       {QStringList{}, words({"goto"}), words({"goto", "0"}), words({"goto", "-1"}),
        words({"goto", "x"}), words({"goto", "1", "2"}), words({"move"}), words({"move", "1.5"}),
        words({"next", "1"}), words({"jump"})}) {
    const QString answer = answerDesktopCommand(bad, &control);
    EXPECT_TRUE(answer.startsWith(QLatin1String("error: usage:"))) << answer.toStdString();
  }
  EXPECT_TRUE(control.calls.isEmpty());
}

TEST(DesktopCommand, ANullServiceAnswersAnError) {
  EXPECT_EQ(answerDesktopCommand(words({"status"}), nullptr),
            QStringLiteral("error: virtual desktops are not available"));
}

TEST(DesktopCommandService, GotoJumpsOnceAndNeverCreates) {
  Harness h(0);
  EXPECT_TRUE(h.desktops->controlGoto(3).startsWith(QLatin1String("ok:")));
  EXPECT_EQ(h.system->jumps, std::vector<int>{2});
  EXPECT_TRUE(h.desktops->controlGoto(4).startsWith(QLatin1String("error:")));
  EXPECT_TRUE(h.desktops->controlGoto(1).startsWith(QLatin1String("ok:")));  // already shown
  EXPECT_EQ(h.system->jumps, std::vector<int>{2});
  EXPECT_EQ(h.system->created, 0);
}

TEST(DesktopCommandService, GotoReportsAFailedSwitch) {
  Harness h(0);
  h.system->jumpWorks = false;
  EXPECT_TRUE(h.desktops->controlGoto(2).startsWith(QLatin1String("error:")));
}

TEST(DesktopCommandService, NextAndPreviousStopAtTheEnds) {
  Harness h(1);
  EXPECT_TRUE(h.desktops->controlStep(1).startsWith(QLatin1String("ok:")));
  EXPECT_EQ(h.system->jumps, std::vector<int>{2});
  Harness last(2);
  EXPECT_TRUE(last.desktops->controlStep(1).contains(QLatin1String("unchanged")));
  EXPECT_TRUE(last.system->jumps.empty());
  Harness first(0);
  EXPECT_TRUE(first.desktops->controlStep(-1).contains(QLatin1String("unchanged")));
  EXPECT_TRUE(first.system->jumps.empty());
  EXPECT_EQ(first.system->created, 0);
}

TEST(DesktopCommandService, MoveSendsTheFocusedWindowAndDoesNotFollow) {
  Harness h(0);
  EXPECT_TRUE(h.desktops->controlMove(2).startsWith(QLatin1String("ok:")));
  EXPECT_EQ(h.system->moves, std::vector<int>{1});
  EXPECT_TRUE(h.system->jumps.empty());
  EXPECT_TRUE(h.desktops->controlMove(9).startsWith(QLatin1String("error:")));
  EXPECT_EQ(h.system->moves.size(), 1u);
}

TEST(DesktopCommandService, MoveToTheShownDesktopDoesNothing) {
  Harness h(1);
  EXPECT_TRUE(h.desktops->controlMove(2).startsWith(QLatin1String("ok:")));
  EXPECT_TRUE(h.system->moves.empty());
}

TEST(DesktopCommandService, MoveSaysWhyItFailed) {
  Harness h(0);
  using Result = vdesk::VirtualDesktopSystem::MoveResult;
  h.system->moveResult = Result::NoWindow;
  EXPECT_TRUE(h.desktops->controlMove(2).contains(QLatin1String("no focused window")));
  h.system->moveResult = Result::OwnWindow;
  EXPECT_TRUE(h.desktops->controlMove(2).contains(QLatin1String("waku")));
  h.system->moveResult = Result::Refused;
  EXPECT_TRUE(h.desktops->controlMove(2).startsWith(QLatin1String("error:")));
  h.system->moveResult = Result::Unsupported;
  EXPECT_TRUE(h.desktops->controlMove(2).startsWith(QLatin1String("error:")));
}

namespace {
// Only read() and jumpTo() and create() are implemented: the rest is the interface's default.
class BareSystem final : public vdesk::VirtualDesktopSystem {
 public:
  std::optional<vdesk::State> read() override { return std::nullopt; }
  bool jumpTo(int) override { return false; }
  int create() override { return -1; }
};
}  // namespace

TEST(DesktopCommandService, ASystemWithoutMoveSupportAnswersUnsupported) {
  BareSystem bare;
  EXPECT_EQ(bare.moveWindow(0x1234, 1), vdesk::VirtualDesktopSystem::MoveResult::Unsupported);
  EXPECT_EQ(bare.moveForegroundWindow(1), vdesk::VirtualDesktopSystem::MoveResult::Unsupported);
}

TEST(DesktopCommandService, AFakeRecordsTheWindowAndTheDesktopOfAMove) {
  Harness h(0);
  EXPECT_EQ(h.system->moveWindow(0x1234, 2), vdesk::VirtualDesktopSystem::MoveResult::Moved);
  h.system->moveResult = vdesk::VirtualDesktopSystem::MoveResult::Refused;
  EXPECT_EQ(h.system->moveWindow(0x99, 1), vdesk::VirtualDesktopSystem::MoveResult::Refused);
  ASSERT_EQ(h.system->windowMoves.size(), 2u);
  EXPECT_EQ(h.system->windowMoves[0], (std::pair<quint64, int>(0x1234, 2)));
  EXPECT_EQ(h.system->windowMoves[1], (std::pair<quint64, int>(0x99, 1)));
}

TEST(DesktopCommandService, StatusNamesTheShownDesktopAndTheCount) {
  Harness h(1);
  EXPECT_EQ(h.desktops->controlStatus(), QStringLiteral("ok: current=2; count=3"));
}

TEST(DesktopCommandService, WithoutASystemEveryCommandAnswersAnError) {
  VirtualDesktops none(nullptr, nullptr);
  EXPECT_TRUE(none.controlGoto(1).startsWith(QLatin1String("error:")));
  EXPECT_TRUE(none.controlMove(1).startsWith(QLatin1String("error:")));
  EXPECT_TRUE(none.controlStep(1).startsWith(QLatin1String("error:")));
  EXPECT_TRUE(none.controlStatus().startsWith(QLatin1String("error:")));
}
