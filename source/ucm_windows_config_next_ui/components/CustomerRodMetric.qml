import QtQuick
import QtQuick.Layouts
import "CustomerUnits.js" as CustomerUnits
import "AppPalette.js" as Palette

Item {
    id: root
    property int rodNumber: 1
    property string newtonsText: "--"
    property string microstrainValueText: "--"
    property string stateText: "等待设备数据"
    property real rodDiameterMm: 0
    property color accent: "#245AA8"
    readonly property real forceN: CustomerUnits.validForceN(newtonsText)
    readonly property bool forceValid: isFinite(forceN)
    readonly property bool diameterValid: isFinite(rodDiameterMm)
                                                   && rodDiameterMm > 0

    function tonneText() {
        return CustomerUnits.tonneText(newtonsText)
    }

    function kilonewtonText() {
        return CustomerUnits.kilonewtonText(newtonsText)
    }

    function microstrainText() {
        return microstrainValueText
    }

    Rectangle {
        anchors.fill: parent
        radius: 14
        color: Palette.surfaceMuted
        border.color: Palette.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 1

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Rectangle {
                width: 22
                height: 22
                radius: 11
                color: Palette.surface
                border.color: root.accent
                border.width: 1.5
                Text {
                    anchors.centerIn: parent
                    text: root.rodNumber
                    color: root.accent
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }
            Text {
                text: "拉杆 " + root.rodNumber
                color: Palette.secondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 7
                height: 7
                radius: 4
                color: root.forceValid ? Palette.green : Palette.tertiary
            }
        }

        Row {
            spacing: 6
            Text {
                text: root.tonneText()
                color: Palette.text
                font.family: "Segoe UI Variable Display"
                font.pixelSize: 29
                font.weight: Font.DemiBold
            }
            Text {
                anchors.baseline: parent.children[0].baseline
                text: "T"
                color: Palette.secondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 16
            }
        }

        Text {
            text: root.kilonewtonText() + " kN"
            color: Palette.secondary
            font.family: "Segoe UI Variable Display"
            font.pixelSize: 15
        }
        Text {
            text: root.microstrainText() + " με"
            color: Palette.tertiary
            font.family: "Segoe UI Variable Display"
            font.pixelSize: 15
        }
        Text {
            Layout.fillWidth: true
            text: root.stateText
            color: root.forceValid ? Palette.tertiary : "#A5ABB3"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }
}
