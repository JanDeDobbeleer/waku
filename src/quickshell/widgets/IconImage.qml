import QtQuick

// Quickshell.Widgets IconImage: an image for an icon, square and centred.
//
//     IconImage { implicitSize: 24; source: Quickshell.iconPath("firefox") }
//
//   source        an image:// url, a file url, or a Windows path (C:\...). A bare icon name has no
//                 image provider here and fails (status Error)
//   implicitSize  the size the item asks for (width and height)
//   actualSize    the side of the square the icon is drawn in: the smaller of width and height
//   asynchronous, mipmap  as on Image
//   backer        the Image behind it, status its load status
Item {
    id: root

    final property string source: ""
    final property real implicitSize: 0
    final property bool asynchronous: false
    final property bool mipmap: false
    final readonly property real actualSize: Math.min(width, height)
    final readonly property Image backer: image
    final readonly property int status: image.status

    implicitWidth: implicitSize
    implicitHeight: implicitSize

    function resolve(src) {
        if (src.length === 0)
            return "";
        if (/^[A-Za-z]:[\\/]/.test(src))
            return "file:///" + src.replace(/\\/g, "/");
        return src;
    }

    Image {
        id: image
        anchors.centerIn: parent
        width: root.actualSize
        height: root.actualSize
        source: root.resolve(root.source)
        sourceSize.width: Math.max(1, Math.round(root.actualSize * Screen.devicePixelRatio))
        sourceSize.height: Math.max(1, Math.round(root.actualSize * Screen.devicePixelRatio))
        fillMode: Image.PreserveAspectFit
        asynchronous: root.asynchronous
        mipmap: root.mipmap
        smooth: true
    }
}
