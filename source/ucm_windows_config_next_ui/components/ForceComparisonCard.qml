import QtQuick
import "AppPalette.js" as Palette

GlassPanel {
    id: root
    performanceSensitive: true
    property string title: ""
    property string ucmValue: "--"
    property string referenceValue: "--"
    property string comparisonText: "等待两路有效值"
    property string stateText: ""
    property color accent: Palette.blue
    property bool showReference: true

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Row {
            spacing: 8
            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: root.accent
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: root.title
                color: Palette.secondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
        }

        Row {
            width: parent.width
            spacing: 12
            Column {
                width: root.showReference ? (parent.width - 24) * 0.53
                                          : parent.width
                spacing: 2
                Text {
                    text: root.showReference ? "UCM 测量" : "当前拉力"
                    color: Palette.tertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 11
                }
                Row {
                    spacing: 5
                    Text {
                        text: root.ucmValue
                        color: Palette.text
                        font.family: "Segoe UI Variable Display"
                        font.pixelSize: root.showReference ? 29 : 34
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "N"
                        color: Palette.tertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 11
                        anchors.baseline: parent.children[0].baseline
                    }
                }
            }
            Rectangle {
                visible: root.showReference
                width: 1
                height: 48
                color: "#D9E2EC"
            }
            Column {
                visible: root.showReference
                width: (parent.width - 24) * 0.47
                spacing: 2
                Text {
                    text: "标准力"
                    color: Palette.tertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 11
                }
                Row {
                    spacing: 5
                    Text {
                        text: root.referenceValue
                        color: root.referenceValue === "--" ? "#A8B1BE" : Palette.blue
                        font.family: "Segoe UI Variable Display"
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "kN"
                        color: Palette.tertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 11
                        anchors.baseline: parent.children[0].baseline
                    }
                }
            }
        }

        Text {
            visible: root.showReference
            width: parent.width
            text: root.comparisonText
            color: root.comparisonText.startsWith("差") ? Palette.blue : "#8A96A6"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 10
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: root.stateText
            color: "#8995A5"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 15
        width: 50
        height: 25
        radius: 8
        color: root.ucmValue === "--" ? Palette.surfaceMuted : "#E8F7F0"
        Text {
            anchors.centerIn: parent
            text: root.ucmValue === "--" ? "待测" : "有数据"
            color: root.ucmValue === "--" ? Palette.tertiary : Palette.green
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }
}
