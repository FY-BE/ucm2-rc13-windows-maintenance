import QtQuick
import QtQuick.Layouts
import "AppPalette.js" as Palette

Item {
    id: root
    property string title: ""
    property string subtitle: ""
    property string stageText: ""
    property string detailText: ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        Column {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            spacing: 3
            Text {
                text: root.title
                color: Palette.text
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 28
                font.weight: Font.DemiBold
            }
            Text {
                text: root.subtitle
                color: Palette.secondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 12
            }
        }

        GlassPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 80, 520)
                spacing: 14
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 58
                    height: 58
                    radius: 20
                    color: "#160B4E9E"
                    border.color: "#260B4E9E"
                    Text {
                        anchors.centerIn: parent
                        text: "QZ"
                        color: Palette.blue
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 18
                        font.weight: Font.Bold
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.stageText
                    color: Palette.text
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: root.detailText
                    horizontalAlignment: Text.AlignHCenter
                    color: "#857E78"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    lineHeight: 1.35
                }
            }
        }
    }
}
