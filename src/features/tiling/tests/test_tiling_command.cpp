// `wakuc tile ...`: the parser of the `tile` verb on a fake TilingControl. wakuc forwards the verb
// as typed, so no client code is involved.
#include <gtest/gtest.h>

#include "features/tiling/tiling_command.h"

using waku::tiling::answerTileCommand;
using waku::tiling::TilingControl;

namespace {

class FakeControl final : public TilingControl {
 public:
  QString controlFloat(std::optional<bool> floating) override {
    calls << QStringLiteral("float|%1").arg(floating ? (*floating ? "yes" : "no") : "toggle");
    return QStringLiteral("ok: fake");
  }
  QString controlLayout(const QString& name) override {
    calls << QStringLiteral("layout|%1").arg(name);
    return QStringLiteral("ok: fake");
  }
  QString controlRetile() override {
    calls << QStringLiteral("retile");
    return QStringLiteral("ok: fake");
  }
  QString controlFocus(int direction) override {
    calls << QStringLiteral("focus|%1").arg(direction);
    return QStringLiteral("ok: fake");
  }
  QString controlStatus() override {
    calls << QStringLiteral("status");
    return QStringLiteral("ok: fake");
  }
  QString controlFocusDirection(waku::tiling::Direction direction) override {
    calls << QStringLiteral("focusdir|%1").arg(static_cast<int>(direction));
    return QStringLiteral("ok: fake");
  }
  QString controlMove(waku::tiling::Direction direction) override {
    calls << QStringLiteral("move|%1").arg(static_cast<int>(direction));
    return QStringLiteral("ok: fake");
  }
  QString controlPromote() override {
    calls << QStringLiteral("promote");
    return QStringLiteral("ok: fake");
  }
  QString controlStack(waku::tiling::Direction direction) override {
    calls << QStringLiteral("stack|%1").arg(static_cast<int>(direction));
    return QStringLiteral("ok: fake");
  }
  QString controlUnstack() override {
    calls << QStringLiteral("unstack");
    return QStringLiteral("ok: fake");
  }
  QString controlCycleStack(int step) override {
    calls << QStringLiteral("cycle|%1").arg(step);
    return QStringLiteral("ok: fake");
  }
  QString controlResize(waku::tiling::Axis axis, int sign) override {
    calls << QStringLiteral("resize|%1|%2").arg(static_cast<int>(axis)).arg(sign);
    return QStringLiteral("ok: fake");
  }
  QString controlFlip(waku::tiling::Axis axis) override {
    calls << QStringLiteral("flip|%1").arg(static_cast<int>(axis));
    return QStringLiteral("ok: fake");
  }
  QString controlMonocle() override {
    calls << QStringLiteral("monocle");
    return QStringLiteral("ok: fake");
  }
  QString controlPause() override {
    calls << QStringLiteral("pause");
    return QStringLiteral("ok: fake");
  }
  QString controlClose() override {
    calls << QStringLiteral("close");
    return QStringLiteral("ok: fake");
  }
  QString controlMinimize() override {
    calls << QStringLiteral("minimize");
    return QStringLiteral("ok: fake");
  }
  QStringList calls;
};

}  // namespace

TEST(TilingCommand, EachVerbReachesTheRightFunction) {
  FakeControl control;
  answerTileCommand({QStringLiteral("toggle")}, &control);
  answerTileCommand({QStringLiteral("float")}, &control);
  answerTileCommand({QStringLiteral("layout"), QStringLiteral("columns")}, &control);
  answerTileCommand({QStringLiteral("retile")}, &control);
  answerTileCommand({QStringLiteral("focus"), QStringLiteral("next")}, &control);
  answerTileCommand({QStringLiteral("focus"), QStringLiteral("previous")}, &control);
  answerTileCommand({QStringLiteral("status")}, &control);
  EXPECT_EQ(control.calls, (QStringList{QStringLiteral("float|toggle"), QStringLiteral("float|yes"),
                                        QStringLiteral("layout|columns"), QStringLiteral("retile"),
                                        QStringLiteral("focus|1"), QStringLiteral("focus|-1"),
                                        QStringLiteral("status")}));
}

TEST(TilingCommand, BadUsageAnswersAnErrorAndCallsNothing) {
  FakeControl control;
  for (const QStringList& words :
       {QStringList{}, QStringList{QStringLiteral("toggle"), QStringLiteral("x")},
        QStringList{QStringLiteral("layout")}, QStringList{QStringLiteral("focus")},
        QStringList{QStringLiteral("focus"), QStringLiteral("sideways")},
        QStringList{QStringLiteral("move")},
        QStringList{QStringLiteral("move"), QStringLiteral("next")},
        QStringList{QStringLiteral("resize"), QStringLiteral("horizontal")},
        QStringList{QStringLiteral("resize"), QStringLiteral("diagonal"),
                    QStringLiteral("increase")},
        QStringList{QStringLiteral("resize"), QStringLiteral("vertical"), QStringLiteral("grow")},
        QStringList{QStringLiteral("flip")},
        QStringList{QStringLiteral("flip"), QStringLiteral("up")},
        QStringList{QStringLiteral("close"), QStringLiteral("now")},
        QStringList{QStringLiteral("stack")},
        QStringList{QStringLiteral("stack"), QStringLiteral("in")},
        QStringList{QStringLiteral("unstack"), QStringLiteral("all")},
        QStringList{QStringLiteral("cycle-stack")},
        QStringList{QStringLiteral("cycle-stack"), QStringLiteral("left")},
        QStringList{QStringLiteral("explode")}}) {
    const QString answer = answerTileCommand(words, &control);
    EXPECT_TRUE(answer.startsWith(QLatin1String("error: usage:"))) << answer.toStdString();
  }
  EXPECT_TRUE(control.calls.isEmpty());
}

TEST(TilingCommand, ANullTilerAnswersAnError) {
  EXPECT_EQ(answerTileCommand({QStringLiteral("status")}, nullptr),
            QStringLiteral("error: the tiler is not available"));
}

TEST(TilingCommand, VerbsIgnoreCase) {
  FakeControl control;
  answerTileCommand({QStringLiteral("TOGGLE")}, &control);
  EXPECT_EQ(control.calls, QStringList{QStringLiteral("float|toggle")});
}

TEST(TilingCommand, TheWindowCommandsReachTheRightFunction) {
  FakeControl control;
  auto run = [&control](std::initializer_list<const char*> words) {
    QStringList list;
    for (const char* word : words) list << QString::fromLatin1(word);
    return answerTileCommand(list, &control);
  };
  run({"focus", "left"});
  run({"focus", "DOWN"});
  run({"move", "right"});
  run({"move", "up"});
  run({"promote"});
  run({"stack", "left"});
  run({"unstack"});
  run({"cycle-stack", "previous"});
  run({"cycle-stack", "NEXT"});
  run({"resize", "horizontal", "increase"});
  run({"resize", "vertical", "decrease"});
  run({"flip", "horizontal"});
  run({"flip", "vertical"});
  run({"monocle"});
  run({"pause"});
  run({"close"});
  run({"minimize"});
  using waku::tiling::Axis;
  using waku::tiling::Direction;
  EXPECT_EQ(
      control.calls,
      (QStringList{QStringLiteral("focusdir|%1").arg(static_cast<int>(Direction::Left)),
                   QStringLiteral("focusdir|%1").arg(static_cast<int>(Direction::Down)),
                   QStringLiteral("move|%1").arg(static_cast<int>(Direction::Right)),
                   QStringLiteral("move|%1").arg(static_cast<int>(Direction::Up)),
                   QStringLiteral("promote"),
                   QStringLiteral("stack|%1").arg(static_cast<int>(Direction::Left)),
                   QStringLiteral("unstack"), QStringLiteral("cycle|-1"), QStringLiteral("cycle|1"),
                   QStringLiteral("resize|%1|1").arg(static_cast<int>(Axis::Horizontal)),
                   QStringLiteral("resize|%1|-1").arg(static_cast<int>(Axis::Vertical)),
                   QStringLiteral("flip|%1").arg(static_cast<int>(Axis::Horizontal)),
                   QStringLiteral("flip|%1").arg(static_cast<int>(Axis::Vertical)),
                   QStringLiteral("monocle"), QStringLiteral("pause"), QStringLiteral("close"),
                   QStringLiteral("minimize")}));
}
