import QtQuick
import QtQuick.Controls.Basic
import "AppPalette.js" as Palette

Button {
    id: root
    property bool primary: false

    implicitHeight: 38
    implicitWidth: Math.max(86, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: 14
    rightPadding: 14
    hoverEnabled: true

    contentItem: Text {
        text: root.text
        color: !root.enabled ? Palette.tertiary : root.primary ? Palette.surface : Palette.blue
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 13
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 12
        color: !root.enabled ? Palette.canvas
                             : root.primary ? (root.down ? Palette.bluePressed : Palette.blue)
                                            : root.down ? "#D9E9FF"
                                                        : root.hovered ? Palette.blueTint : Palette.surface
        border.width: root.primary ? 0 : 1
        border.color: Palette.borderStrong
        Behavior on color { ColorAnimation { duration: 130 } }
    }
}
