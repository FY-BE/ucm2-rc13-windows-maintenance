import QtQuick
import QtQuick.Controls.Basic
import "AppPalette.js" as Palette

TextField {
    id: root
    implicitHeight: 42
    leftPadding: 14
    rightPadding: 14
    color: Palette.text
    placeholderTextColor: Palette.tertiary
    selectionColor: "#B8D8FF"
    selectedTextColor: Palette.text
    font.family: "Microsoft YaHei UI"
    font.pixelSize: 13
    background: Rectangle {
        radius: 12
        color: root.enabled ? Palette.surface : Palette.canvas
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Palette.blue : Palette.borderStrong
    }
}
