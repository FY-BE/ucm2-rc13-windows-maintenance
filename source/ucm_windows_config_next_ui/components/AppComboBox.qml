import QtQuick
import QtQuick.Controls.Basic
import "AppPalette.js" as Palette

ComboBox {
    id: root
    implicitHeight: 42
    leftPadding: 14
    rightPadding: 34
    font.family: "Microsoft YaHei UI"
    font.pixelSize: 13
    contentItem: Text {
        leftPadding: root.leftPadding
        rightPadding: root.rightPadding
        text: root.displayText
        color: root.enabled ? Palette.text : Palette.tertiary
        font: root.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: root.width - width - 14
        anchors.verticalCenter: parent.verticalCenter
        text: "⌄"
        color: Palette.blue
        font.pixelSize: 20
        font.weight: Font.DemiBold
    }
    background: Rectangle {
        radius: 12
        color: root.enabled ? Palette.surface : Palette.canvas
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Palette.blue : Palette.borderStrong
    }
}
