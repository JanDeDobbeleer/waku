import QtQuick
import tidy
import tidy.services as S

// The control center: a bar button that opens a panel with the account and session buttons,
// quick toggles, brightness, volume and microphone sliders, power plan and mode, and what is
// playing, drawn like the control center of Python YASB (square tiles with the label below, a
// divider between sections, cards for the power selectors). A click opens the panel, a right click
// opens the Settings app.
//
//     ControlCenter {
//         glyph: ""
//         columns: 3
//         quickActions: ["dnd", "mute", "micMute", "screenshot", "touchKeyboard", "darkMode"]
//         sectionsOrder: ["systemControls", "quickActions", "sliders", "power", "media"]
//     }
//
// Sections: systemControls (profile, Settings pages, power menu), quickActions, sliders, power,
// media. `sectionsOrder` lists the ones to show, top to bottom; the `show*` flags hide a section or
// a slider without changing the order. Quick action ids: dnd (on and off), cycleDnd (off, priority
// only, alarms only), mute, micMute, screenshot, touchKeyboard, darkMode. Lock, sleep, hibernate,
// sign out, restart and shut down are in the power menu; sign out, restart and shut down ask for a
// second click on the same row.
//
// The volume and microphone sliders drive the default output and input (the same AudioOutput and
// AudioInput the Volume widget uses); the track is the Media singleton.
Pill {
    id: root

    property string glyph: ""
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
    property int thumbnailSize: 64
    property string powerPlanTitle: "Power Plan"
    property string powerModeTitle: "Power Mode"

    // The panel is 360 px wide, as in Python YASB (.control-center-menu min-width).
    readonly property int panelWidth: 360
    readonly property var quickNames: ({
            "dnd": "Do Not Disturb",
            "cycleDnd": "Do Not Disturb",
            "mute": "Mute",
            "micMute": "Mic Mute",
            "screenshot": "Screenshot",
            "touchKeyboard": "Keyboard",
            "darkMode": "Dark Mode"
        })
    // Segoe Fluent Icons: the defaults of Python YASB.
    readonly property var quickGlyphs: ({
            "dnd": "",
            "cycleDnd": "",
            "mute": "",
            "micMute": "",
            "screenshot": "",
            "touchKeyboard": "",
            "darkMode": ""
        })
    readonly property color divider: Qt.alpha(Theme.text, 0.1)
    readonly property color muted: Qt.alpha(Theme.text, 0.5)
    readonly property color tileFill: Qt.alpha(Theme.text, 0.06)
    readonly property color tileFillHover: Qt.alpha(Theme.text, 0.08)
    readonly property color tileBorder: Qt.alpha(Theme.text, 0.08)
    readonly property color tileBorderHover: Qt.alpha(Theme.text, 0.1)
    // Which dropdown is open: "", "settings", "power", "plan", "mode", "brightness", "volume" or
    // "microphone". One at a time.
    property string openList: ""
    readonly property var settingsPages: [
        {
            "id": "ms-settings:windowsupdate",
            "name": "Windows Update"
        },
        {
            "id": "ms-settings:network",
            "name": "Network & Internet"
        },
        {
            "id": "ms-settings:bluetooth",
            "name": "Bluetooth & Devices"
        },
        {
            "id": "ms-settings:sound",
            "name": "Sound"
        },
        {
            "id": "ms-settings:notifications",
            "name": "Notifications"
        },
        {
            "id": "ms-settings:powersleep",
            "name": "Power & Battery"
        },
        {
            "id": "ms-settings:appsfeatures",
            "name": "Apps"
        },
        {
            "id": "ms-settings:yourinfo",
            "name": "Accounts"
        },
        {
            "id": "ms-settings:privacy",
            "name": "Privacy & Security"
        },
        {
            "id": "ms-settings:",
            "name": "All Settings"
        }
    ]

    function toggleList(name) {
        openList = openList === name ? "" : name;
    }

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

    icon: glyph
    tooltip: "Control center"

    onClicked: panel.toggle()
    onRightClicked: S.ControlCenter.openSettings("ms-settings:")

    // A section of the panel: padding, an optional line below or above it and an optional tint. The
    // children are stacked in a column.
    component Section: Item {
        id: section

        default property alias content: stack.data
        property real padTop: 16
        property real padBottom: 16
        property real padSide: 12
        property bool lineBelow: false
        property bool lineAbove: false
        property bool tinted: false

        width: root.panelWidth
        implicitWidth: root.panelWidth
        implicitHeight: stack.implicitHeight + padTop + padBottom

        // The tint stops short of the panel's rounded bottom corners, as the window is drawn round.
        Rectangle {
            visible: section.tinted
            x: Theme.popupBorderWidth
            width: parent.width - 2 * Theme.popupBorderWidth
            height: parent.height - Theme.popupRadius
            color: Qt.alpha(Theme.onAccent, 0.2)
        }

        Rectangle {
            visible: section.tinted
            x: Theme.popupBorderWidth
            y: parent.height - 2 * Theme.popupRadius - Theme.popupBorderWidth
            width: parent.width - 2 * Theme.popupBorderWidth
            height: 2 * Theme.popupRadius
            radius: Theme.popupRadius
            color: Qt.alpha(Theme.onAccent, 0.2)
        }

        Column {
            id: stack
            x: section.padSide
            y: section.padTop
            width: section.width - 2 * section.padSide
        }

        Rectangle {
            visible: section.lineBelow
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: root.divider
        }

        Rectangle {
            visible: section.lineAbove
            width: parent.width
            height: 1
            color: root.divider
        }
    }

    // A glyph from the icon font.
    component Glyph: Text {
        property real size: 16

        color: Theme.text
        font.family: Theme.iconFont
        font.pixelSize: size
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    // A round button of the account row.
    component RoundButton: Rectangle {
        id: round

        property string glyph: ""
        property bool on: false

        signal clicked

        width: 32
        height: 32
        radius: 16
        color: Qt.alpha(Theme.text, on || hover.hovered ? 0.2 : 0.08)
        opacity: tap.pressed ? 0.5 : 1

        Glyph {
            anchors.centerIn: parent
            size: 14
            text: round.glyph
        }

        HoverHandler {
            id: hover
        }

        TapHandler {
            id: tap
            onTapped: round.clicked()
        }
    }

    // A dropdown list: rows to pick one from; the current one has an accent bar.
    component OptionList: Column {
        id: options

        property var choices: []
        property string activeId: ""
        property string pending: ""

        signal picked(string id)

        spacing: 2

        Repeater {
            model: options.choices

            delegate: Rectangle {
                id: row

                required property var modelData

                readonly property bool isActive: modelData.id === options.activeId || modelData.active === true
                readonly property bool isPending: options.pending !== "" && modelData.id === options.pending

                width: options.width
                height: 30
                radius: 6
                color: isPending ? Theme.warnFill : (rowHover.hovered ? Qt.alpha(Theme.text, 0.1) : "transparent")

                Rectangle {
                    visible: row.isActive
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 4
                    height: 12
                    radius: 2
                    color: Theme.accent
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 20
                    width: parent.width - 28
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallFontSize
                    font.weight: Font.DemiBold
                    text: row.modelData.name
                }

                HoverHandler {
                    id: rowHover
                }

                TapHandler {
                    onTapped: options.picked(row.modelData.id)
                }
            }
        }
    }

    // One slider line: glyph, slider, percentage and, optionally, the chevron that picks a device or
    // monitor.
    component SliderRow: Column {
        id: line

        property string name: ""
        property string icon: ""
        property real value: 0
        property bool sliderEnabled: true
        property bool showChoice: false
        property var choices: []
        property string activeId: ""
        property alias sliderName: slider.objectName

        signal moved(real value)
        signal picked(string id)

        width: parent ? parent.width : 0

        Item {
            width: line.width
            height: 34

            Glyph {
                id: lead
                width: 36
                height: parent.height
                horizontalAlignment: Text.AlignLeft
                opacity: line.sliderEnabled ? 1 : 0.5
                text: line.icon
            }

            Rectangle {
                id: more
                // Takes its room even when there is nothing to pick, so the rows line up.
                visible: line.showChoice && line.choices.length > 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: root.showSourceSelectors ? 24 : 0
                height: 24
                radius: 6
                color: moreHover.hovered || root.openList === line.name ? Qt.alpha(Theme.text, 0.1) : "transparent"

                Glyph {
                    anchors.centerIn: parent
                    size: 12
                    color: Qt.alpha(Theme.text, 0.6)
                    text: ""
                }

                HoverHandler {
                    id: moreHover
                }

                TapHandler {
                    onTapped: root.toggleList(line.name)
                }
            }

            Text {
                id: percent
                anchors.right: more.left
                anchors.rightMargin: more.width > 0 ? 4 : 0
                width: 36
                height: parent.height
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.weight: Font.DemiBold
                text: Math.round(line.value) + "%"
            }

            Slider {
                id: slider
                anchors.left: lead.right
                anchors.right: percent.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                enabled: line.sliderEnabled
                value: line.value
                onMoved: v => line.moved(v)
            }
        }

        OptionList {
            visible: root.openList === line.name
            x: 36
            width: line.width - 36
            choices: line.choices
            activeId: line.activeId
            onPicked: id => {
                root.openList = "";
                line.picked(id);
            }
        }
    }

    // A power plan or mode: title, current value and a chevron.
    component Card: Rectangle {
        id: card

        property string title: ""
        property string current: ""
        property bool usable: true

        signal clicked

        height: Math.max(36, textColumn.implicitHeight + 16)
        radius: 6
        color: hover.hovered && usable ? root.tileFillHover : root.tileFill
        border.color: root.tileBorder
        border.width: 1
        opacity: !usable ? 0.5 : (tap.pressed ? 0.5 : 1)

        Column {
            id: textColumn
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 20 - 24

            Text {
                width: parent.width
                elide: Text.ElideRight
                color: root.muted
                font.family: Theme.fontFamily
                font.pixelSize: 11
                font.weight: Font.DemiBold
                text: card.title
            }

            Text {
                width: parent.width
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
                text: card.current
            }
        }

        Glyph {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            size: 14
            color: root.muted
            text: ""
        }

        HoverHandler {
            id: hover
        }

        TapHandler {
            id: tap
            enabled: card.usable
            onTapped: card.clicked()
        }
    }

    Component {
        id: systemControlsSection

        Section {
            padTop: 12
            padBottom: 0
            padSide: 16

            Item {
                width: parent.width
                height: Math.max(32, root.profileImageSize + 4)

                // Profile: the picture, round; a click opens the account page. The ring around it
                // is drawn in the popup's colour to cut the corners off the square image.
                Item {
                    id: avatar
                    width: root.profileImageSize
                    height: width
                    anchors.verticalCenter: parent.verticalCenter

                    Image {
                        visible: S.ControlCenter.avatarUrl !== ""
                        anchors.fill: parent
                        source: S.ControlCenter.avatarUrl
                        sourceSize: Qt.size(root.profileImageSize * 2, root.profileImageSize * 2)
                        fillMode: Image.PreserveAspectCrop
                    }

                    Rectangle {
                        visible: S.ControlCenter.avatarUrl !== ""
                        readonly property real band: root.profileImageSize * 0.25
                        anchors.centerIn: parent
                        width: root.profileImageSize + 2 * band
                        height: width
                        radius: width / 2
                        color: "transparent"
                        border.width: band
                        border.color: Theme.popupSurface
                    }

                    Rectangle {
                        visible: S.ControlCenter.avatarUrl !== ""
                        anchors.fill: parent
                        radius: width / 2
                        color: "transparent"
                        border.width: 1
                        border.color: Qt.alpha(Theme.text, 0.2)
                    }

                    Text {
                        visible: S.ControlCenter.avatarUrl === ""
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        text: S.ControlCenter.userName
                    }

                    TapHandler {
                        onTapped: S.ControlCenter.openSettings("ms-settings:yourinfo")
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    RoundButton {
                        glyph: ""
                        on: root.openList === "settings"
                        onClicked: {
                            root.toggleList("settings");
                            S.ControlCenter.cancelAction();
                        }
                    }

                    RoundButton {
                        glyph: ""
                        on: root.openList === "power"
                        onClicked: {
                            root.toggleList("power");
                            S.ControlCenter.cancelAction();
                        }
                    }
                }
            }

            OptionList {
                visible: root.openList === "settings"
                width: parent.width
                choices: root.settingsPages
                onPicked: id => {
                    root.openList = "";
                    panel.close();
                    S.ControlCenter.openSettings(id);
                }
            }

            // Sign out, restart and shut down ask again on the same row.
            OptionList {
                visible: root.openList === "power"
                width: parent.width
                pending: S.ControlCenter.pendingAction
                choices: [
                    {
                        "id": "lock",
                        "name": "Lock"
                    },
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
                        "name": S.ControlCenter.pendingAction === "signOut" ? "Click again to sign out" : "Sign out"
                    },
                    {
                        "id": "restart",
                        "name": S.ControlCenter.pendingAction === "restart" ? "Click again to restart" : "Restart"
                    },
                    {
                        "id": "shutdown",
                        "name": S.ControlCenter.pendingAction === "shutdown" ? "Click again to shut down" : "Shut down"
                    }
                ]
                onPicked: id => root.runAction(id)
            }
        }
    }

    Component {
        id: quickActionsSection

        Section {
            lineBelow: true

            Grid {
                id: quickGrid

                readonly property int count: Math.max(1, root.columns)

                columns: count

                Repeater {
                    model: root.quickActions

                    delegate: Item {
                        id: quick

                        required property string modelData

                        readonly property bool active: root.quickActive(modelData)
                        readonly property bool usable: root.quickEnabled(modelData)

                        width: (root.panelWidth - 24) / quickGrid.count
                        height: tile.height + caption.implicitHeight + 12

                        Rectangle {
                            id: tile
                            x: 4
                            width: parent.width - 8
                            height: 48
                            radius: 6
                            color: quick.active ? (tileHover.hovered ? Qt.darker(Theme.accent, 1.1) : Theme.accent) : (tileHover.hovered && quick.usable ? root.tileFillHover : root.tileFill)
                            border.width: 1
                            border.color: quick.active ? color : (tileHover.hovered && quick.usable ? root.tileBorderHover : root.tileBorder)
                            opacity: !quick.usable ? 0.5 : (tileTap.pressed ? 0.5 : 1)

                            Glyph {
                                anchors.centerIn: parent
                                color: quick.active ? Theme.onAccent : Theme.text
                                text: root.quickGlyphs[quick.modelData] ?? ""
                            }

                            HoverHandler {
                                id: tileHover
                            }

                            TapHandler {
                                id: tileTap
                                onTapped: {
                                    if (quick.usable)
                                        root.runQuick(quick.modelData);
                                }
                            }
                        }

                        Text {
                            id: caption
                            x: 4
                            y: tile.height + 4
                            width: parent.width - 8
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            opacity: quick.usable ? 1 : 0.5
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            text: root.quickNames[quick.modelData] ?? quick.modelData
                        }
                    }
                }
            }
        }
    }

    Component {
        id: slidersSection

        Section {
            padSide: 20
            lineBelow: true

            SliderRow {
                visible: root.showBrightness && S.ControlCenter.brightnessAvailable
                name: "brightness"
                icon: ""
                sliderName: "brightnessSlider"
                value: S.ControlCenter.brightness
                showChoice: root.showSourceSelectors && S.ControlCenter.brightnessSources.length > 1
                choices: S.ControlCenter.brightnessSources
                activeId: S.ControlCenter.brightnessSource
                onMoved: v => S.ControlCenter.setBrightness(Math.round(v))
                onPicked: id => S.ControlCenter.setBrightnessSource(id)
            }

            SliderRow {
                visible: root.showVolume && S.AudioOutput.available
                name: "volume"
                icon: S.AudioOutput.muted ? "" : ""
                sliderName: "volumeSlider"
                value: S.AudioOutput.volume
                showChoice: root.showSourceSelectors
                choices: S.AudioOutput.devices
                activeId: S.AudioOutput.defaultDevice
                onMoved: v => S.AudioOutput.volume = Math.round(v)
                onPicked: id => S.AudioOutput.setDefaultDevice(id)
            }

            SliderRow {
                visible: root.showMicrophone && S.AudioInput.available
                name: "microphone"
                icon: ""
                sliderName: "microphoneSlider"
                value: S.AudioInput.volume
                showChoice: root.showSourceSelectors
                choices: S.AudioInput.devices
                activeId: S.AudioInput.defaultDevice
                onMoved: v => S.AudioInput.volume = Math.round(v)
                onPicked: id => S.AudioInput.setDefaultDevice(id)
            }
        }
    }

    Component {
        id: powerSection

        Section {
            Row {
                Item {
                    width: 4
                    height: 1
                }

                Card {
                    width: (root.panelWidth - 24) / 2 - 8
                    title: root.powerPlanTitle
                    current: S.ControlCenter.powerPlanName !== "" ? S.ControlCenter.powerPlanName : "Unknown"
                    onClicked: root.toggleList("plan")
                }

                Item {
                    width: 8
                    height: 1
                }

                Card {
                    width: (root.panelWidth - 24) / 2 - 8
                    title: root.powerModeTitle
                    usable: S.ControlCenter.powerModeAvailable
                    current: S.ControlCenter.powerModeName !== "" ? S.ControlCenter.powerModeName : "Unknown"
                    onClicked: root.toggleList("mode")
                }
            }

            OptionList {
                visible: root.openList === "plan"
                x: 4
                width: parent.width - 8
                choices: S.ControlCenter.powerPlans
                onPicked: id => {
                    root.openList = "";
                    S.ControlCenter.setPowerPlan(id);
                }
            }

            OptionList {
                visible: root.openList === "mode"
                x: 4
                width: parent.width - 8
                choices: S.ControlCenter.powerModes
                onPicked: id => {
                    root.openList = "";
                    S.ControlCenter.setPowerMode(id);
                }
            }
        }
    }

    Component {
        id: mediaSection

        Section {
            padTop: 12
            padBottom: 12
            padSide: 12
            lineAbove: true
            tinted: true

            Row {
                id: mediaRow

                readonly property bool hasArt: S.Media.thumbnailUrl !== ""

                width: parent.width
                spacing: 12

                Image {
                    visible: mediaRow.hasArt
                    width: root.thumbnailSize
                    height: root.thumbnailSize
                    source: S.Media.thumbnailUrl
                    sourceSize: Qt.size(root.thumbnailSize * 2, root.thumbnailSize * 2)
                    fillMode: Image.PreserveAspectCrop
                }

                Column {
                    width: mediaRow.width - 3 * 32 - 2 * 4 - 2 * 12 - (mediaRow.hasArt ? root.thumbnailSize + 12 : 0)
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        text: S.Media.title
                    }

                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        color: root.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        text: S.Media.artist
                    }
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Repeater {
                        model: [
                            {
                                "glyph": "",
                                "ok": S.Media.canPrevious,
                                "act": 0
                            },
                            {
                                "glyph": S.Media.playing ? "" : "",
                                "ok": S.Media.canPlayPause,
                                "act": 1
                            },
                            {
                                "glyph": "",
                                "ok": S.Media.canNext,
                                "act": 2
                            }
                        ]

                        delegate: Rectangle {
                            id: mediaButton

                            required property var modelData

                            width: 32
                            height: 32
                            radius: 6
                            color: mediaHover.hovered && modelData.ok ? Qt.alpha(Theme.text, 0.06) : "transparent"
                            opacity: modelData.ok ? (mediaTap.pressed ? 0.5 : 1) : 0.5

                            Glyph {
                                anchors.centerIn: parent
                                size: 14
                                text: mediaButton.modelData.glyph
                            }

                            HoverHandler {
                                id: mediaHover
                            }

                            TapHandler {
                                id: mediaTap
                                onTapped: {
                                    if (!mediaButton.modelData.ok)
                                        return;
                                    if (mediaButton.modelData.act === 0)
                                        S.Media.previous();
                                    else if (mediaButton.modelData.act === 1)
                                        S.Media.playPause();
                                    else
                                        S.Media.next();
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    PopupWindow {
        id: panel
        objectName: "controlCenterPopup"
        anchorItem: root
        padding: 0
        spacing: 0

        // Read the machine again each time the panel opens; a half-confirmed shutdown never
        // survives closing it.
        onOpened: S.ControlCenter.refresh()
        onClosed: {
            S.ControlCenter.cancelAction();
            root.openList = "";
        }

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
