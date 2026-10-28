#include <gtest/gtest.h>

#include <QFontDatabase>

#include "spike/tests/bar_fixtures.h"

namespace {

QString pillConfig(const QString& barProperties = QString()) {
  return QStringLiteral(R"(
import QtQuick
import barnacle
import barnacle.widgets
Config {
    Bar {
        %1
        layout: BarLayout {
            start: [ Pill { objectName: "p"; text: "x" } ]
        }
    }
}
)")
      .arg(barProperties);
}

QString themeText(const QString& properties) {
  return QStringLiteral("import barnacle\nThemeSpec { %1 }\n").arg(properties);
}

QColor colorOf(Harness& h, const QString& name, const char* property) {
  QQuickItem* i = findItem(h.window(), name);
  return i ? i->property(property).value<QColor>() : QColor();
}

Theme* themeOf(Harness& h) {
  return h.engine.singletonInstance<Theme*>(QStringLiteral("barnacle"), QStringLiteral("Theme"));
}

void writeTheme(Harness& h, const QString& text) {
  write(h.dir.filePath(QStringLiteral("theme.qml")), text);
}

const QColor kDefaultSurface(0x10, 0x10, 0x18, 0x66);
const QColor kDefaultFill(0xff, 0xff, 0xff, 0x22);

}  // namespace

// ---- the values ----

TEST(ThemeValuesTest, DefaultsAreTheShippedLook) {
  const ThemeSpec spec;
  EXPECT_EQ(spec.property("text").value<QColor>(), QColor(0xff, 0xff, 0xff));
  EXPECT_EQ(spec.property("surface").value<QColor>(), kDefaultSurface);
  EXPECT_EQ(spec.property("accent").value<QColor>(), QColor(0x5b, 0x8d, 0xef));
  EXPECT_EQ(spec.property("fill").value<QColor>(), kDefaultFill);
  EXPECT_EQ(spec.property("fontSize").toInt(), 13);
  EXPECT_EQ(spec.property("smallFontSize").toInt(), 12);
  EXPECT_EQ(spec.property("pillHeight").toInt(), 24);
  EXPECT_EQ(spec.property("radius").toInt(), -1);
  EXPECT_EQ(spec.property("padding").toInt(), 10);
  EXPECT_EQ(spec.property("spacing").toInt(), 12);
}

TEST(ThemeValuesTest, TheDefaultFontIsTheSystemFont) {
  const ThemeSpec spec;
  EXPECT_FALSE(spec.property("fontFamily").toString().isEmpty());
  EXPECT_EQ(spec.property("fontFamily").toString(),
            QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
}

TEST(ThemeValuesTest, DerivedFillsKeepTheColourAndSetTheAlpha) {
  ThemeSpec spec;
  spec.setProperty("accent", QColor(10, 20, 30));
  const QColor accentFill = spec.property("accentFill").value<QColor>();
  EXPECT_EQ(accentFill.red(), 10);
  EXPECT_EQ(accentFill.green(), 20);
  EXPECT_EQ(accentFill.blue(), 30);
  EXPECT_EQ(accentFill.alpha(), 0x88);
  EXPECT_EQ(spec.property("warnFill").value<QColor>().alpha(), 0x55);
  EXPECT_EQ(spec.property("goodFill").value<QColor>().alpha(), 0x33);
  EXPECT_EQ(spec.property("cautionFill").value<QColor>().alpha(), 0x33);
}

TEST(ThemeValuesTest, ChangingAPropertyEmitsChanged) {
  ThemeSpec spec;
  QSignalSpy changed(&spec, &ThemeSpec::changed);
  spec.setProperty("accent", QColor(1, 2, 3));
  EXPECT_GE(changed.count(), 1);
  const int after = changed.count();
  spec.setProperty("accent", QColor(1, 2, 3));  // same value: nothing to announce
  EXPECT_EQ(changed.count(), after);
}

TEST(ThemeValuesTest, AssignFromCopiesWritablePropertiesAndTheDerivedFillsFollow) {
  ThemeSpec source;
  source.setProperty("accent", QColor(1, 2, 3));
  source.setProperty("fontSize", 19);
  source.setProperty("fontFamily", QStringLiteral("Test Family"));
  Theme theme;
  QSignalSpy changed(&theme, &Theme::changed);
  theme.assignFrom(source);
  EXPECT_EQ(theme.property("accent").value<QColor>(), QColor(1, 2, 3));
  EXPECT_EQ(theme.property("fontSize").toInt(), 19);
  EXPECT_EQ(theme.property("fontFamily").toString(), QStringLiteral("Test Family"));
  EXPECT_EQ(theme.property("accentFill").value<QColor>().red(), 1);
  EXPECT_GE(changed.count(), 1);
}

TEST(ThemeValuesTest, AssigningDefaultsRestoresThem) {
  Theme theme;
  theme.setProperty("accent", QColor(1, 2, 3));
  theme.setProperty("spacing", 99);
  theme.assignFrom(ThemeSpec());
  EXPECT_EQ(theme.property("accent").value<QColor>(), QColor(0x5b, 0x8d, 0xef));
  EXPECT_EQ(theme.property("spacing").toInt(), 12);
}

// ---- theme.qml and the singleton ----

TEST(ThemeFile, ASpecSetsOnlyWhatItNames) {
  QQmlEngine engine;
  QTemporaryDir dir;
  write(dir.filePath(QStringLiteral("theme.qml")),
        themeText(QStringLiteral("accent: \"#ff8800\"; fontSize: 15")));
  QQmlComponent component(&engine, QUrl::fromLocalFile(dir.filePath(QStringLiteral("theme.qml"))));
  ASSERT_FALSE(component.isError()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> root(component.create());
  auto* spec = qobject_cast<ThemeSpec*>(root.get());
  ASSERT_NE(spec, nullptr);
  EXPECT_EQ(spec->property("accent").value<QColor>(), QColor(QStringLiteral("#ff8800")));
  EXPECT_EQ(spec->property("fontSize").toInt(), 15);
  EXPECT_EQ(spec->property("smallFontSize").toInt(), 12);  // untouched: the default
}

TEST(ThemeFile, TheSingletonExistsWithDefaults) {
  Harness h;
  Theme* theme = themeOf(h);
  ASSERT_NE(theme, nullptr);
  EXPECT_EQ(theme->property("accent").value<QColor>(), QColor(0x5b, 0x8d, 0xef));
  EXPECT_EQ(themeOf(h), theme);  // one per engine
}

// ---- the host applies it ----

TEST(ThemeHost, WithoutAThemeFileTheDefaultsApply) {
  Harness h;
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  settle();
  EXPECT_EQ(colorOf(h, QStringLiteral("background"), "color"), kDefaultSurface);
  EXPECT_EQ(colorOf(h, QStringLiteral("p"), "fill"), kDefaultFill);
}

TEST(ThemeHost, AThemeFileRestylesTheBarAndItsWidgets) {
  Harness h;
  writeTheme(h,
             themeText(QStringLiteral(
                 "surface: \"#80102030\"; fill: \"#40ff0000\"; text: \"#00ff00\"; spacing: 30")));
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  settle();
  EXPECT_EQ(colorOf(h, QStringLiteral("background"), "color"), QColor(0x10, 0x20, 0x30, 0x80));
  EXPECT_EQ(colorOf(h, QStringLiteral("p"), "fill"), QColor(0xff, 0x00, 0x00, 0x40));
  EXPECT_EQ(colorOf(h, QStringLiteral("p"), "textColor"), QColor(0x00, 0xff, 0x00));
  EXPECT_EQ(findItem(h.window(), QStringLiteral("layout"))->childItems().size() > 0, true);
}

TEST(ThemeHost, ABarsOwnBackgroundBeatsTheThemeSurface) {
  Harness h;
  writeTheme(h, themeText(QStringLiteral("surface: \"#80102030\"")));
  h.start(pillConfig(QStringLiteral("background: \"#ff112233\"")));
  ASSERT_TRUE(h.host->reload());
  settle();
  EXPECT_EQ(colorOf(h, QStringLiteral("background"), "color"), QColor(0x11, 0x22, 0x33));
}

TEST(ThemeHost, TheLayoutSpacingFollowsTheTheme) {
  Harness h;
  writeTheme(h, themeText(QStringLiteral("spacing: 31")));
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  settle();
  QQuickItem* layout = findItem(h.window(), QStringLiteral("layout"))->childItems().value(0);
  ASSERT_NE(layout, nullptr);
  EXPECT_EQ(layout->property("spacing").toInt(), 31);
}

TEST(ThemeHost, ChangingTheThemeFileAndReloadingRestyles) {
  Harness h;
  writeTheme(h, themeText(QStringLiteral("fill: \"#40ff0000\"")));
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(colorOf(h, QStringLiteral("p"), "fill"), QColor(0xff, 0x00, 0x00, 0x40));

  writeTheme(h, themeText(QStringLiteral("fill: \"#4000ff00\"")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(colorOf(h, QStringLiteral("p"), "fill"), QColor(0x00, 0xff, 0x00, 0x40));
}

TEST(ThemeHost, RemovingTheThemeFileRestoresTheDefaults) {
  Harness h;
  writeTheme(h, themeText(QStringLiteral("fill: \"#40ff0000\"")));
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  ASSERT_TRUE(QFile::remove(h.dir.filePath(QStringLiteral("theme.qml"))));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(colorOf(h, QStringLiteral("p"), "fill"), kDefaultFill);
}

TEST(ThemeHost, TheThemeFileIsWatched) {
  Harness h;
  writeTheme(h, themeText(QStringLiteral("fill: \"#40ff0000\"")));
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  h.host->watch();
  QSignalSpy reloaded(h.host.get(), &BarHost::reloaded);

  writeTheme(h, themeText(QStringLiteral("fill: \"#4000ff00\"")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloaded.count() >= 1; }, 5000));
  EXPECT_EQ(colorOf(h, QStringLiteral("p"), "fill"), QColor(0x00, 0xff, 0x00, 0x40));
}

TEST(ThemeHost, WidgetsFollowAChangeToTheSingletonWithoutAReload) {
  Harness h;
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  themeOf(h)->setProperty("fill", QColor(1, 2, 3, 4));
  EXPECT_EQ(colorOf(h, QStringLiteral("p"), "fill"), QColor(1, 2, 3, 4));
}

TEST(ThemeHost, ABrokenThemeKeepsThePreviousBarsAndTheme) {
  Harness h;
  writeTheme(h, themeText(QStringLiteral("fill: \"#40ff0000\"")));
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  QQuickWindow* before = h.window();

  writeTheme(h, QStringLiteral("import barnacle\nThemeSpec { fill: "));
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().startsWith(QStringLiteral("theme.qml:")))
      << qPrintable(h.host->error());
  EXPECT_EQ(h.window(), before);
  EXPECT_EQ(colorOf(h, QStringLiteral("p"), "fill"), QColor(0xff, 0x00, 0x00, 0x40));
  EXPECT_EQ(themeOf(h)->property("fill").value<QColor>(), QColor(0xff, 0x00, 0x00, 0x40));
}

TEST(ThemeHost, ARootThatIsNotAThemeSpecIsAnError) {
  Harness h;
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  writeTheme(h, QStringLiteral("import QtQuick\nItem {}\n"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("theme.qml must declare a ThemeSpec")))
      << qPrintable(h.host->error());
}

TEST(ThemeHost, FixingTheThemeClearsTheError) {
  Harness h;
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  writeTheme(h, QStringLiteral("not qml {{{"));
  ASSERT_FALSE(h.host->reload());
  writeTheme(h, themeText(QStringLiteral("fill: \"#40ff0000\"")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->error().isEmpty());
  EXPECT_EQ(colorOf(h, QStringLiteral("p"), "fill"), QColor(0xff, 0x00, 0x00, 0x40));
}

// ---- how a widget uses the size and radius ----

TEST(ThemeHost, ThePillHeightAndRadiusComeFromTheTheme) {
  Harness h;
  writeTheme(h, themeText(QStringLiteral("pillHeight: 30; radius: 4")));
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  settle();
  QQuickItem* pill = findItem(h.window(), QStringLiteral("p"));
  ASSERT_NE(pill, nullptr);
  EXPECT_EQ(pill->implicitHeight(), 30);
  EXPECT_EQ(pill->property("radius").toInt(), 4);
}

TEST(ThemeHost, ANegativeRadiusIsAFullPillAndABigOneIsClamped) {
  Harness h;
  h.start(pillConfig());
  ASSERT_TRUE(h.host->reload());
  settle();
  QQuickItem* pill = findItem(h.window(), QStringLiteral("p"));
  ASSERT_NE(pill, nullptr);
  EXPECT_DOUBLE_EQ(pill->property("radius").toDouble(), pill->height() / 2);
  themeOf(h)->setProperty("radius", 500);
  EXPECT_DOUBLE_EQ(pill->property("radius").toDouble(), pill->height() / 2);
}
