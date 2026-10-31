// Overlay windows: a PanelWindow or FloatingWindow that is shown on demand (a launcher, an OSD).
// `visible`, show/hide/toggle and the shown/hidden signals, `hideDelay`, the lazy native window,
// layer and keyboard focus reaching the window hooks, dismissOnBlur and dismissOnEscape, the state
// across a reload, the `surface` control verbs, and examples/launcher. Offscreen; the native hooks
// are the recording fakes of the Harness, so no real window is raised and no focus moves.
//
// Not checkable offscreen: that the swap chain (RHI) does not exist before the first show (there is
// no swap chain on this platform; the test checks the platform window handle instead), and that
// Windows really moves the foreground (the hooks record the call; the live check is by hand).
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QWindow>
#include <cstdlib>

#include "host/qml/control_handler.h"
#include "host/testing/shell_fixtures.h"

namespace {

QString overlayShell(const QString& properties, const QString& extra = QString()) {
  return QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    PanelWindow {
        name: "launcher"
        %3
        implicitWidth: 200
        implicitHeight: 100
        %1
        Item {
            objectName: "content"
            anchors.fill: parent
            focus: true
            %2
        }
    }
}
)")
      .arg(properties, extra,
           properties.contains(QStringLiteral("visible:")) ? QString()
                                                           : QStringLiteral("visible: false"));
}

// Makes `window` the active window and waits for it (the offscreen platform supports it).
bool activate(QsWindow* window) {
  QWindow* backing = window->backingWindow();
  backing->requestActivate();
  return QTest::qWaitFor([&] { return backing->isActive(); }, 2000);
}

QsWindow* named(Harness& h, const QString& name = QStringLiteral("launcher"), int i = 0) {
  return h.host->namedWindows(name).value(i);
}

bool started(Harness& h, const QString& config) {
  h.start(config);
  return h.host->reload();
}

quintptr hwndOf(QsWindow* window) {
  return static_cast<quintptr>(window->backingWindow()->winId());
}

}  // namespace

// ---- the lazy native window ----

TEST(Overlays, AHiddenWindowHasNoNativeWindowUntilItIsFirstShown) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QString())));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  EXPECT_FALSE(window->visible());
  EXPECT_FALSE(window->backingWindowVisible());
  EXPECT_EQ(window->backingWindow()->handle(), nullptr);
  EXPECT_TRUE(h.native.noActivate.empty());
  EXPECT_TRUE(h.native.created.empty());
  EXPECT_TRUE(h.native.backdrops.empty());

  window->show();
  EXPECT_TRUE(window->backingWindowVisible());
  EXPECT_NE(window->backingWindow()->handle(), nullptr);
  // The native look is applied at the first show, not before.
  EXPECT_EQ(h.native.backdrops.size(), 1u);
}

TEST(Overlays, ABuiltWindowWithVisibleTrueShowsAtOnceWithoutTakingTheFocus) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("visible: true; keyboardFocus: "
                                                     "WlrKeyboardFocus.OnDemand"))));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  EXPECT_TRUE(window->backingWindowVisible());
  EXPECT_TRUE(h.native.foregrounded.empty());
}

// ---- show, hide, toggle and their signals ----

TEST(Overlays, ShowHideAndToggleChangeTheWindowAndFireTheSignals) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QString())));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  QSignalSpy shown(window, &QsWindow::shown);
  QSignalSpy hidden(window, &QsWindow::hidden);
  QSignalSpy changed(window, &QsWindow::visibleChanged);

  window->show();
  EXPECT_TRUE(window->visible());
  EXPECT_TRUE(window->backingWindowVisible());
  EXPECT_EQ(shown.count(), 1);
  window->show();  // already shown: nothing happens
  EXPECT_EQ(shown.count(), 1);

  window->hide();
  EXPECT_FALSE(window->visible());
  EXPECT_FALSE(window->backingWindowVisible());
  EXPECT_EQ(hidden.count(), 1);

  window->toggle();
  EXPECT_TRUE(window->backingWindowVisible());
  window->toggle();
  EXPECT_FALSE(window->backingWindowVisible());
  EXPECT_EQ(shown.count(), 2);
  EXPECT_EQ(hidden.count(), 2);
  EXPECT_EQ(changed.count(), 4);
}

TEST(Overlays, TheLayerAndTheKeyboardFocusReachTheWindowHooksAndFlags) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("layer: WlrLayer.Overlay; keyboardFocus: "
                                                     "WlrKeyboardFocus.OnDemand"))));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  window->show();
  const quintptr hwnd = hwndOf(window);
  ASSERT_EQ(h.native.raised.size(), 1u);
  EXPECT_EQ(h.native.raised[0], hwnd);
  ASSERT_FALSE(h.native.noActivate.empty());
  EXPECT_EQ(h.native.noActivate.back(), std::make_pair(hwnd, false));
  EXPECT_TRUE(window->backingWindow()->flags().testFlag(Qt::WindowStaysOnTopHint));
  EXPECT_FALSE(window->backingWindow()->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
  // A run-time show of a window with keyboard focus takes the foreground.
  ASSERT_EQ(h.native.foregrounded.size(), 1u);
  EXPECT_EQ(h.native.foregrounded[0], hwnd);
  // Hiding it asks for nothing; showing it again asks again.
  window->hide();
  EXPECT_EQ(h.native.foregrounded.size(), 1u);
  window->show();
  EXPECT_EQ(h.native.foregrounded.size(), 2u);
}

TEST(Overlays, KeyboardFocusNoneNeverActivatesAndBottomIsNotRaised) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("layer: WlrLayer.Bottom"))));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  window->show();
  ASSERT_FALSE(h.native.noActivate.empty());
  EXPECT_EQ(h.native.noActivate.back(), std::make_pair(hwndOf(window), true));
  EXPECT_TRUE(h.native.raised.empty());
  EXPECT_TRUE(h.native.foregrounded.empty());
  const Qt::WindowFlags flags = window->backingWindow()->flags();
  EXPECT_TRUE(flags.testFlag(Qt::WindowStaysOnBottomHint));
  EXPECT_TRUE(flags.testFlag(Qt::WindowDoesNotAcceptFocus));
}

TEST(Overlays, ChangingTheLayerOfAShownWindowStacksItAgain) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("layer: WlrLayer.Bottom"))));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  window->show();
  ASSERT_TRUE(h.native.raised.empty());
  window->setProperty("layer", WlrLayer::Overlay);
  EXPECT_EQ(h.native.raised.size(), 1u);
  window->setProperty("keyboardFocus", WlrKeyboardFocus::OnDemand);
  EXPECT_EQ(h.native.noActivate.back().second, false);
}

// ---- hideDelay ----

TEST(Overlays, HideDelayKeepsTheWindowUpWhileTheContentAnimatesOut) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("hideDelay: 120"))));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  window->show();
  QSignalSpy hidden(window, &QsWindow::hidden);

  window->hide();
  EXPECT_FALSE(window->visible());              // the property says hidden at once ...
  EXPECT_TRUE(window->backingWindowVisible());  // ... the window stays up for the animation
  EXPECT_TRUE(window->hidePending());
  EXPECT_TRUE(QTest::qWaitFor([&] { return !window->backingWindowVisible(); }, 2000));
  EXPECT_FALSE(window->hidePending());
  EXPECT_EQ(hidden.count(), 1);
}

TEST(Overlays, ShowingAgainDuringTheHideDelayCancelsTheHide) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("hideDelay: 80"))));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  window->show();
  QSignalSpy hidden(window, &QsWindow::hidden);
  QSignalSpy shown(window, &QsWindow::shown);
  window->hide();
  EXPECT_TRUE(window->hidePending());
  window->toggle();
  // The hide was called off: no timer is left to take the window down later.
  EXPECT_FALSE(window->hidePending());
  QCoreApplication::processEvents();
  EXPECT_TRUE(window->visible());
  EXPECT_TRUE(window->backingWindowVisible());
  EXPECT_EQ(hidden.count(), 0);
  EXPECT_EQ(shown.count(), 0);  // it never left the screen
}

TEST(Overlays, ZeroDelayHidesAtOnce) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QString())));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  window->show();
  window->hide();
  EXPECT_FALSE(window->backingWindowVisible());
}

// ---- dismissOnBlur and dismissOnEscape ----

TEST(Overlays, LosingTheActivationHidesAWindowThatDismissesOnBlur) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("keyboardFocus: WlrKeyboardFocus.OnDemand; "
                                                     "dismissOnBlur: true"))));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  window->show();
  QWindow* backing = window->backingWindow();
  backing->requestActivate();
  ASSERT_TRUE(QTest::qWaitFor([&] { return backing->isActive(); }, 2000));

  QWindow other;
  other.setGeometry(0, 0, 40, 40);
  other.show();
  other.requestActivate();
  EXPECT_TRUE(QTest::qWaitFor([&] { return !backing->isActive(); }, 2000));
  EXPECT_FALSE(window->visible());
  EXPECT_FALSE(window->backingWindowVisible());
}

TEST(Overlays, AWindowWithoutKeyboardFocusIsNeverDismissedByBlur) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("dismissOnBlur: true"))));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  window->show();
  activate(window);
  QWindow other;
  other.setGeometry(0, 0, 40, 40);
  other.show();
  other.requestActivate();
  settle();
  EXPECT_TRUE(window->visible());
  EXPECT_TRUE(window->backingWindowVisible());
}

TEST(Overlays, WithoutDismissOnBlurALostActivationKeepsTheWindow) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("keyboardFocus: WlrKeyboardFocus.OnDemand"))));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  window->show();
  QWindow* backing = window->backingWindow();
  backing->requestActivate();
  ASSERT_TRUE(QTest::qWaitFor([&] { return backing->isActive(); }, 2000));
  QWindow other;
  other.setGeometry(0, 0, 40, 40);
  other.show();
  other.requestActivate();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !backing->isActive(); }, 2000));
  EXPECT_TRUE(window->visible());
}

TEST(Overlays, EscapeHidesAWindowThatDismissesOnEscape) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("keyboardFocus: WlrKeyboardFocus.OnDemand; "
                                                     "dismissOnEscape: true"))));
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  window->show();
  activate(window);
  QTest::keyClick(window->backingWindow(), Qt::Key_A);
  EXPECT_TRUE(window->visible());
  QTest::keyClick(window->backingWindow(), Qt::Key_Escape);
  EXPECT_FALSE(window->visible());
  EXPECT_FALSE(window->backingWindowVisible());
}

TEST(Overlays, EscapeDoesNothingWithoutDismissOnEscapeAndAnItemCanTakeIt) {
  Harness plain;
  ASSERT_TRUE(started(plain, overlayShell(QString())));
  QsWindow* window = named(plain);
  ASSERT_NE(window, nullptr);
  window->show();
  QTest::keyClick(window->backingWindow(), Qt::Key_Escape);
  EXPECT_TRUE(window->visible());

  // An item that handles Escape keeps it: the window stays.
  Harness handled;
  ASSERT_TRUE(started(handled, overlayShell(QStringLiteral("dismissOnEscape: true"),
                                            QStringLiteral("Keys.onEscapePressed: (event) => "
                                                           "event.accepted = true"))));
  QsWindow* other = named(handled);
  ASSERT_NE(other, nullptr);
  other->show();
  ASSERT_TRUE(activate(other));
  ASSERT_NE(other->backingWindow()->activeFocusItem(), nullptr);
  QTest::keyClick(other->backingWindow(), Qt::Key_Escape);
  EXPECT_TRUE(other->visible());
}

// ---- the state across a reload ----

TEST(Overlays, AWindowShownAtRunTimeIsShownAgainAfterAReload) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("keyboardFocus: "
                                                     "WlrKeyboardFocus.OnDemand"))));
  named(h)->show();
  ASSERT_EQ(h.native.foregrounded.size(), 1u);

  write(h.shellFile(), overlayShell(QStringLiteral("keyboardFocus: WlrKeyboardFocus.OnDemand"),
                                    QStringLiteral("property int generation: 2")));
  ASSERT_TRUE(h.host->reload());
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  EXPECT_TRUE(window->visible());
  EXPECT_TRUE(window->backingWindowVisible());
  EXPECT_TRUE(findItem(window->backingWindow(), QStringLiteral("content"))
                  ->property("generation")
                  .isValid());
  // Re-showing after a reload must not grab the keyboard focus again.
  EXPECT_EQ(h.native.foregrounded.size(), 1u);
}

TEST(Overlays, AWindowHiddenAtRunTimeStaysHiddenAfterAReload) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QStringLiteral("visible: true"))));
  ASSERT_TRUE(named(h)->backingWindowVisible());
  named(h)->hide();

  ASSERT_TRUE(h.host->reload());
  ASSERT_NE(named(h), nullptr);
  EXPECT_FALSE(named(h)->visible());
  EXPECT_FALSE(named(h)->backingWindowVisible());
}

TEST(Overlays, AWindowNeverToggledFollowsItsDeclarationAfterAReload) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QString())));
  ASSERT_TRUE(h.host->reload());
  EXPECT_FALSE(named(h)->backingWindowVisible());
}

// ---- the control verbs ----

TEST(OverlayControl, ListShowHideToggleAndStatusAnswerForANamedWindow) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QString())));
  const ControlHandler handler = makeControlHandler(*h.host, false);

  EXPECT_EQ(handler(QStringLiteral("surface list")), QStringLiteral("ok: launcher=hidden"));
  EXPECT_EQ(handler(QStringLiteral("surface status launcher")),
            QStringLiteral("ok: launcher hidden"));
  EXPECT_EQ(handler(QStringLiteral("surface show launcher")), QStringLiteral("ok: launcher shown"));
  EXPECT_TRUE(named(h)->backingWindowVisible());
  EXPECT_EQ(handler(QStringLiteral("surface list")), QStringLiteral("ok: launcher=shown"));
  EXPECT_EQ(handler(QStringLiteral("surface toggle launcher")),
            QStringLiteral("ok: launcher hidden"));
  EXPECT_FALSE(named(h)->backingWindowVisible());
  EXPECT_EQ(handler(QStringLiteral("surface toggle launcher")),
            QStringLiteral("ok: launcher shown"));
  EXPECT_EQ(handler(QStringLiteral("surface hide launcher")),
            QStringLiteral("ok: launcher hidden"));
  EXPECT_FALSE(named(h)->backingWindowVisible());
}

TEST(OverlayControl, BadInputIsAnError) {
  Harness h;
  ASSERT_TRUE(started(h, overlayShell(QString())));
  const ControlHandler handler = makeControlHandler(*h.host, false);
  EXPECT_TRUE(handler(QStringLiteral("surface")).startsWith(QStringLiteral("error: usage:")));
  EXPECT_TRUE(handler(QStringLiteral("surface show")).startsWith(QStringLiteral("error: usage:")));
  EXPECT_TRUE(
      handler(QStringLiteral("surface list extra")).startsWith(QStringLiteral("error: usage:")));
  EXPECT_TRUE(handler(QStringLiteral("surface explode launcher"))
                  .startsWith(QStringLiteral("error: unknown surface command 'explode'")));
  EXPECT_EQ(handler(QStringLiteral("surface show nothing")),
            QStringLiteral("error: no surface named 'nothing' (known: launcher)"));
  EXPECT_TRUE(handler(QStringLiteral("surface show \"unterminated"))
                  .startsWith(QStringLiteral("error: unterminated quote")));
}

TEST(OverlayControl, AVerbAppliesToEveryWindowOfTheName) {
  Harness h;
  h.screenCount = 2;
  ASSERT_TRUE(started(h, QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    Variants {
        model: ScreenList.screens
        PanelWindow {
            required property ShellScreen modelData
            screen: modelData
            name: "osd"
            visible: false
            implicitWidth: 100
            implicitHeight: 40
        }
    }
}
)")));
  ASSERT_EQ(h.host->namedWindows(QStringLiteral("osd")).size(), 2);
  const ControlHandler handler = makeControlHandler(*h.host, false);
  EXPECT_EQ(handler(QStringLiteral("surface list")), QStringLiteral("ok: osd=hidden (2 windows)"));
  EXPECT_EQ(handler(QStringLiteral("surface show osd")),
            QStringLiteral("ok: osd shown (2 windows)"));
  for (QsWindow* window : h.host->namedWindows(QStringLiteral("osd"))) {
    EXPECT_TRUE(window->backingWindowVisible());
  }
  // Toggle hides all when any is shown.
  h.host->namedWindows(QStringLiteral("osd")).first()->hide();
  EXPECT_EQ(handler(QStringLiteral("surface toggle osd")),
            QStringLiteral("ok: osd hidden (2 windows)"));
}

TEST(OverlayControl, AWindowComponentWithItsOwnRequiredNameIsFoundByThatName) {
  // Caelestia's StyledWindow declares `required property string name`, which shadows wabar's.
  Harness h;
  write(h.dir.filePath(QStringLiteral("StyledWindow.qml")), QStringLiteral(R"(
import wabar
PanelWindow {
    required property string name
    implicitWidth: 200
    implicitHeight: 100
    visible: false
}
)"));
  ASSERT_TRUE(started(h, QStringLiteral(R"(
import wabar
ShellRoot {
    StyledWindow { name: "drawers" }
}
)")));
  const ControlHandler handler = makeControlHandler(*h.host, false);
  EXPECT_EQ(handler(QStringLiteral("surface list")), QStringLiteral("ok: drawers=hidden"));
  EXPECT_EQ(handler(QStringLiteral("surface show drawers")), QStringLiteral("ok: drawers shown"));
  EXPECT_TRUE(h.host->namedWindows(QStringLiteral("drawers")).value(0)->backingWindowVisible());
  EXPECT_EQ(handler(QStringLiteral("surface hide drawers")), QStringLiteral("ok: drawers hidden"));
}

TEST(OverlayControl, AWindowWithoutANameFallsBackToItsObjectName) {
  Harness h;
  ASSERT_TRUE(started(h, QStringLiteral(R"(
import wabar
ShellRoot {
    PanelWindow {
        objectName: "menu"
        implicitWidth: 200
        implicitHeight: 100
        visible: false
    }
}
)")));
  const ControlHandler handler = makeControlHandler(*h.host, false);
  EXPECT_EQ(handler(QStringLiteral("surface list")), QStringLiteral("ok: menu=hidden"));
  EXPECT_EQ(handler(QStringLiteral("surface toggle menu")), QStringLiteral("ok: menu shown"));
}

TEST(OverlayControl, UnnamedWindowsAreNotListedAndStatusCarriesThePid) {
  Harness h;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  const ControlHandler handler = makeControlHandler(*h.host, false);
  EXPECT_EQ(handler(QStringLiteral("surface list")), QStringLiteral("ok: none"));
  EXPECT_TRUE(
      handler(QStringLiteral("status"))
          .startsWith(QStringLiteral("running; pid=%1;").arg(QCoreApplication::applicationPid())));
}

// ---- examples/launcher ----

TEST(OverlayExample, TheLauncherLoadsHiddenAndTogglesThroughTheHostAndTheHandler) {
  QFile file(QStringLiteral(WABAR_EXAMPLES_DIR "/launcher/shell.qml"));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  Harness h;
  h.start(QString::fromUtf8(file.readAll()));
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->error().isEmpty()) << h.host->error().toStdString();
  QsWindow* window = named(h);
  ASSERT_NE(window, nullptr);
  EXPECT_FALSE(window->backingWindowVisible());
  EXPECT_EQ(window->backingWindow()->handle(), nullptr);
  EXPECT_EQ(window->property("layer").toInt(), WlrLayer::Overlay);
  EXPECT_EQ(window->property("keyboardFocus").toInt(), WlrKeyboardFocus::OnDemand);

  // Through the host.
  window->toggle();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(window->backingWindow()));
  settle();
  const QRect s = QGuiApplication::primaryScreen()->geometry();
  EXPECT_EQ(window->backingWindow()->geometry().size(), QSize(640, 420));
  EXPECT_LE(std::abs(window->backingWindow()->geometry().center().x() - s.center().x()), 1);
  EXPECT_NE(findItem(window->backingWindow(), QStringLiteral("query")), nullptr);
  EXPECT_NE(findItem(window->backingWindow(), QStringLiteral("results")), nullptr);
  EXPECT_EQ(h.native.foregrounded.size(), 1u);
  window->toggle();
  EXPECT_TRUE(QTest::qWaitFor([&] { return !window->backingWindowVisible(); }, 2000));

  // Through the control handler.
  const ControlHandler handler = makeControlHandler(*h.host, false);
  EXPECT_EQ(handler(QStringLiteral("surface toggle launcher")),
            QStringLiteral("ok: launcher shown"));
  EXPECT_TRUE(window->backingWindowVisible());
  EXPECT_EQ(h.native.foregrounded.size(), 2u);
  EXPECT_EQ(handler(QStringLiteral("surface toggle launcher")),
            QStringLiteral("ok: launcher hidden"));
  EXPECT_TRUE(QTest::qWaitFor([&] { return !window->backingWindowVisible(); }, 2000));
  // Escape closes it too.
  handler(QStringLiteral("surface show launcher"));
  QTest::keyClick(window->backingWindow(), Qt::Key_Escape);
  EXPECT_FALSE(window->visible());
}
