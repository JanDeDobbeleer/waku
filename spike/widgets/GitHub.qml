import QtQuick
import tidy
import tidy.services as S

// GitHub notifications. The pill shows a glyph and a dot while there are unread notifications. A
// click opens a list grouped by category (state icon, title, repository and age, comment count;
// click a row to mark it read and open it in the browser, "Mark all as read" in the footer), a
// right click toggles the alternate label. Hovering shows the unread count. Signed out, the popup
// offers "Sign in": GitHub's device flow shows a code to enter at github.com/login/device. The
// token is the one the Python build saved (or YASB_GITHUB_TOKEN); it never reaches QML.
//
//     GitHub { interval: 600000; maxNotification: 30; categoriesOrder: ["Issue", "PullRequest"] }
//
// `label` is the pill text, `labelAlt` the alternate one; {data} is the unread count. `interval`
// is the poll period in milliseconds. `showCategories` groups the list in `categoriesOrder` (other
// categories follow). `maxFieldSize` cuts titles and descriptions at that many characters.
// `showComments` fetches and shows each item's comment count (`commentGlyph` precedes it).
// `dotCorner` is top_left, top_right, bottom_left or bottom_right; the margins are in pixels.
// `hideEmpty` hides the widget while nothing is unread and the user is signed in.
Pill {
    id: root

    property string label: ""
    property string labelAlt: "Notifications {data}"
    property bool showAlt: false
    property int interval: 600000
    property int maxNotification: 30
    property bool onlyUnread: true
    property bool showComments: true
    property int maxFieldSize: 54
    property bool showCategories: true
    property var categoriesOrder: ["Issue", "PullRequest", "CheckSuite", "Release", "Discussion"]
    property var reasonFilters: []
    property bool hideEmpty: false
    property string commentGlyph: ""
    property bool dotEnabled: true
    property string dotCorner: "top_right"
    property color dotColor: "#ef4747"
    property int dotMarginX: 1
    property int dotMarginY: 1

    readonly property var sections: showCategories ? S.GitHub.groups : [{
            "type": "",
            "title": "",
            "items": S.GitHub.items
        }]

    visible: S.GitHub.available && !(hideEmpty && S.GitHub.signedIn && S.GitHub.unreadCount === 0)
    text: (showAlt ? labelAlt : label).split("{data}").join(S.GitHub.unreadCount)
    tooltip: !S.GitHub.signedIn ? "GitHub: not signed in" : S.GitHub.error !== "" ? S.GitHub.error : "Notifications " + S.GitHub.unreadCount

    onClicked: panel.toggle()
    onRightClicked: showAlt = !showAlt

    Binding {
        target: S.GitHub
        property: "interval"
        value: root.interval
    }
    Binding {
        target: S.GitHub
        property: "maxNotification"
        value: root.maxNotification
    }
    Binding {
        target: S.GitHub
        property: "onlyUnread"
        value: root.onlyUnread
    }
    Binding {
        target: S.GitHub
        property: "showComments"
        value: root.showComments
    }
    Binding {
        target: S.GitHub
        property: "reasonFilters"
        value: root.reasonFilters
    }
    Binding {
        target: S.GitHub
        property: "categoriesOrder"
        value: root.categoriesOrder
    }

    Rectangle {
        objectName: "githubDot"
        parent: root
        z: 1
        width: 6
        height: 6
        radius: 3
        color: root.dotColor
        visible: root.dotEnabled && S.GitHub.unreadCount > 0
        x: root.dotCorner.endsWith("left") ? root.dotMarginX : root.width - width - root.dotMarginX
        y: root.dotCorner.startsWith("top") ? root.dotMarginY : root.height - height - root.dotMarginY
    }

    PopupWindow {
        id: panel
        objectName: "githubPopup"
        anchorItem: root
        spacing: 8

        onOpened: S.GitHub.refresh()

        Text {
            visible: !S.GitHub.signedIn
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: S.GitHub.signingIn && S.GitHub.userCode !== "" ? "Enter this code at github.com/login/device" : "Sign in to GitHub to see your notifications"
        }

        Text {
            objectName: "githubUserCode"
            visible: !S.GitHub.signedIn && S.GitHub.userCode !== ""
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 24
            font.bold: true
            text: S.GitHub.userCode
        }

        Text {
            visible: !S.GitHub.signedIn && S.GitHub.signingIn && S.GitHub.userCode === ""
            color: Theme.text
            opacity: 0.6
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            text: "Connecting to GitHub..."
        }

        Text {
            visible: !S.GitHub.signedIn && S.GitHub.signInError !== ""
            width: 320
            wrapMode: Text.Wrap
            color: Theme.text
            opacity: 0.8
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            text: S.GitHub.signInError
        }

        Row {
            visible: !S.GitHub.signedIn
            spacing: 8

            Pill {
                objectName: "githubSignIn"
                visible: !S.GitHub.signingIn
                text: "Sign in"
                onClicked: S.GitHub.signIn()
            }

            Pill {
                objectName: "githubOpenBrowser"
                visible: S.GitHub.userCode !== ""
                text: "Open GitHub"
                onClicked: S.Shell.open(S.GitHub.verificationUri)
            }

            Pill {
                objectName: "githubCancel"
                visible: S.GitHub.signingIn
                text: "Cancel"
                onClicked: S.GitHub.cancelSignIn()
            }
        }

        Text {
            visible: S.GitHub.signedIn && S.GitHub.error !== ""
            width: 340
            wrapMode: Text.Wrap
            color: Theme.text
            opacity: 0.8
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            text: S.GitHub.error
        }

        Text {
            visible: S.GitHub.signedIn && S.GitHub.items.length === 0 && S.GitHub.error === ""
            color: Theme.text
            opacity: 0.6
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: root.onlyUnread ? "No unread notifications" : "No notifications"
        }

        Column {
            visible: S.GitHub.signedIn && S.GitHub.items.length > 0
            spacing: 6

            Repeater {
                model: root.sections

                delegate: Column {
                    id: section
                    required property var modelData

                    spacing: 2

                    Text {
                        visible: section.modelData.title !== ""
                        color: Theme.text
                        opacity: 0.6
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallFontSize
                        font.bold: true
                        text: section.modelData.title
                    }

                    Repeater {
                        model: section.modelData.items

                        delegate: Rectangle {
                            id: card
                            required property var modelData

                            objectName: "githubItem"
                            width: 340
                            height: Math.max(body.implicitHeight, 24) + 12
                            radius: 6
                            color: cardArea.containsMouse ? Theme.fillHover : Theme.fill

                            MouseArea {
                                id: cardArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    const url = S.GitHub.open(card.modelData.id);
                                    if (url !== "")
                                        S.Shell.open(url);
                                    panel.close();
                                }
                            }

                            Text {
                                id: icon
                                x: 8
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.text
                                opacity: card.modelData.unread ? 1 : 0.5
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                                text: card.modelData.icon
                            }

                            Column {
                                id: body
                                x: 32
                                y: 6
                                width: parent.width - 40 - (comments.visible ? comments.width + 8 : 0)
                                spacing: 1

                                Text {
                                    width: parent.width
                                    elide: Text.ElideRight
                                    color: Theme.text
                                    opacity: card.modelData.unread ? 1 : 0.6
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                    font.bold: card.modelData.unread
                                    text: S.GitHub.truncate(card.modelData.title, root.maxFieldSize)
                                }

                                Text {
                                    width: parent.width
                                    elide: Text.ElideRight
                                    color: Theme.text
                                    opacity: 0.6
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.smallFontSize
                                    text: S.GitHub.truncate((root.showCategories ? "" : card.modelData.type + " • ") + card.modelData.repo + (card.modelData.time !== "" ? " • Updated " + card.modelData.time : ""), root.maxFieldSize)
                                }
                            }

                            Text {
                                id: comments
                                visible: root.showComments && card.modelData.comments >= 0
                                x: parent.width - width - 8
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.text
                                opacity: 0.7
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.smallFontSize
                                text: (root.commentGlyph !== "" ? root.commentGlyph + " " : "") + card.modelData.comments
                            }
                        }
                    }
                }
            }
        }

        Row {
            visible: S.GitHub.signedIn && S.GitHub.items.length > 0
            spacing: 12

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.text
                opacity: 0.6
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
                text: "Unread notifications (" + S.GitHub.unreadCount + ")"
            }

            Pill {
                objectName: "githubMarkAll"
                text: "Mark all as read"
                onClicked: {
                    S.GitHub.markAllRead();
                    panel.close();
                }
            }
        }
    }
}
