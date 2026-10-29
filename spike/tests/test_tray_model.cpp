#include <gtest/gtest.h>

#include <QSignalSpy>
#include <memory>

#include "spike/services/tray_icon_model.h"
#include "spike/services/tray_image_provider.h"

using namespace tidy::services;

namespace {

platform::TrayIconData makeIcon(const QString& exe, int size = 16, char fill = 'x') {
  platform::TrayIconData d;
  d.hWnd = 0x1234;
  d.exe = exe;
  d.szTip = QStringLiteral("tip ") + exe;
  d.iconWidth = size;
  d.iconHeight = size;
  d.iconRgba = QByteArray(size * size * 4, fill);
  return d;
}

QVariant role(const TrayIconModel& m, int row, TrayIconModel::Role r) {
  return m.data(m.index(row), r);
}

}  // namespace

TEST(TrayModel, AddsRowsInArrivalOrder) {
  TrayIconModel m;
  QSignalSpy inserted(&m, &QAbstractItemModel::rowsInserted);
  QSignalSpy count(&m, &TrayIconModel::countChanged);
  m.apply(7, makeIcon(QStringLiteral("a.exe")));
  m.apply(3, makeIcon(QStringLiteral("b.exe")));
  EXPECT_EQ(m.rowCount(), 2);
  EXPECT_EQ(inserted.count(), 2);
  EXPECT_EQ(count.count(), 2);
  EXPECT_EQ(role(m, 0, TrayIconModel::IdRole).toInt(), 7);
  EXPECT_EQ(role(m, 1, TrayIconModel::IdRole).toInt(), 3);
  EXPECT_EQ(m.rowOfId(3), 1);
  EXPECT_EQ(m.rowOfId(99), -1);
}

TEST(TrayModel, Roles) {
  TrayIconModel m;
  platform::TrayIconData d = makeIcon(QStringLiteral("a.exe"));
  d.hWnd = 255;
  m.apply(5, d);
  EXPECT_EQ(role(m, 0, TrayIconModel::TooltipRole).toString(), QStringLiteral("tip a.exe"));
  EXPECT_EQ(role(m, 0, TrayIconModel::ExeRole).toString(), QStringLiteral("a.exe"));
  EXPECT_EQ(role(m, 0, TrayIconModel::HwndRole).toString(), QStringLiteral("255"));
  EXPECT_FALSE(role(m, 0, TrayIconModel::HasGuidRole).toBool());
  EXPECT_EQ(role(m, 0, TrayIconModel::IconUrlRole).toString(), QStringLiteral("image://tray/5/1"));
  EXPECT_FALSE(m.roleNames().isEmpty());
  EXPECT_FALSE(role(m, 5, TrayIconModel::IdRole).isValid());
}

TEST(TrayModel, KeyIsGuidOrExePath) {
  TrayIconModel m;
  platform::TrayIconData withGuid = makeIcon(QStringLiteral("a.exe"));
  withGuid.guid = QUuid::fromString(QStringLiteral("{11111111-2222-3333-4444-555555555555}"));
  platform::TrayIconData noGuid = makeIcon(QStringLiteral("b.exe"));
  noGuid.exePath = QStringLiteral("C:\\x\\b.exe");
  m.apply(1, withGuid);
  m.apply(2, noGuid);
  EXPECT_TRUE(role(m, 0, TrayIconModel::HasGuidRole).toBool());
  EXPECT_EQ(role(m, 0, TrayIconModel::KeyRole).toString(),
            withGuid.guid->toString(QUuid::WithoutBraces));
  EXPECT_EQ(role(m, 1, TrayIconModel::KeyRole).toString(), noGuid.exePath);
}

TEST(TrayModel, UpdateEmitsDataChangedAndBumpsRevisionOnlyForNewPixels) {
  TrayIconModel m;
  m.apply(1, makeIcon(QStringLiteral("a.exe"), 16, 'x'));
  EXPECT_EQ(m.revision(1), 1);
  const QString url = role(m, 0, TrayIconModel::IconUrlRole).toString();

  QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
  QSignalSpy inserted(&m, &QAbstractItemModel::rowsInserted);
  platform::TrayIconData same = makeIcon(QStringLiteral("a.exe"), 16, 'x');
  same.szTip = QStringLiteral("new tip");
  m.apply(1, same);
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(inserted.count(), 0);
  EXPECT_EQ(m.rowCount(), 1);
  EXPECT_EQ(m.revision(1), 1);
  EXPECT_EQ(role(m, 0, TrayIconModel::IconUrlRole).toString(), url);
  EXPECT_EQ(role(m, 0, TrayIconModel::TooltipRole).toString(), QStringLiteral("new tip"));

  m.apply(1, makeIcon(QStringLiteral("a.exe"), 16, 'y'));
  EXPECT_EQ(changed.count(), 2);
  EXPECT_EQ(m.revision(1), 2);
  EXPECT_EQ(role(m, 0, TrayIconModel::IconUrlRole).toString(), QStringLiteral("image://tray/1/2"));
}

TEST(TrayModel, NoPixelsMeansNoUrl) {
  TrayIconModel m;
  platform::TrayIconData d;
  d.exe = QStringLiteral("a.exe");
  m.apply(1, d);
  EXPECT_TRUE(role(m, 0, TrayIconModel::IconUrlRole).toString().isEmpty());
  EXPECT_TRUE(m.image(1).isNull());
}

TEST(TrayModel, RemoveRows) {
  TrayIconModel m;
  m.apply(1, makeIcon(QStringLiteral("a.exe")));
  m.apply(2, makeIcon(QStringLiteral("b.exe")));
  QSignalSpy removed(&m, &QAbstractItemModel::rowsRemoved);
  QSignalSpy count(&m, &TrayIconModel::countChanged);
  EXPECT_FALSE(m.remove(99));
  EXPECT_EQ(removed.count(), 0);
  EXPECT_TRUE(m.remove(1));
  EXPECT_EQ(removed.count(), 1);
  EXPECT_EQ(count.count(), 1);
  EXPECT_EQ(m.rowCount(), 1);
  EXPECT_EQ(role(m, 0, TrayIconModel::IdRole).toInt(), 2);
  EXPECT_EQ(m.iconData(1), nullptr);
  ASSERT_NE(m.iconData(2), nullptr);
  EXPECT_EQ(m.iconData(2)->exe, QStringLiteral("b.exe"));
}

TEST(TrayImageProvider, ReturnsScaledImageAndEmptyForUnknown) {
  auto model = std::make_unique<TrayIconModel>();
  model->apply(4, makeIcon(QStringLiteral("a.exe"), 16));
  TrayImageProvider provider(model.get());

  QSize size;
  const QImage img = provider.requestImage(QStringLiteral("4/1"), &size, QSize());
  EXPECT_EQ(img.size(), QSize(32, 32));
  EXPECT_EQ(size, QSize(32, 32));

  EXPECT_TRUE(provider.requestImage(QStringLiteral("9/1"), &size, QSize()).isNull());
  EXPECT_TRUE(provider.requestImage(QStringLiteral("abc/1"), &size, QSize()).isNull());

  model.reset();
  EXPECT_TRUE(provider.requestImage(QStringLiteral("4/1"), &size, QSize()).isNull());
}
