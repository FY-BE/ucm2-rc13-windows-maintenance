import QtQuick
import QtQuick.Controls
import "AppPalette.js" as Palette

Button {
    id: root
    property bool primary: false
    property bool compact: false

    implicitWidth: Math.max(compact ? 106 : 260, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: compact ? 38 : 52
    hoverEnabled: true
    leftPadding: compact ? 18 : 26
    rightPadding: leftPadding

    contentItem: Text {
        text: root.text
        color: !root.enabled ? Palette.tertiary
                             : root.primary ? Palette.surface : Palette.blue
        font.family: "Microsoft YaHei UI"
        font.pixelSize: root.compact ? 13 : 16
        font.weight: root.primary ? Font.DemiBold : Font.Medium
        font.letterSpacing: 0
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: root.compact ? 11 : 14
        color: !root.enabled ? Palette.canvas
                             : root.primary
                               ? (root.down ? Palette.bluePressed : Palette.blue)
                               : (root.down ? "#D9E9FF"
                                            : root.hovered ? Palette.blueTint
                                                           : Palette.surface)
        border.width: root.primary || !root.enabled ? 0 : 1
        border.color: root.hovered ? Palette.blue : Palette.borderStrong

        Behavior on color { ColorAnimation { duration: 110 } }
    }
}
