import QtQuick
import tidy
import tidy.services as S

// The control center: a bar button that opens a panel with the account and session buttons,
// quick toggles, brightness, volume and microphone sliders, power plan and mode, and what is
// playing. A click opens the panel, a right click opens the Settings app.
//
//     ControlCenter {
//         glyph: "⚙"
//         columns: 3
//         quickActions: ["dnd", "mute", "micMute", "screenshot", "touchKeyboard", "darkMode"]
//         sectionsOrder: ["systemControls", "quickActions", "sliders", "power", "media"]
//     }
//
// Sections: systemControls (profile, lock, Settings pages, power menu), quickActions, sliders,
// power, media. `sectionsOrder` lists the ones to show, top to bottom; the `show*` flags hide a
// section or a slider without changing the order. Quick action ids: dnd (on and off), cycleDnd
// (off, priority only, alarms only), mute, micMute, screenshot, touchKeyboard, darkMode. Sign out,
// restart and shut down ask for a second click on the same button.
//
// The volume and microphone sliders drive the default output and input (the same AudioOutput and
// AudioInput the Volume widget uses); the track is the Media singleton.
Pill {
    id: root

    property string glyph: "⚙"
    property var sectionsOrder: ["systemControls", "quickActions", "sliders", "power", "media"]
    property var quickActions: ["dnd", "mute", "micMute", "screenshot", "touchKeyboard", "darkMode"]
    property int columns: 3
    property bool showSystemControls: true
    property bool showQuickActions: true
    property bool showSliders: true
    property bool showBrightness: true
    property bool showVolume: true
    property bool showMicrophone: true
    property bool showSourceSelectors: true
    property bool showPower: true
    property bool showMedia: true
    property int profileImageSize: 28
    property int thumbnailSize: 36
    property string powerPlanTitle: "Power Plan"
    property string powerModeTitle: "Power Mode"

    readonly property int panelWidth: 340
    readonly property var quickLabels: ({
            "dnd": "DND",
            "cycleDnd": "DND",
            "mute": "Mute",
            "micMute": "Mic",
            "screenshot": "Shot",
            "touchKeyboard": "Keys",
            "darkMode": "Dark"
        })
    readonly property var quickNames: ({
            "dnd": "Do Not Disturb",
            "cycleDnd": "Do Not Disturb",
            "micMute": "Mic Mute",
            "screenshot": "Screenshot",
            "touchKeyboard": "Keyboard",
            "darkMode": "Dark Mode"
        })
    readonly property var quickGlyphs: ({
            "dnd": "☾",
            "cycleDnd": "☾",
            "mute": "🔇",
            "micMute": "🎙",
            "screenshot": "✂",
            "touchKeyboard": "⌨",
            "darkMode": "◐"
        })
    readonly property var settingsPages: [
        {
            "name": "Windows Update",
            "uri": "ms-settings:windowsupdate"
        },
        {
            "name": "Network & Internet",
            "uri": "ms-settings:network"
        },
        {
            "name": "Bluetooth & Devices",
            "uri": "ms-settings:bluetooth"
        },
        {
            "name": "Sound",
            "uri": "ms-settings:sound"
        },
        {
            "name": "Notifications",
            "uri": "ms-settings:notifications"
        },
        {
            "name": "Power & Battery",
            "uri": "ms-settings:powersleep"
        },
        {
            "name": "Apps",
            "uri": "ms-settings:appsfeatures"
        },
        {
            "name": "Accounts",
            "uri": "ms-settings:yourinfo"
        },
        {
            "name": "Privacy & Security",
            "uri": "ms-settings:privacy"
        },
        {
            "name": "All Settings",
            "uri": "ms-settings:"
        }
    ]

    function quickActive(id) {
        switch (id) {
        case "dnd":
        case "cycleDnd":
            return S.ControlCenter.dndActive;
        case "mute":
            return S.AudioOutput.muted;
        case "micMute":
            return S.AudioInput.muted;
        case "darkMode":
            return S.ControlCenter.darkMode;
        }
        return false;
    }

    function quickEnabled(id) {
        if (id === "mute")
            return S.AudioOutput.available;
        if (id === "micMute")
            return S.AudioInput.available;
        return true;
    }

    function runQuick(id) {
        switch (id) {
        case "dnd":
            S.ControlCenter.toggleDnd();
            break;
        case "cycleDnd":
            S.ControlCenter.cycleDnd();
            break;
        case "mute":
            S.AudioOutput.muted = !S.AudioOutput.muted;
            break;
        case "micMute":
            S.AudioInput.muted = !S.AudioInput.muted;
            break;
        case "screenshot":
            panel.close();
            S.ControlCenter.screenshot();
            break;
        case "touchKeyboard":
            S.ControlCenter.toggleTouchKeyboard();
            break;
        case "darkMode":
            S.ControlCenter.toggleTheme();
            break;
        }
    }

    // Runs a session or power action; the panel closes once it ran. The ones that need a second
    // click return false on the first.
    function runAction(id) {
        if (S.ControlCenter.requestAction(id))
            panel.close();
    }

    function sectionShown(name) {
        switch (name) {
        case "systemControls":
            return showSystemControls;
        case "quickActions":
            return showQuickActions && quickActions.length > 0;
        case "sliders":
            return showSliders && (showBrightness || showVolume || showMicrophone);
        case "power":
            return showPower && S.ControlCenter.powerPlans.length > 0;
        case "media":
            return showMedia && S.Media.available;
        }
        return false;
    }

    function sectionComponent(name) {
        switch (name) {
        case "systemControls":
            return systemControlsSection;
        case "quickActions":
            return quickActionsSection;
        case "sliders":
            return slidersSection;
        case "power":
            return powerSection;
        case "media":
            return mediaSection;
        }
        return null;
    }

    text: glyph
    tooltip: "Control center"

    onClicked: panel.toggle()
    onRightClicked: S.ControlCenter.openSettings("ms-settings:")

    // A labelled, collapsible list of choices: shows the current one, expands to pick another.
    component Choice: Column {
        id: choice

        property string title: ""
        property string current: ""
        property var choices: []
        property string activeId: ""
        property bool expanded: false
        property int rowWidth: 200

        signal picked(string id)

        spacing: 4

        Pill {
            width: choice.rowWidth
            text: (choice.title !== "" ? choice.title + ": " : "") + choice.current
            maxTextWidth: choice.rowWidth - 2 * Theme.padding - 20
            onClicked: choice.expanded = !choice.expanded

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
                text: choice.expanded ? "▴" : "▾"
            }
        }

        Repeater {
            model: choice.expanded ? choice.choices : []

            delegate: Pill {
                required property var modelData

                width: choice.rowWidth
                maxTextWidth: choice.rowWidth - 2 * Theme.padding
                text: modelData.name
                fill: modelData.id === choice.activeId || modelData.active === true ? Theme.accentFill : Theme.fill
                onClicked: {
                    choice.expanded = false;
                    choice.picked(modelData.id);
                }
            }
        }
    }

    // One slider line: glyph, slider, percentage and, optionally, a device or monitor selector.
    component SliderRow: Column {
        id: line

        property string icon: ""
        property real value: 0
        property bool sliderEnabled: true
        property bool showChoice: false
        property var choices: []
        property string activeId: ""
        property string current: ""
        property alias sliderName: slider.objectName

        signal moved(real value)
        signal picked(string id)

        spacing: 4

        Row {
            spacing: 10

            Text {
                width: 24
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignHCenter
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                text: line.icon
            }

            Slider {
                id: slider
                width: root.panelWidth - 24 - 40 - 20 - 2 * Theme.padding
                enabled: line.sliderEnabled
                value: line.value
                onMoved: v => line.moved(v)
            }

            Text {
                width: 36
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
                text: Math.round(line.value) + "%"
            }
        }

        Choice {
            visible: line.showChoice && line.choices.length > 0
            x: 34
            rowWidth: root.panelWidth - 34 - 2 * Theme.padding
            current: line.current
            choices: line.choices
            activeId: line.activeId
            onPicked: id => line.picked(id)
        }
    }

    Component {
        id: systemControlsSection

        Column {
            spacing: 6

            Row {
                spacing: 8

                // Profile: picture and name; a click opens the account page.
                Pill {
                    height: Math.max(Theme.pillHeight, root.profileImageSize + 8)
                    width: root.panelWidth - 3 * 8 - 2 * 44 - 2 * Theme.padding
                    maxTextWidth: width - root.profileImageSize - 3 * Theme.padding
                    text: S.ControlCenter.userName
                    onClicked: S.ControlCenter.openSettings("ms-settings:yourinfo")

                    Image {
                        visible: S.ControlCenter.avatarUrl !== ""
                        width: root.profileImageSize
                        height: root.profileImageSize
                        anchors.verticalCenter: parent.verticalCenter
                        source: S.ControlCenter.avatarUrl
                        sourceSize: Qt.size(root.profileImageSize * 2, root.profileImageSize * 2)
                        fillMode: Image.PreserveAspectCrop
                    }
                }

                Pill {
                    width: 44
                    text: "🔒"
                    tooltip: "Lock"
                    onClicked: root.runAction("lock")
                }

                Pill {
                    width: 44
                    text: "⚙"
                    tooltip: "Settings"
                    fill: settingsList.visible ? Theme.accentFill : Theme.fill
                    onClicked: {
                        powerList.visible = false;
                        settingsList.visible = !settingsList.visible;
                    }
                }

                Pill {
                    width: 44
                    text: "⏻"
                    tooltip: "Power"
                    fill: powerList.visible ? Theme.accentFill : Theme.fill
                    onClicked: {
                        settingsList.visible = false;
                        powerList.visible = !powerList.visible;
                        S.ControlCenter.cancelAction();
                    }
                }
            }

            Column {
                id: settingsList
                visible: false
                spacing: 4

                Repeater {
                    model: root.settingsPages

                    delegate: Pill {
                        required property var modelData

                        width: root.panelWidth - 2 * Theme.padding
                        text: modelData.name
                        onClicked: {
                            settingsList.visible = false;
                            panel.close();
                            S.ControlCenter.openSettings(modelData.uri);
                        }
                    }
                }
            }

            // Sign out, restart and shut down ask again on the same button.
            Column {
                id: powerList
                visible: false
                spacing: 4

                Repeater {
                    model: [
                        {
                            "id": "sleep",
                            "name": "Sleep"
                        },
                        {
                            "id": "hibernate",
                            "name": "Hibernate"
                        },
                        {
                            "id": "signOut",
                            "name": "Sign out"
                        },
                        {
                            "id": "restart",
                            "name": "Restart"
                        },
                        {
                            "id": "shutdown",
                            "name": "Shut down"
                        }
                    ]

                    delegate: Pill {
                        required property var modelData

                        width: root.panelWidth - 2 * Theme.padding
                        text: S.ControlCenter.pendingAction === modelData.id ? "Click again to " + modelData.name.toLowerCase() : modelData.name
                        fill: S.ControlCenter.pendingAction === modelData.id ? Theme.warnFill : Theme.fill
                        onClicked: root.runAction(modelData.id)
                    }
                }
            }
        }
    }

    Component {
        id: quickActionsSection

        Grid {
            columns: Math.max(1, root.columns)
            spacing: 6

            Repeater {
                model: root.quickActions

                delegate: Pill {
                    required property string modelData

                    readonly property bool active: root.quickActive(modelData)
                    readonly property bool usable: root.quickEnabled(modelData)

                    width: (root.panelWidth - 2 * Theme.padding - (Math.max(1, root.columns) - 1) * 6) / Math.max(1, root.columns)
                    maxTextWidth: width - 2 * Theme.padding - 22
                    text: (root.quickGlyphs[modelData] ?? "") + " " + (root.quickLabels[modelData] ?? modelData)
                    fill: active ? Theme.accentFill : Theme.fill
                    opacity: usable ? 1 : 0.4
                    interactive: usable
                    tooltip: usable ? (root.quickNames[modelData] ?? "") : "Unavailable"
                    onClicked: {
                        if (usable)
                            root.runQuick(modelData);
                    }
                }
            }
        }
    }

    Component {
        id: slidersSection

        Column {
            spacing: 8

            SliderRow {
                visible: root.showBrightness && S.ControlCenter.brightnessAvailable
                icon: "☀"
                sliderName: "brightnessSlider"
                value: S.ControlCenter.brightness
                showChoice: root.showSourceSelectors && S.ControlCenter.brightnessSources.length > 1
                choices: S.ControlCenter.brightnessSources
                activeId: S.ControlCenter.brightnessSource
                current: {
                    for (const source of S.ControlCenter.brightnessSources)
                        if (source.id === S.ControlCenter.brightnessSource)
                            return source.name;
                    return "Monitor";
                }
                onMoved: v => S.ControlCenter.setBrightness(Math.round(v))
                onPicked: id => S.ControlCenter.setBrightnessSource(id)
            }

            SliderRow {
                visible: root.showVolume && S.AudioOutput.available
                icon: S.AudioOutput.muted ? "🔇" : "🔊"
                sliderName: "volumeSlider"
                value: S.AudioOutput.volume
                showChoice: root.showSourceSelectors
                choices: S.AudioOutput.devices
                activeId: S.AudioOutput.defaultDevice
                current: {
                    for (const device of S.AudioOutput.devices)
                        if (device.id === S.AudioOutput.defaultDevice)
                            return device.name;
                    return "Output";
                }
                onMoved: v => S.AudioOutput.volume = Math.round(v)
                onPicked: id => S.AudioOutput.setDefaultDevice(id)
            }

            SliderRow {
                visible: root.showMicrophone && S.AudioInput.available
                icon: S.AudioInput.muted ? "🚫" : "🎙"
                sliderName: "microphoneSlider"
                value: S.AudioInput.volume
                showChoice: root.showSourceSelectors
                choices: S.AudioInput.devices
                activeId: S.AudioInput.defaultDevice
                current: {
                    for (const device of S.AudioInput.devices)
                        if (device.id === S.AudioInput.defaultDevice)
                            return device.name;
                    return "Input";
                }
                onMoved: v => S.AudioInput.volume = Math.round(v)
                onPicked: id => S.AudioInput.setDefaultDevice(id)
            }
        }
    }

    Component {
        id: powerSection

        Column {
            spacing: 6

            Choice {
                rowWidth: root.panelWidth - 2 * Theme.padding
                title: root.powerPlanTitle
                current: S.ControlCenter.powerPlanName !== "" ? S.ControlCenter.powerPlanName : "Unknown"
                choices: S.ControlCenter.powerPlans
                onPicked: id => S.ControlCenter.setPowerPlan(id)
            }

            Choice {
                visible: S.ControlCenter.powerModeAvailable
                rowWidth: root.panelWidth - 2 * Theme.padding
                title: root.powerModeTitle
                current: S.ControlCenter.powerModeName !== "" ? S.ControlCenter.powerModeName : "Unknown"
                choices: S.ControlCenter.powerModes
                onPicked: id => S.ControlCenter.setPowerMode(id)
            }
        }
    }

    Component {
        id: mediaSection

        Row {
            spacing: 10

            Image {
                visible: S.Media.thumbnailUrl !== ""
                width: root.thumbnailSize
                height: root.thumbnailSize
                source: S.Media.thumbnailUrl
                sourceSize: Qt.size(root.thumbnailSize * 2, root.thumbnailSize * 2)
                fillMode: Image.PreserveAspectCrop
            }

            Column {
                width: 112
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                    text: S.Media.title
                }

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    color: Theme.text
                    opacity: 0.7
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallFontSize
                    text: S.Media.artist
                }
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Pill {
                    text: "⏮"
                    opacity: S.Media.canPrevious ? 1 : 0.4
                    onClicked: S.Media.previous()
                }

                Pill {
                    text: S.Media.playing ? "⏸" : "▶"
                    opacity: S.Media.canPlayPause ? 1 : 0.4
                    onClicked: S.Media.playPause()
                }

                Pill {
                    text: "⏭"
                    opacity: S.Media.canNext ? 1 : 0.4
                    onClicked: S.Media.next()
                }
            }
        }
    }

    PopupWindow {
        id: panel
        objectName: "controlCenterPopup"
        anchorItem: root
        spacing: 10

        // Read the machine again each time the panel opens; a half-confirmed shutdown never
        // survives closing it.
        onOpened: S.ControlCenter.refresh()
        onClosed: S.ControlCenter.cancelAction()

        Repeater {
            model: root.sectionsOrder

            delegate: Loader {
                required property string modelData

                active: root.sectionShown(modelData)
                visible: active
                sourceComponent: root.sectionComponent(modelData)
            }
        }
    }
}
