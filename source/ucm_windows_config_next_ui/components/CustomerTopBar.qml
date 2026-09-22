import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "AppPalette.js" as Palette

Rectangle {
    id: root
    property var backendObject
    property int currentPage: 0
    signal pageRequested(int page)
    signal engineerRequested()

    color: Palette.surface
    border.color: Palette.border
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 8

        RowLayout {
            Layout.preferredWidth: 245
            Layout.fillHeight: true
            spacing: 12

            Image {
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                source: "../assets/qizhen-ucm-icon.png"
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            Column {
                spacing: 1
                Text {
                    text: "启真传感"
                    color: Palette.text
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "QIZHEN SENSING"
                    color: Palette.tertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 8
                    font.letterSpacing: 1.3
                    font.weight: Font.DemiBold
                }
            }
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            Layout.preferredWidth: 110
            Layout.preferredHeight: 32
            radius: 16
            color: Palette.surfaceMuted
            border.color: Palette.border

            Row {
                anchors.centerIn: parent
                spacing: 7
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: root.backendObject && root.backendObject.connected
                           ? Palette.green : Palette.orange
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: root.backendObject && root.backendObject.connected
                          ? "设备在线" : "设备离线"
                    color: Palette.secondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 11
                }
            }
        }

        CustomerTopNavButton {
            iconSource: "../assets/customer-home.svg"
            label: "首页"
            selected: root.currentPage === 0
            onClicked: root.pageRequested(0)
        }
        CustomerTopNavButton {
            iconSource: "../assets/customer-settings.svg"
            label: "设置"
            selected: root.currentPage === 1
            onClicked: root.pageRequested(1)
        }
        CustomerTopNavButton {
            iconSource: "../assets/customer-info.svg"
            label: "信息"
            selected: root.currentPage === 2
            onClicked: root.pageRequested(2)
        }
        CustomerTopNavButton {
            iconSource: "../assets/customer-engineer.svg"
            label: "工程师模式"
            selected: false
            onClicked: root.engineerRequested()
        }
    }
}
