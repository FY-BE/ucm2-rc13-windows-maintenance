import QtQuick
import "AppPalette.js" as Palette

Rectangle {
    id: root
    property string iconText: ""
    property string label: ""
    property bool selected: false
    signal clicked

    implicitHeight: 48
    radius: 14
    color: selected ? Palette.blueTint : mouseArea.containsMouse
                               ? Palette.canvas : "transparent"
    border.color: selected ? "#D2E5FF" : "transparent"

    Behavior on color { ColorAnimation { duration: 140 } }

    Row {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 12
        Rectangle {
            width: 28
            height: 28
            radius: 10
            color: root.selected ? Palette.blue : Palette.canvas
            anchors.verticalCenter: parent.verticalCenter
            Text {
                anchors.centerIn: parent
                text: root.iconText
                color: root.selected ? Palette.surface : Palette.secondary
                font.family: "Segoe UI Symbol"
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }
        Text {
            text: root.label
            color: root.selected ? Palette.blue : Palette.text
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 14
            font.weight: root.selected ? Font.DemiBold : Font.Medium
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
