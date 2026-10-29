#include <gtest/gtest.h>

#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <memory>
#include <vector>

#include "spike/services/tray_icon_model.h"
#include "spike/services/tray_pins.h"
#include "widgets/systray/systray_state.h"

using namespace tidy::services;

namespace {

QString exePath(const char* name) {
  return QStringLiteral("C:\\apps\\") + QString::fromLatin1(name) + QStringLiteral(".exe");
}

platform::TrayIconData makeIcon(const char* name) {
  platform::TrayIconData d;
  d.hWnd = 0x1234;
  d.exe = QString::fromLatin1(name) + QStringLiteral(".exe");
  d.exePath = exePath(name);
  d.szTip = QString::fromLatin1(name);
  d.iconWidth = 16;
  d.iconHeight = 16;
  d.iconRgba = QByteArray(16 * 16 * 4, 'x');
  return d;
}

std::vector<int> idsOf(const QAbstractItemModel& m) {
  std::vector<int> out;
  for (int r = 0; r < m.rowCount(); ++r) {
    out.push_back(m.data(m.index(r, 0), TrayIconModel::IdRole).toInt());
  }
  return out;
}

QStringList exesOf(const QAbstractItemModel& m) {
  QStringList out;
  for (int r = 0; r < m.rowCount(); ++r) {
    out.push_back(m.data(m.index(r, 0), TrayIconModel::ExeRole).toString());
  }
  return out;
}

// Follows a list model through its signals the way a view does and checks it ends at rowCount.
class ViewTracker {
 public:
  explicit ViewTracker(QAbstractItemModel* m) : model_(m), rows_(m->rowCount()) {
    QObject::connect(m, &QAbstractItemModel::rowsInserted, m,
                     [this](const QModelIndex&, int a, int b) { rows_ += b - a + 1; });
    QObject::connect(m, &QAbstractItemModel::rowsRemoved, m,
                     [this](const QModelIndex&, int a, int b) { rows_ -= b - a + 1; });
    QObject::connect(m, &QAbstractItemModel::modelReset, m, [this] { rows_ = model_->rowCount(); });
  }
  bool consistent() const { return rows_ == model_->rowCount(); }

 private:
  QAbstractItemModel* model_;
  int rows_;
};

class TrayPinsTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.isValid()); }

  QString stateFile(const QString& screen = QStringLiteral("None")) const {
    return yasb::widgets::SystrayStateStore(dir_.path(), screen).path();
  }
  void writeRaw(const QByteArray& text, const QString& screen = QStringLiteral("None")) {
    QFile f(stateFile(screen));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(text);
  }
  static QByteArray entry(const char* key, bool pinned, int index) {
    return QByteArray("\"") + QString(exePath(key)).replace("\\", "\\\\").toUtf8() +
           "\": {\"is_pinned\": " + (pinned ? "true" : "false") +
           ", \"index\": " + QByteArray::number(index) + "}";
  }
  yasb::widgets::IconStateMap loadState(const QString& screen = QStringLiteral("None")) const {
    return yasb::widgets::SystrayStateStore(dir_.path(), screen).load();
  }

  // A fresh model and pins on the temp dir, filled with `names` (ids 1..n in that order).
  struct Setup {
    TrayIconModel model;
    TrayPins pins;
  };
  std::unique_ptr<Setup> make(const std::vector<const char*>& names,
                              const QString& screen = QString()) {
    auto s = std::make_unique<Setup>();
    s->pins.setStateDir(dir_.path());
    if (!screen.isEmpty()) s->pins.setScreenId(screen);
    s->pins.setIcons(&s->model);
    int id = 1;
    for (const char* name : names) s->model.apply(id++, makeIcon(name));
    return s;
  }

  QTemporaryDir dir_;
};

}  // namespace

TEST_F(TrayPinsTest, RestoresPinnedAndUnpinnedFromTheFile) {
  writeRaw("{" + entry("a", true, 0) + "," + entry("b", false, 0) + "," + entry("c", true, 1) +
           "}");
  auto s = make({"a", "b", "c"});
  EXPECT_EQ(exesOf(*s->pins.pinned()), (QStringList{"a.exe", "c.exe"}));
  EXPECT_EQ(exesOf(*s->pins.unpinned()), (QStringList{"b.exe"}));
  EXPECT_TRUE(s->pins.isPinned(1));
  EXPECT_FALSE(s->pins.isPinned(2));
  EXPECT_EQ(s->pins.pinned()->property("count").toInt(), 2);
}

TEST_F(TrayPinsTest, SavedIndexOrdersIconsThatArriveShuffled) {
  writeRaw("{" + entry("a", true, 0) + "," + entry("b", true, 1) + "," + entry("c", true, 2) + "," +
           entry("d", false, 0) + "," + entry("e", false, 1) + "}");
  auto s = make({"c", "e", "a", "d", "b"});
  EXPECT_EQ(exesOf(*s->pins.pinned()), (QStringList{"a.exe", "b.exe", "c.exe"}));
  EXPECT_EQ(exesOf(*s->pins.unpinned()), (QStringList{"d.exe", "e.exe"}));
}

TEST_F(TrayPinsTest, UnknownIconIsUnpinnedAtTheEnd) {
  writeRaw("{" + entry("a", false, 0) + "," + entry("b", false, 1) + "}");
  auto s = make({"x", "b", "y", "a"});
  EXPECT_TRUE(s->pins.pinned()->rowCount() == 0);
  // Saved ones first by index, unknown ones after them in arrival order.
  EXPECT_EQ(exesOf(*s->pins.unpinned()), (QStringList{"a.exe", "b.exe", "x.exe", "y.exe"}));
}

TEST_F(TrayPinsTest, ArrivalAndRemovalDoNotSave) {
  auto s = make({"a", "b"});
  s->model.remove(1);
  EXPECT_FALSE(QFile::exists(stateFile()));
}

TEST_F(TrayPinsTest, ChangesUpdateTheListsAndPersist) {
  auto s = make({"a", "b", "c", "d"});  // all unpinned: a b c d
  s->pins.setPinned(3, true);
  s->pins.setPinned(1, true);
  EXPECT_EQ(idsOf(*s->pins.pinned()), (std::vector<int>{3, 1}));
  EXPECT_EQ(idsOf(*s->pins.unpinned()), (std::vector<int>{2, 4}));

  s->pins.move(4, true, 1);  // pin d between c and a
  EXPECT_EQ(idsOf(*s->pins.pinned()), (std::vector<int>{3, 4, 1}));
  s->pins.move(1, true, -5);  // within the list, clamped to the front
  EXPECT_EQ(idsOf(*s->pins.pinned()), (std::vector<int>{1, 3, 4}));
  s->pins.moveBy(1, 1);
  EXPECT_EQ(idsOf(*s->pins.pinned()), (std::vector<int>{3, 1, 4}));
  s->pins.moveBy(4, 5);  // clamped: already last
  EXPECT_EQ(idsOf(*s->pins.pinned()), (std::vector<int>{3, 1, 4}));
  s->pins.move(3, false, 99);  // unpin to the end
  EXPECT_EQ(idsOf(*s->pins.pinned()), (std::vector<int>{1, 4}));
  EXPECT_EQ(idsOf(*s->pins.unpinned()), (std::vector<int>{2, 3}));
  s->pins.setPinned(2, false);  // already unpinned: nothing
  EXPECT_EQ(idsOf(*s->pins.unpinned()), (std::vector<int>{2, 3}));

  auto fresh = make({"a", "b", "c", "d"});
  EXPECT_EQ(idsOf(*fresh->pins.pinned()), idsOf(*s->pins.pinned()));
  EXPECT_EQ(idsOf(*fresh->pins.unpinned()), idsOf(*s->pins.unpinned()));
  // And the same when the icons arrive in another order.
  auto shuffled = make({"d", "c", "b", "a"});
  EXPECT_EQ(exesOf(*shuffled->pins.pinned()), exesOf(*s->pins.pinned()));
  EXPECT_EQ(exesOf(*shuffled->pins.unpinned()), exesOf(*s->pins.unpinned()));
}

TEST_F(TrayPinsTest, MoveByAtTheEndsIsANoOpAndDoesNotSave) {
  auto s = make({"a", "b"});
  s->pins.moveBy(1, -1);
  s->pins.moveBy(2, 1);
  s->pins.moveBy(99, 1);
  EXPECT_EQ(idsOf(*s->pins.unpinned()), (std::vector<int>{1, 2}));
  EXPECT_FALSE(QFile::exists(stateFile()));
}

TEST_F(TrayPinsTest, IconsOfOneProgramAreSeparateRowsWithSharedState) {
  auto s = make({"a", "a", "b"});  // ids 1 and 2 share the key C:\apps\a.exe
  EXPECT_EQ(s->pins.unpinned()->rowCount(), 3);
  s->pins.setPinned(1, true);
  EXPECT_EQ(idsOf(*s->pins.pinned()), (std::vector<int>{1, 2}));
  EXPECT_EQ(idsOf(*s->pins.unpinned()), (std::vector<int>{3}));
  ASSERT_NE(loadState().find(exePath("a")), nullptr);
  EXPECT_TRUE(loadState().find(exePath("a"))->isPinned);

  auto fresh = make({"a", "a", "b"});
  EXPECT_EQ(idsOf(*fresh->pins.pinned()), (std::vector<int>{1, 2}));
  EXPECT_EQ(idsOf(*fresh->pins.unpinned()), (std::vector<int>{3}));
}

TEST_F(TrayPinsTest, RemovedIconReturnsInItsSavedPlace) {
  auto s = make({"a", "b", "c"});
  s->pins.setPinned(1, true);
  s->pins.setPinned(2, true);
  s->pins.setPinned(3, true);
  EXPECT_EQ(idsOf(*s->pins.pinned()), (std::vector<int>{1, 2, 3}));

  s->model.remove(2);
  EXPECT_EQ(idsOf(*s->pins.pinned()), (std::vector<int>{1, 3}));
  s->pins.moveBy(3, -1);  // a save while b is away
  EXPECT_EQ(idsOf(*s->pins.pinned()), (std::vector<int>{3, 1}));
  EXPECT_NE(loadState().find(exePath("b")), nullptr);

  s->model.apply(9, makeIcon("b"));  // new id, same program
  EXPECT_EQ(exesOf(*s->pins.pinned()), (QStringList{"c.exe", "b.exe", "a.exe"}));
  EXPECT_TRUE(s->pins.isPinned(9));
}

TEST_F(TrayPinsTest, OtherKeysInTheFileSurviveASave) {
  writeRaw("{" + entry("other", true, 0) + "}");
  auto s = make({"a"});
  s->pins.setPinned(1, true);
  const auto state = loadState();
  ASSERT_NE(state.find(exePath("other")), nullptr);
  EXPECT_TRUE(state.find(exePath("other"))->isPinned);
  ASSERT_NE(state.find(exePath("a")), nullptr);
  EXPECT_TRUE(state.find(exePath("a"))->isPinned);
}

TEST_F(TrayPinsTest, MissingAndCorruptFilesStartEmpty) {
  auto missing = make({"a", "b"});
  EXPECT_EQ(missing->pins.pinned()->rowCount(), 0);
  EXPECT_EQ(missing->pins.unpinned()->rowCount(), 2);

  writeRaw("{ this is not json");
  auto corrupt = make({"a", "b"});
  EXPECT_EQ(corrupt->pins.pinned()->rowCount(), 0);
  EXPECT_EQ(corrupt->pins.unpinned()->rowCount(), 2);
  corrupt->pins.setPinned(1, true);  // still saves
  EXPECT_TRUE(loadState().find(exePath("a"))->isPinned);
}

TEST_F(TrayPinsTest, ScreenIdChangeReloadsTheState) {
  writeRaw("{" + entry("a", true, 0) + "}", QStringLiteral("LEFT"));
  writeRaw("{" + entry("b", true, 0) + "}", QStringLiteral("RIGHT"));
  auto s = make({"a", "b"});
  EXPECT_EQ(s->pins.pinned()->rowCount(), 0);  // "None": no file

  QSignalSpy changed(&s->pins, &TrayPins::screenIdChanged);
  s->pins.setScreenId(QStringLiteral("LEFT"));
  EXPECT_EQ(exesOf(*s->pins.pinned()), (QStringList{"a.exe"}));
  s->pins.setScreenId(QStringLiteral("RIGHT"));
  EXPECT_EQ(exesOf(*s->pins.pinned()), (QStringList{"b.exe"}));
  EXPECT_EQ(exesOf(*s->pins.unpinned()), (QStringList{"a.exe"}));
  EXPECT_EQ(changed.count(), 2);

  s->pins.setPinned(1, true);  // saves into the RIGHT file only
  EXPECT_TRUE(loadState(QStringLiteral("RIGHT")).find(exePath("a"))->isPinned);
  EXPECT_FALSE(loadState(QStringLiteral("LEFT")).find(exePath("b")));
}

TEST_F(TrayPinsTest, SourceChangesForwardToTheMatchingRow) {
  auto s = make({"a", "b"});
  s->pins.setPinned(2, true);
  QSignalSpy pinnedChanged(s->pins.pinned(), &QAbstractItemModel::dataChanged);
  QSignalSpy unpinnedChanged(s->pins.unpinned(), &QAbstractItemModel::dataChanged);
  platform::TrayIconData d = makeIcon("b");
  d.szTip = QStringLiteral("new tip");
  s->model.apply(2, d);
  EXPECT_EQ(pinnedChanged.count(), 1);
  EXPECT_EQ(unpinnedChanged.count(), 0);
  const QAbstractItemModel& pinned = *s->pins.pinned();
  EXPECT_EQ(pinned.data(pinned.index(0, 0), TrayIconModel::TooltipRole).toString(),
            QStringLiteral("new tip"));
  EXPECT_FALSE(pinned.roleNames().isEmpty());
}

TEST_F(TrayPinsTest, ViewsSeeConsistentRowCounts) {
  writeRaw("{" + entry("a", true, 0) + "," + entry("b", false, 0) + "}");
  TrayIconModel model;
  TrayPins pins;
  ViewTracker pinnedView(pins.pinned());
  ViewTracker unpinnedView(pins.unpinned());
  QSignalSpy inserted(pins.pinned(), &QAbstractItemModel::rowsInserted);
  QSignalSpy removed(pins.pinned(), &QAbstractItemModel::rowsRemoved);
  QSignalSpy moved(pins.pinned(), &QAbstractItemModel::rowsMoved);
  QSignalSpy count(pins.pinned(), &TrayPinList::countChanged);
  pins.setStateDir(dir_.path());
  pins.setIcons(&model);

  model.apply(1, makeIcon("a"));
  model.apply(2, makeIcon("b"));
  model.apply(3, makeIcon("c"));
  EXPECT_EQ(inserted.count(), 1);  // only a is pinned
  EXPECT_TRUE(pinnedView.consistent());
  EXPECT_TRUE(unpinnedView.consistent());

  pins.setPinned(3, true);
  pins.moveBy(3, -1);
  EXPECT_EQ(moved.count(), 1);
  pins.setPinned(2, true);
  pins.move(1, false, 0);
  EXPECT_TRUE(pinnedView.consistent());
  EXPECT_TRUE(unpinnedView.consistent());

  model.remove(3);
  model.remove(2);
  EXPECT_EQ(removed.count() >= 1, true);
  EXPECT_EQ(pins.pinned()->rowCount(), 0);
  EXPECT_EQ(pins.unpinned()->rowCount(), 1);
  EXPECT_TRUE(pinnedView.consistent());
  EXPECT_TRUE(unpinnedView.consistent());

  pins.setScreenId(QStringLiteral("OTHER"));  // reset
  EXPECT_TRUE(pinnedView.consistent());
  EXPECT_TRUE(unpinnedView.consistent());
  EXPECT_GT(count.count(), 0);
}

TEST_F(TrayPinsTest, HiddenRoleIsForwardedByTheLists) {
  auto s = make({"a"});
  auto d = makeIcon("a");
  d.uFlags = platform::kNifState;
  d.dwState = 1;
  s->model.apply(1, d);
  const QAbstractItemModel& list = *s->pins.unpinned();
  EXPECT_TRUE(list.data(list.index(0, 0), TrayIconModel::HiddenRole).toBool());
  EXPECT_TRUE(list.roleNames().values().contains("hidden"));
}

TEST_F(TrayPinsTest, ScreenDerivesTheScreenIdAndReloads) {
  QScreen* screen = QGuiApplication::primaryScreen();
  ASSERT_NE(screen, nullptr);
  const QString id =
      yasb::widgets::cleanScreenId(screen->manufacturer(), screen->name(), screen->serialNumber());
  // The offscreen screen may clean to an empty id, which is the "None" file.
  writeRaw("{" + entry("a", true, 0) + "}", id.isEmpty() ? QStringLiteral("None") : id);
  TrayIconModel model;
  TrayPins pins;
  pins.setStateDir(dir_.path());
  pins.setIcons(&model);
  model.apply(1, makeIcon("a"));

  QSignalSpy changed(&pins, &TrayPins::screenChanged);
  pins.setScreen(screen);
  EXPECT_EQ(pins.screen(), screen);
  EXPECT_EQ(pins.screenId(), id);
  EXPECT_EQ(changed.count(), 1);
  EXPECT_TRUE(pins.isPinned(1));

  pins.setScreen(nullptr);
  EXPECT_EQ(pins.screenId(), QStringLiteral("None"));
  EXPECT_EQ(pins.screen(), nullptr);
  if (!id.isEmpty()) EXPECT_FALSE(pins.isPinned(1));  // the "None" file is empty

  pins.setScreenId(QStringLiteral("X"));  // the plain property still works
  EXPECT_EQ(pins.screenId(), QStringLiteral("X"));
}

TEST_F(TrayPinsTest, WindowSuppliesTheScreen) {
  QWindow window;
  window.setScreen(QGuiApplication::primaryScreen());
  TrayPins pins;
  pins.setStateDir(dir_.path());
  QSignalSpy changed(&pins, &TrayPins::windowChanged);
  pins.setWindow(&window);
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(pins.window(), &window);
  EXPECT_EQ(pins.screen(), QGuiApplication::primaryScreen());
  pins.setWindow(nullptr);
  EXPECT_EQ(pins.screen(), nullptr);
  EXPECT_EQ(pins.screenId(), QStringLiteral("None"));
}
