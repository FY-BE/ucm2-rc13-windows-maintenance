import QtQuick
import "AppPalette.js" as Palette

GlassPanel {
    id: root
    performanceSensitive: true
    property string title: ""
    property string value: "--"
    property string unit: ""
    property string subtitle: ""
    property color accent: Palette.blue
    property bool hero: false
    property string symbol: "▮"

    Column {
        anchors.fill: parent
        anchors.margins: 17
        spacing: 7

        Row {
            spacing: 9
            Rectangle {
                width: 34
                height: 34
                radius: 10
                color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.11)
                Text {
                    anchors.centerIn: parent
                    text: root.symbol
                    color: root.accent
                    font.family: "Segoe UI Symbol"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
            }
            Text {
                text: root.title
                color: Palette.secondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Row {
            spacing: 8
            Text {
                text: root.value
                color: Palette.text
                font.family: root.title === "过程状态" ? "Microsoft YaHei UI" : "Segoe UI Variable Display"
                font.pixelSize: root.hero ? 40
                                            : root.value.length > 9 ? 23
                                            : root.value.length > 7 ? 27 : 32
                font.weight: Font.DemiBold
            }
            Text {
                text: root.unit
                color: Palette.tertiary
                font.family: "Segoe UI Variable Display"
                font.pixelSize: root.hero ? 18 : 14
                anchors.baseline: parent.children[0].baseline
            }
        }

        Text {
            text: root.subtitle
            color: Palette.tertiary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            width: parent.width
        }
    }
}
