// A picture gallery and a live window preview, built from two engine types meant for ported
// shells: `Thumbnails` (which the wallpaper gallery also uses) and `WindowThumbnail` (which no
// built-in widget uses). Run it with
//
//     waku --config-home examples/gallery
//
// `Thumbnails.url(path, width)` gives an `image://thumb` URL: a downscaled copy of the file,
// decoded off the GUI thread and cached in memory (and in %LOCALAPPDATA%\waku\thumbs on the live
// desktop). `Thumbnails.prewarm(paths, width)` fills the cache ahead of time.
//
// `WindowThumbnail` is a live DWM preview of another window. Set `previewHwnd` to the handle of a
// top-level window, for example `(Get-Process notepad).MainWindowHandle` in PowerShell. The
// compositor draws it above the window, so nothing can be drawn on top of it.
import QtQuick
import waku
import waku.services

ShellRoot {
    FloatingWindow {
        id: gallery

        // The pictures to show: two of the wallpapers Windows ships.
        property list<string> images: {
            const folder = Shell.env("SystemRoot") + "/Web/Wallpaper/Windows/";
            return [folder + "img0.jpg", folder + "img19.jpg"];
        }
        // The window to preview, as a number; 0 shows the hint instead.
        property real previewHwnd: 0

        name: "gallery"
        title: "waku gallery"
        implicitWidth: 720
        implicitHeight: 440
        color: "#202020"
        Component.onCompleted: Thumbnails.prewarm(images, 340)

        Grid {
            id: grid

            x: 12
            y: 12
            columns: 2
            spacing: 8

            Repeater {
                model: gallery.images

                Image {
                    required property string modelData

                    objectName: "thumb"
                    width: 340
                    height: 190
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    source: Thumbnails.url(modelData, 340)
                }
            }
        }

        WindowThumbnail {
            id: preview

            objectName: "preview"
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                margins: 12
            }
            height: 200
            hwnd: gallery.previewHwnd
        }

        Text {
            anchors.centerIn: preview
            visible: !preview.available
            color: "#c0c0c0"
            text: "Set previewHwnd to a window handle to see a live preview here"
        }
    }
}
