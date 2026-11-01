// The Quickshell compatibility modules made of QML: Quickshell.Widgets, Quickshell.Hyprland,
// Quickshell.Services.{UPower,Pipewire,SystemTray} and Quickshell.Bluetooth. The service-backed
// ones read their values from a replaceable backend object, so the tests give them fakes written
// in QML and check the mapping, the setters and the live updates.
#include <gtest/gtest.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <memory>

namespace {

struct Loaded {
  QQmlEngine engine;
  std::unique_ptr<QObject> root;

  explicit Loaded(const QByteArray& source) {
    QQmlComponent component(&engine);
    component.setData(source, QUrl(QStringLiteral("file:///modules.qml")));
    EXPECT_FALSE(component.isError()) << qPrintable(component.errorString());
    root.reset(component.create());
    EXPECT_NE(root, nullptr) << qPrintable(component.errorString());
  }

  QVariant get(const char* name) const { return root->property(name); }
  QObject* child(const char* objectName) const {
    return root->findChild<QObject*>(QString::fromLatin1(objectName));
  }
  void call(QObject* object, const char* method) {
    ASSERT_TRUE(QMetaObject::invokeMethod(object, method));
  }
};

}  // namespace

// ---- Quickshell.Widgets ----

TEST(QuickshellWidgets, WrappersSizeThemselvesToTheirChild) {
  Loaded l(R"(
import QtQuick
import Quickshell.Widgets
Item {
    property alias item: wi
    property alias rect: wr
    property alias area: wm
    property alias clip: cw
    WrapperItem {
        id: wi; margin: 4; leftMargin: 10
        Item { implicitWidth: 20; implicitHeight: 30 }
    }
    WrapperRectangle {
        id: wr; margin: 2; border.width: 3
        Item { implicitWidth: 20; implicitHeight: 30 }
    }
    WrapperMouseArea {
        id: wm; extraMargin: 1
        Item { implicitWidth: 8; implicitHeight: 8 }
    }
    ClippingWrapperRectangle {
        id: cw; margin: 5; radius: 6; color: "red"
        Item { implicitWidth: 40; implicitHeight: 10 }
    }
}
)");
  ASSERT_NE(l.root, nullptr);
  auto size = [&](const char* name) {
    QObject* o = l.get(name).value<QObject*>();
    return QSizeF(o->property("implicitWidth").toReal(), o->property("implicitHeight").toReal());
  };
  EXPECT_EQ(size("item"), QSizeF(10 + 20 + 4, 4 + 30 + 4));
  EXPECT_EQ(size("rect"), QSizeF(2 + 3 + 20 + 3 + 2, 2 + 3 + 30 + 3 + 2));  // border added
  EXPECT_EQ(size("area"), QSizeF(10, 10));
  EXPECT_EQ(size("clip"), QSizeF(50, 20));
}

TEST(QuickshellWidgets, TheChildIsPlacedAndFillsTheWrapper) {
  Loaded l(R"(
import QtQuick
import Quickshell.Widgets
WrapperItem {
    id: w
    margin: 5
    width: 100; height: 60
    property alias kid: k
    Rectangle { id: k; implicitWidth: 10; implicitHeight: 10 }
}
)");
  ASSERT_NE(l.root, nullptr);
  auto* kid = l.get("kid").value<QQuickItem*>();
  ASSERT_NE(kid, nullptr);
  EXPECT_EQ(kid->parentItem(), qobject_cast<QQuickItem*>(l.root.get()));
  EXPECT_EQ(kid->x(), 5);
  EXPECT_EQ(kid->y(), 5);
  EXPECT_EQ(kid->width(), 90);
  EXPECT_EQ(kid->height(), 50);
  l.root->setProperty("resizeChild", false);
  EXPECT_EQ(kid->width(), 10);
}

TEST(QuickshellWidgets, ClippingRectangleKeepsChildrenInItsContentItem) {
  Loaded l(R"(
import QtQuick
import Quickshell.Widgets
ClippingRectangle {
    width: 50; height: 50; radius: 8; color: "blue"; border.width: 2; border.color: "white"
    property alias kid: k
    Rectangle { id: k; anchors.fill: parent }
}
)");
  ASSERT_NE(l.root, nullptr);
  auto* kid = l.get("kid").value<QQuickItem*>();
  auto* content = l.get("contentItem").value<QQuickItem*>();
  ASSERT_NE(content, nullptr);
  EXPECT_EQ(kid->parentItem(), content);
  EXPECT_EQ(content->width(), 46);
  EXPECT_EQ(kid->width(), 46);  // inside the border
  EXPECT_TRUE(l.get("antialiasing").toBool());
  l.root->setProperty("contentUnderBorder", true);
  EXPECT_EQ(kid->width(), 50);
}

TEST(QuickshellWidgets, IconImageIsSquareAndResolvesPaths) {
  Loaded l(R"(
import QtQuick
import Quickshell.Widgets
IconImage {
    implicitSize: 24
    width: 40; height: 24
    source: "C:\\x\\missing.png"
    mipmap: true
    readonly property string resolved: resolve(source)
    readonly property real imageWidth: backer.width
}
)");
  ASSERT_NE(l.root, nullptr);
  EXPECT_EQ(l.get("actualSize").toReal(), 24);
  EXPECT_EQ(l.get("imageWidth").toReal(), 24);
  EXPECT_EQ(l.get("resolved").toString(), "file:///C:/x/missing.png");
  EXPECT_TRUE(l.get("mipmap").toBool());
  EXPECT_EQ(l.get("implicitWidth").toReal(), 24);
}

// ---- Quickshell.Hyprland ----

TEST(QuickshellHyprland, TheSingletonIsInert) {
  Loaded l(R"(
import QtQuick
import Quickshell.Hyprland
Item {
    property int workspaces: Hyprland.workspaces.values.length
    property int monitors: Hyprland.monitors.values.length
    property int toplevels: Hyprland.toplevels.values.length
    property bool noFocus: Hyprland.focusedWorkspace === null && Hyprland.focusedMonitor === null && Hyprland.activeToplevel === null
    property int events: 0
    GlobalShortcut { objectName: "gs"; name: "x" }
    Connections { target: Hyprland; function onRawEvent() { events++ } }
    Component.onCompleted: {
        Hyprland.dispatch("workspace 2");
        Hyprland.refreshWorkspaces();
        Hyprland.refreshMonitors();
        Hyprland.refreshToplevels();
    }
}
)");
  ASSERT_NE(l.root, nullptr);
  EXPECT_EQ(l.get("workspaces").toInt(), 0);
  EXPECT_EQ(l.get("monitors").toInt(), 0);
  EXPECT_EQ(l.get("toplevels").toInt(), 0);
  EXPECT_TRUE(l.get("noFocus").toBool());
  EXPECT_EQ(l.get("events").toInt(), 0);
  QObject* shortcut = l.child("gs");
  ASSERT_NE(shortcut, nullptr);
  EXPECT_EQ(shortcut->property("name").toString(), "x");
}

TEST(QuickshellHyprland, FocusGrabEndsWhenTheWindowsLoseTheActivation) {
  Loaded l(R"(
import QtQuick
import Quickshell.Hyprland
Item {
    property alias grab: g
    property int clearedCount: 0
    QtObject { id: w; objectName: "w"; property bool active: false; property bool visible: true }
    HyprlandFocusGrab {
        id: g
        windows: [w]
        graceInterval: 10
        onCleared: clearedCount++
    }
}
)");
  ASSERT_NE(l.root, nullptr);
  QObject* grab = l.get("grab").value<QObject*>();
  QObject* window = l.child("w");
  grab->setProperty("active", true);
  QTest::qWait(40);
  EXPECT_TRUE(grab->property("active").toBool());  // not armed: the window was never active
  window->setProperty("active", true);
  QTest::qWait(20);
  EXPECT_EQ(l.get("clearedCount").toInt(), 0);
  window->setProperty("active", false);
  QTRY_COMPARE_WITH_TIMEOUT(l.get("clearedCount").toInt(), 1, 1000);
  EXPECT_FALSE(grab->property("active").toBool());
}

TEST(QuickshellHyprland, FocusGrabEndsWhenEveryWindowIsHidden) {
  Loaded l(R"(
import QtQuick
import Quickshell.Hyprland
Item {
    property alias grab: g
    property int clearedCount: 0
    QtObject { id: w; objectName: "w"; property bool active: false; property bool visible: true }
    HyprlandFocusGrab { id: g; windows: [w]; graceInterval: 10; onCleared: clearedCount++ }
}
)");
  QObject* grab = l.get("grab").value<QObject*>();
  l.child("w")->setProperty("visible", false);
  grab->setProperty("active", true);
  QTRY_COMPARE_WITH_TIMEOUT(l.get("clearedCount").toInt(), 1, 1000);
}

// ---- Quickshell.Services.UPower ----

TEST(QuickshellUPower, DisplayDeviceFollowsTheBatteryService) {
  Loaded l(R"(
import QtQuick
import Quickshell.Services.UPower
Item {
    QtObject {
        id: fake; objectName: "fake"
        property bool available: true
        property int percent: 80
        property bool charging: false
        property bool plugged: false
        property int timeRemaining: 3600
        signal updated
    }
    Component.onCompleted: UPower.backend = fake
    property real pct: UPower.displayDevice.percentage
    property int st: UPower.displayDevice.state
    property bool present: UPower.displayDevice.isPresent
    property bool batMode: UPower.onBattery
    property int devices: UPower.devices.values.length
    property real toEmpty: UPower.displayDevice.timeToEmpty
    property bool laptop: UPower.displayDevice.isLaptopBattery
    property bool stateIsDischarging: UPower.displayDevice.state === UPowerDeviceState.Discharging
    property string stateName: UPowerDeviceState.toString(UPower.displayDevice.state)
}
)");
  ASSERT_NE(l.root, nullptr);
  QObject* fake = l.child("fake");
  EXPECT_DOUBLE_EQ(l.get("pct").toDouble(), 0.8);  // a fraction, as energy / energyCapacity
  EXPECT_TRUE(l.get("stateIsDischarging").toBool());
  EXPECT_EQ(l.get("stateName").toString(), "Discharging");
  EXPECT_TRUE(l.get("present").toBool());
  EXPECT_TRUE(l.get("batMode").toBool());
  EXPECT_EQ(l.get("devices").toInt(), 1);
  EXPECT_EQ(l.get("toEmpty").toInt(), 3600);
  EXPECT_TRUE(l.get("laptop").toBool());

  fake->setProperty("charging", true);
  fake->setProperty("plugged", true);
  fake->setProperty("percent", 100);
  l.call(fake, "updated");
  EXPECT_EQ(l.get("stateName").toString(), "Charging");
  EXPECT_DOUBLE_EQ(l.get("pct").toDouble(), 1.0);
  EXPECT_FALSE(l.get("batMode").toBool());
  EXPECT_EQ(l.get("toEmpty").toInt(), 0);

  fake->setProperty("available", false);
  l.call(fake, "updated");
  EXPECT_FALSE(l.get("present").toBool());
  EXPECT_EQ(l.get("devices").toInt(), 0);
}

TEST(QuickshellUPower, PowerProfilesMapTheWindowsModes) {
  Loaded l(R"(
import QtQuick
import Quickshell.Services.UPower
Item {
    QtObject {
        id: fake; objectName: "fake"
        property string last: ""
        property var powerModes: [
            { id: "{E1}", name: "Best power efficiency", active: false },
            { id: "{00000000-0000-0000-0000-000000000000}", name: "Balanced", active: true },
            { id: "{E3}", name: "Best performance", active: false }
        ]
        signal changed
        function setPowerMode(id) { last = id; return true }
    }
    Component.onCompleted: PowerProfiles.backend = fake
    property int profile: PowerProfiles.profile
    property bool hasPerf: PowerProfiles.hasPerformanceProfile
    function choose(p) { PowerProfiles.profile = p }
    property string profileName: PowerProfile.toString(PowerProfiles.profile)
}
)");
  ASSERT_NE(l.root, nullptr);
  EXPECT_EQ(l.get("profileName").toString(), "Balanced");
  EXPECT_TRUE(l.get("hasPerf").toBool());
  QObject* fake = l.child("fake");
  EXPECT_EQ(fake->property("last").toString(), "");  // reading the profile wrote nothing
  QVariant perf = 2;
  ASSERT_TRUE(QMetaObject::invokeMethod(l.root.get(), "choose", Q_ARG(QVariant, perf)));
  EXPECT_EQ(fake->property("last").toString(), "{E3}");
  QVariant saver = 0;
  ASSERT_TRUE(QMetaObject::invokeMethod(l.root.get(), "choose", Q_ARG(QVariant, saver)));
  EXPECT_EQ(fake->property("last").toString(), "{E1}");
}

// ---- Quickshell.Services.Pipewire ----

namespace {
const char* kPipewireQml = R"(
import QtQuick
import Quickshell
import Quickshell.Services.Pipewire
Item {
    QtObject {
        id: out; objectName: "out"
        property bool available: true
        property int volume: 40
        property bool muted: false
        property var devices: [{ id: "d1", name: "Speakers" }, { id: "d2", name: "Headset" }]
        property string defaultDevice: "d1"
        property string chosen: ""
        signal changed
        function setDefaultDevice(id) { chosen = id; return true }
    }
    QtObject {
        id: inp; objectName: "inp"
        property bool available: true
        property int volume: 90
        property bool muted: true
        property var devices: [{ id: "m1", name: "Mic" }]
        property string defaultDevice: "m1"
        signal changed
        function setDefaultDevice(id) { return true }
    }
    QtObject {
        id: stream; objectName: "stream"
        property string name: "Music"
        property string description: "Music"
        property int pid: 321
        property real volume: 0.5
        property bool muted: false
        property var properties: ({ "application.name": "Music", "application.icon_name": "music.exe" })
        signal changed
    }
    ScriptModel { id: sess; values: [stream] }
    Component.onCompleted: {
        Pipewire.output = out;
        Pipewire.input = inp;
        Pipewire.sessions = sess;
    }
    property var sink: Pipewire.defaultAudioSink
    property var source: Pipewire.defaultAudioSource
    property int nodeCount: Pipewire.nodes.values.length
}
)";
}

TEST(QuickshellPipewire, DefaultNodesReadAndWriteTheEndpoints) {
  Loaded l(kPipewireQml);
  ASSERT_NE(l.root, nullptr);
  QObject* sink = l.get("sink").value<QObject*>();
  QObject* source = l.get("source").value<QObject*>();
  ASSERT_NE(sink, nullptr);
  ASSERT_NE(source, nullptr);
  EXPECT_TRUE(sink->property("isSink").toBool());
  EXPECT_FALSE(sink->property("isStream").toBool());
  EXPECT_FALSE(source->property("isSink").toBool());
  EXPECT_EQ(sink->property("description").toString(), "Speakers");
  EXPECT_EQ(sink->property("properties").toMap().value("media.class").toString(), "Audio/Sink");
  QObject* audio = sink->property("audio").value<QObject*>();
  ASSERT_NE(audio, nullptr);
  EXPECT_DOUBLE_EQ(audio->property("volume").toDouble(), 0.4);
  EXPECT_TRUE(source->property("audio").value<QObject*>()->property("muted").toBool());

  QObject* out = l.child("out");
  audio->setProperty("volume", 0.7);
  EXPECT_EQ(out->property("volume").toInt(), 70);
  audio->setProperty("muted", true);
  EXPECT_TRUE(out->property("muted").toBool());
  audio->setProperty("volume", 3.0);  // clamped
  EXPECT_EQ(out->property("volume").toInt(), 100);

  out->setProperty("volume", 10);
  l.call(out, "changed");
  EXPECT_DOUBLE_EQ(audio->property("volume").toDouble(), 0.1);
}

TEST(QuickshellPipewire, NodesListSinkSourceAndSessions) {
  Loaded l(kPipewireQml);
  ASSERT_NE(l.root, nullptr);
  EXPECT_EQ(l.get("nodeCount").toInt(), 3);
  // The stream node is the third value of Pipewire.nodes.
  QQmlComponent probe(&l.engine);
  probe.setData(R"(
import QtQuick
import Quickshell.Services.Pipewire
QtObject {
    readonly property var node: Pipewire.nodes.values[2]
}
)",
                QUrl(QStringLiteral("file:///probe.qml")));
  std::unique_ptr<QObject> probeObject(probe.create());
  ASSERT_NE(probeObject, nullptr) << qPrintable(probe.errorString());
  QObject* stream = probeObject->property("node").value<QObject*>();
  ASSERT_NE(stream, nullptr);
  EXPECT_TRUE(stream->property("isStream").toBool());
  EXPECT_FALSE(stream->property("isSink").toBool());
  EXPECT_EQ(stream->property("name").toString(), "Music");
  EXPECT_EQ(stream->property("properties").toMap().value("application.name").toString(), "Music");
  EXPECT_EQ(stream->property("properties").toMap().value("application.icon-name").toString(),
            "music.exe");
  QObject* audio = stream->property("audio").value<QObject*>();
  EXPECT_DOUBLE_EQ(audio->property("volume").toDouble(), 0.5);
  audio->setProperty("volume", 0.25);
  EXPECT_DOUBLE_EQ(l.child("stream")->property("volume").toDouble(), 0.25);
}

TEST(QuickshellPipewire, PreferredDefaultSinkSelectsTheDevice) {
  Loaded l(kPipewireQml);
  ASSERT_NE(l.root, nullptr);
  QQmlComponent probe(&l.engine);
  probe.setData(R"(
import QtQuick
import Quickshell.Services.Pipewire
QtObject {
    function pick() { Pipewire.preferredDefaultAudioSink = Pipewire.defaultAudioSink }
    readonly property var tracker: PwObjectTracker { objects: [Pipewire.defaultAudioSink, null] }
    readonly property var links: PwNodeLinkTracker { node: Pipewire.defaultAudioSink }
    readonly property int linkCount: links.linkGroups.length
}
)",
                QUrl(QStringLiteral("file:///probe2.qml")));
  std::unique_ptr<QObject> probeObject(probe.create());
  ASSERT_NE(probeObject, nullptr) << qPrintable(probe.errorString());
  EXPECT_EQ(probeObject->property("linkCount").toInt(), 0);
  ASSERT_TRUE(QMetaObject::invokeMethod(probeObject.get(), "pick"));
  EXPECT_EQ(l.child("out")->property("chosen").toString(), "d1");
}

// ---- Quickshell.Services.SystemTray ----

TEST(QuickshellSystemTray, ItemsFollowTheTrayModelAndForwardClicks) {
  Loaded l(R"(
import QtQuick
import Quickshell.Services.SystemTray
Item {
    QtObject {
        id: fake; objectName: "fake"
        property var clicks: []
        function click(id, button) { clicks = clicks.concat([[id, button]]) }
    }
    ListModel { id: rows; objectName: "rows" }
    Component.onCompleted: {
        SystemTray.tray = fake;
        SystemTray.trayModel = rows;
        rows.append({ "id": 5, "key": "k", "tooltip": "Discord - 2 unread", "exe": "C:\\Apps\\Discord.exe", "iconUrl": "image://tray/5/1", "hidden": false });
    }
    property int count: SystemTray.items.values.length
    property var item: SystemTray.items.values[0]
}
)");
  ASSERT_NE(l.root, nullptr);
  EXPECT_EQ(l.get("count").toInt(), 1);
  QObject* item = l.get("item").value<QObject*>();
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->property("id").toString(), "discord");
  EXPECT_EQ(item->property("title").toString(), "Discord - 2 unread");
  EXPECT_EQ(item->property("tooltipTitle").toString(), "Discord - 2 unread");
  EXPECT_EQ(item->property("icon").toString(), "image://tray/5/1");
  EXPECT_EQ(item->property("status").toInt(), 1);
  EXPECT_FALSE(item->property("hasMenu").toBool());
  EXPECT_EQ(item->property("menu").value<QObject*>(), nullptr);

  ASSERT_TRUE(QMetaObject::invokeMethod(item, "activate"));
  ASSERT_TRUE(QMetaObject::invokeMethod(item, "secondaryActivate"));
  ASSERT_TRUE(QMetaObject::invokeMethod(item, "scroll", Q_ARG(qreal, 1.0), Q_ARG(bool, false)));
  const QVariantList clicks = l.child("fake")->property("clicks").toList();
  ASSERT_EQ(clicks.size(), 2);
  EXPECT_EQ(clicks.at(0).toList().at(0).toInt(), 5);
  EXPECT_EQ(clicks.at(0).toList().at(1).toInt(), int(Qt::LeftButton));
  EXPECT_EQ(clicks.at(1).toList().at(1).toInt(), int(Qt::MiddleButton));

  // A row update changes the same item; a removal drops it.
  QObject* rows = l.child("rows");
  QVariantMap changed{{"tooltip", "Discord"}};
  QQmlExpression update(l.engine.rootContext(), rows,
                        QStringLiteral("setProperty(0, \"tooltip\", \"Discord\")"));
  update.evaluate();
  ASSERT_FALSE(update.hasError()) << qPrintable(update.error().toString());
  EXPECT_EQ(item->property("title").toString(), "Discord");
  QQmlExpression remove(l.engine.rootContext(), rows, QStringLiteral("remove(0)"));
  remove.evaluate();
  ASSERT_FALSE(remove.hasError()) << qPrintable(remove.error().toString());
  EXPECT_EQ(l.get("count").toInt(), 0);
}

// ---- Quickshell.Bluetooth ----

TEST(QuickshellBluetooth, AdapterAndDevicesFollowTheService) {
  Loaded l(R"(
import QtQuick
import Quickshell.Bluetooth
Item {
    QtObject {
        id: fake; objectName: "fake"
        property bool available: true
        property bool radioOn: true
        property var calls: []
        property var devices: [
            { id: "AA:BB", name: "Buds", connected: false, paired: true, type: "audio", batteryPercent: 50 },
            { id: "CC:DD", name: "Mouse", connected: true, paired: true, type: "hid", batteryPercent: -1 }
        ]
        signal changed
        function setRadio(on) { calls = calls.concat(["radio:" + on]); radioOn = on }
        function connectDevice(id) { calls = calls.concat(["connect:" + id]) }
        function disconnectDevice(id) { calls = calls.concat(["disconnect:" + id]) }
        property bool scanning: false
        property bool pairing: false
        function scan() { calls = calls.concat(["scan"]); scanning = true }
        function pairDevice(id) { calls = calls.concat(["pair:" + id]); pairing = true }
        function forgetDevice(id) { calls = calls.concat(["forget:" + id]); pairing = true }
        function cancelPair() { calls = calls.concat(["cancel"]) }
    }
    Component.onCompleted: Bluetooth.service = fake
    property var adapter: Bluetooth.defaultAdapter
    property int deviceCount: Bluetooth.devices.values.length
    property int adapterCount: Bluetooth.adapters.values.length
}
)");
  ASSERT_NE(l.root, nullptr);
  QObject* adapter = l.get("adapter").value<QObject*>();
  ASSERT_NE(adapter, nullptr);
  EXPECT_TRUE(adapter->property("enabled").toBool());
  EXPECT_EQ(adapter->property("state").toInt(), 1);
  EXPECT_EQ(l.get("deviceCount").toInt(), 2);
  EXPECT_EQ(l.get("adapterCount").toInt(), 1);

  QQmlComponent probe(&l.engine);
  probe.setData(R"(
import QtQuick
import Quickshell.Bluetooth
QtObject {
    readonly property var buds: Bluetooth.defaultAdapter.devices.values[0]
    readonly property var mouse: Bluetooth.devices.values[1]
}
)",
                QUrl(QStringLiteral("file:///bt.qml")));
  std::unique_ptr<QObject> probeObject(probe.create());
  ASSERT_NE(probeObject, nullptr) << qPrintable(probe.errorString());
  QObject* buds = probeObject->property("buds").value<QObject*>();
  QObject* mouse = probeObject->property("mouse").value<QObject*>();
  ASSERT_NE(buds, nullptr);
  ASSERT_NE(mouse, nullptr);
  EXPECT_EQ(buds->property("name").toString(), "Buds");
  EXPECT_EQ(buds->property("address").toString(), "AA:BB");
  EXPECT_FALSE(buds->property("connected").toBool());
  EXPECT_TRUE(buds->property("paired").toBool());
  EXPECT_TRUE(buds->property("batteryAvailable").toBool());
  EXPECT_DOUBLE_EQ(buds->property("battery").toDouble(), 0.5);
  EXPECT_TRUE(mouse->property("connected").toBool());
  EXPECT_FALSE(mouse->property("batteryAvailable").toBool());

  QObject* fake = l.child("fake");
  ASSERT_TRUE(QMetaObject::invokeMethod(buds, "connect"));
  mouse->setProperty("connected", false);
  adapter->setProperty("enabled", false);
  const QVariantList calls = fake->property("calls").toList();
  ASSERT_EQ(calls.size(), 3);
  EXPECT_EQ(calls.at(0).toString(), "connect:AA:BB");
  EXPECT_EQ(calls.at(1).toString(), "disconnect:CC:DD");
  EXPECT_EQ(calls.at(2).toString(), "radio:false");

  // The service reports the new state; nothing is written back.
  fake->setProperty("devices", QVariantList{QVariantMap{{"id", "AA:BB"},
                                                        {"name", "Buds"},
                                                        {"connected", true},
                                                        {"paired", true},
                                                        {"batteryPercent", 40}}});
  l.call(fake, "changed");
  EXPECT_EQ(l.get("deviceCount").toInt(), 1);
  EXPECT_TRUE(buds->property("connected").toBool());
  EXPECT_EQ(fake->property("calls").toList().size(), 3);

  // discovering starts one scan and follows the service's `scanning`.
  adapter->setProperty("discovering", true);
  EXPECT_EQ(fake->property("calls").toList().size(), 4);
  EXPECT_EQ(fake->property("calls").toList().at(3).toString(), "scan");
  EXPECT_TRUE(fake->property("scanning").toBool());
  EXPECT_TRUE(adapter->property("discovering").toBool());
  // A scan cannot be stopped: false snaps back while it runs.
  adapter->setProperty("discovering", false);
  QTRY_VERIFY_WITH_TIMEOUT(adapter->property("discovering").toBool(), 1000);
  EXPECT_EQ(fake->property("calls").toList().size(), 4);
  fake->setProperty("scanning", false);
  EXPECT_FALSE(adapter->property("discovering").toBool());

  // pair, cancelPair and forget go to the service; `pairing` follows it.
  EXPECT_FALSE(buds->property("pairing").toBool());
  ASSERT_TRUE(QMetaObject::invokeMethod(buds, "pair"));
  EXPECT_EQ(fake->property("calls").toList().at(4).toString(), "pair:AA:BB");
  EXPECT_TRUE(buds->property("pairing").toBool());
  ASSERT_TRUE(QMetaObject::invokeMethod(buds, "cancelPair"));
  EXPECT_EQ(fake->property("calls").toList().at(5).toString(), "cancel");
  fake->setProperty("pairing", false);
  EXPECT_FALSE(buds->property("pairing").toBool());
  ASSERT_TRUE(QMetaObject::invokeMethod(buds, "forget"));
  EXPECT_EQ(fake->property("calls").toList().at(6).toString(), "forget:AA:BB");
}

TEST(QuickshellBluetooth, NoRadioMeansNoAdapter) {
  Loaded l(R"(
import QtQuick
import Quickshell.Bluetooth
Item {
    QtObject { id: fake; property bool available: false; property bool radioOn: false; property var devices: []; signal changed }
    Component.onCompleted: Bluetooth.service = fake
    property bool none: Bluetooth.defaultAdapter === null
    property int adapters: Bluetooth.adapters.values.length
    property string stateName: BluetoothDeviceState.toString(BluetoothDeviceState.Connected)
}
)");
  ASSERT_NE(l.root, nullptr);
  EXPECT_TRUE(l.get("none").toBool());
  EXPECT_EQ(l.get("adapters").toInt(), 0);
  EXPECT_EQ(l.get("stateName").toString(), "Connected");
}

// ---- Quickshell.Wayland idle types, QsMenuOpener, Quickshell.Networking ----

TEST(QuickshellWayland, IdleInhibitorAndIdleMonitorLoad) {
  Loaded l(R"(
import QtQuick
import Quickshell.Wayland
Item {
    IdleInhibitor { id: inhibitor; objectName: "inhibitor"; enabled: false }
    IdleMonitor { id: monitor; objectName: "monitor"; timeout: 0 }
    IdleMonitor { id: waiting; objectName: "waiting"; timeout: 1000000 }
    property bool inhibitorEnabled: inhibitor.enabled
    property bool monitorIdle: monitor.isIdle
    property bool waitingIdle: waiting.isIdle
}
)");
  ASSERT_NE(l.root, nullptr);
  EXPECT_FALSE(l.get("inhibitorEnabled").toBool());
  // A zero timeout never idles, and a timeout far beyond the idle time has not elapsed.
  EXPECT_FALSE(l.get("monitorIdle").toBool());
  EXPECT_FALSE(l.get("waitingIdle").toBool());
}

TEST(QuickshellCore, QsMenuOpenerHasNoEntriesWithoutAMenu) {
  Loaded l(R"(
import QtQuick
import Quickshell
Item {
    QsMenuOpener { id: opener; menu: null }
    property int count: opener.children.values.length
    property var handle: opener.menu
}
)");
  ASSERT_NE(l.root, nullptr);
  EXPECT_EQ(l.get("count").toInt(), 0);
  EXPECT_TRUE(l.get("handle").isNull());
}

TEST(QuickshellNetworking, WifiEnabledFollowsTheServiceAndSwitchesIt) {
  Loaded l(R"(
import QtQuick
import Quickshell.Networking
Item {
    QtObject {
        id: fake; objectName: "fake"
        property bool wifiEnabled: true
        property var calls: []
        signal wifiChanged
        function setWifiEnabled(on) { calls = calls.concat(["radio:" + on]); wifiEnabled = on; wifiChanged(); return true }
    }
    Component.onCompleted: Networking.service = fake
    property bool enabled: Networking.wifiEnabled
    property bool hardware: Networking.wifiHardwareEnabled
    function assign(on) { Networking.wifiEnabled = on }
}
)");
  ASSERT_NE(l.root, nullptr);
  QObject* fake = l.child("fake");
  ASSERT_NE(fake, nullptr);
  EXPECT_TRUE(l.get("enabled").toBool());
  EXPECT_TRUE(l.get("hardware").toBool());
  // The service changes: the singleton follows and does not switch the radio back.
  fake->setProperty("wifiEnabled", false);
  QMetaObject::invokeMethod(fake, "wifiChanged");
  EXPECT_FALSE(l.get("enabled").toBool());
  EXPECT_TRUE(fake->property("calls").toList().isEmpty());
  // Assigning switches the radio through the service.
  QVariant ignored;
  ASSERT_TRUE(QMetaObject::invokeMethod(l.root.get(), "assign", Q_RETURN_ARG(QVariant, ignored),
                                        Q_ARG(QVariant, true)));
  EXPECT_EQ(fake->property("calls").toStringList(), QStringList{QStringLiteral("radio:true")});
  EXPECT_TRUE(l.get("enabled").toBool());
}

TEST(QuickshellWayland, ToplevelManagerListsNothingWithoutADesktop) {
  // The taskbar service is not available offscreen: the list is empty and nothing is active.
  Loaded l(R"(
import QtQuick
import Quickshell.Wayland
Item {
    property int count: ToplevelManager.toplevels.values.length
    property var active: ToplevelManager.activeToplevel
}
)");
  ASSERT_NE(l.root, nullptr);
  EXPECT_EQ(l.get("count").toInt(), 0);
  EXPECT_TRUE(l.get("active").isNull());
}

TEST(QuickshellWayland, AToplevelHasTheQuickshellProperties) {
  Loaded l(R"(
import QtQuick
import Quickshell.Wayland
Item {
    Toplevel {
        id: t; objectName: "t"
        appId: "notepad"; title: "Untitled"; activated: true; hwnd: 4242
    }
    property string appId: t.appId
    property bool activated: t.activated
    property bool fullscreen: t.fullscreen
    property int screenCount: t.screens.length
}
)");
  ASSERT_NE(l.root, nullptr);
  EXPECT_EQ(l.get("appId").toString(), QStringLiteral("notepad"));
  EXPECT_TRUE(l.get("activated").toBool());
  EXPECT_FALSE(l.get("fullscreen").toBool());
  EXPECT_EQ(l.get("screenCount").toInt(), 0);
}
