import QtQuick
import wabar
import wabar.services as S

// The wallpaper changer. A hotkey (default alt+e) or a click on the button opens a gallery of the
// pictures in `imagePath`; clicking one sets it as the wallpaper on every monitor. A right click
// picks a random wallpaper. The button shows `label` and is hidden when it is empty, but the
// widget stays loaded so the hotkey keeps working.
//
//     Wallpapers { imagePath: "C:/Users/me/Pictures/Wallpapers"; label: "🖼" }
//
// `hotkey` is "" for none; `hotkeyScreen` is the screen the gallery opens on: "cursor" (default),
// "primary" or "active" (the focused window's screen). `autoChange` is a number of seconds
// between random wallpapers, 0 for never. `imageWidth`, `orientation` ("portrait" or
// "landscape"), `cornerRadius` and `accentColor` (the border of the current wallpaper) style the
// gallery, a strip of thumbnails. The animated change (`engine`) and the other gallery types are
// not supported yet: the wallpaper changes at once.
Pill {
    id: root

    property string label: ""
    property string imagePath: ""
    property string hotkey: "alt+e"
    property string hotkeyScreen: "cursor"
    property int autoChange: 0
    property int imageWidth: 240
    property string orientation: "portrait"
    property int cornerRadius: 0
    property color accentColor: "#ffffff"

    function sync() {
        S.Wallpapers.configure(imagePath, hotkey, hotkeyScreen, autoChange);
    }

    visible: label !== "" && S.Wallpapers.available
    text: label
    tooltip: "Change wallpaper"

    onImagePathChanged: sync()
    onHotkeyChanged: sync()
    onHotkeyScreenChanged: sync()
    onAutoChangeChanged: sync()
    Component.onCompleted: sync()

    onClicked: S.Wallpapers.toggleGallery()
    onRightClicked: S.Wallpapers.random()

    WallpaperGallery {
        imageWidth: root.imageWidth
        orientation: root.orientation
        cornerRadius: root.cornerRadius
        accentColor: root.accentColor
    }
}
