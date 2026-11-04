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
        QStringList{QStringLiteral("focus"), QStringLiteral("up")},
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
