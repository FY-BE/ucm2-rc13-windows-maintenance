import QtQuick
import QtQuick.Controls
import "AppPalette.js" as Palette

Control {
    id: root
    property url iconSource
    property string label: ""
    property bool selected: false
    signal clicked()

    implicitWidth: label.length > 4 ? 132 : 86
    implicitHeight: 46
    hoverEnabled: true

    background: Rectangle {
        radius: 14
        color: root.selected ? Palette.blueTint
                             : root.hovered ? Palette.canvas : "transparent"
        Behavior on color { ColorAnimation { duration: 130 } }
    }

    contentItem: Row {
        spacing: 8
        anchors.centerIn: parent
        Image {
            width: 21
            height: 21
            source: root.iconSource
            fillMode: Image.PreserveAspectFit
            smooth: true
            opacity: root.selected ? 1.0 : 0.72
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.label
            color: root.selected ? Palette.blue : Palette.secondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 14
            font.weight: root.selected ? Font.DemiBold : Font.Medium
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    TapHandler {
        onTapped: root.clicked()
    }

    ToolTip.visible: root.hovered
    ToolTip.text: root.label
    ToolTip.delay: 450
}
