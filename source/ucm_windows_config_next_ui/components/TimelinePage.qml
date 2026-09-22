import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "AppPalette.js" as Palette

Item {
    id: root
    signal backRequested()
    property bool engineerMode: false
    property var events: []
    property string statusText: ""
    signal refreshRequested()

    readonly property var headerColumns: engineerMode ? [
        ["生命周期", 84], ["事件身份", 174], ["级别", 74],
        ["原因", 230], ["阶段 / 范围", 168], ["过程", 96],
        ["正式输出 / AGC", 1]
    ] : [
        ["时间", 170], ["级别", 92], ["事件", 1], ["设备状态", 190]
    ]

    function rowColumns(event) {
        return engineerMode ? [
            [event.lifecycle, 84],
            [event.identity + "\n" + event.timestamp, 174],
            [event.severity, 74], [event.reason, 230],
            [event.stageScope, 168], [event.process, 96],
            [event.formalAgc, 1]
        ] : [
            [event.timestamp, 170], [event.severity, 92],
            [event.customerReason, 1], [event.process, 190]
        ]
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            Column {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: root.engineerMode ? "事件时间线" : "历史事件"
                    color: Palette.text
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 27
                    font.weight: Font.DemiBold
                }
                Text {
                    text: root.engineerMode
                          ? "R3 diagnostics 固定 13 列 CSV · 新事件优先"
                          : "设备报警与状态变化 · 新事件优先"
                    color: Palette.secondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 11
                }
            }
            AppButton {
                visible: root.engineerMode
                text: "返回日志与事件"
                Layout.preferredWidth: 128
                Layout.preferredHeight: 36
                onClicked: root.backRequested()
            }
            AppButton {
                text: root.engineerMode ? "刷新 source 4" : "刷新"
                Layout.preferredWidth: root.engineerMode ? 132 : 86
                Layout.preferredHeight: 36
                onClicked: root.refreshRequested()
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 11
                    color: parent.down ? "#0C3F87"
                                       : parent.hovered ? "#2369C8" : Palette.blue
                }
            }
        }

        GlassPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            Layout.minimumHeight: 58
            Layout.maximumHeight: 58
            elevated: false
            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                Rectangle {
                    width: 9; height: 9; radius: 5
                    color: root.events.length > 0 ? Palette.green : Palette.orange
                }
                Text {
                    Layout.fillWidth: true
                    text: root.engineerMode ? root.statusText
                                            : root.events.length > 0
                                              ? "历史事件已更新"
                                              : "当前没有可显示的历史事件"
                    color: Palette.secondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
                Text {
                    visible: root.engineerMode
                    text: "只读 · 最大 64 KiB · 严格枚举"
                    color: "#8A96A6"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 9
                }
            }
        }

        GlassPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    color: Palette.blue
                    radius: 11
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 12
                        Repeater {
                            model: root.headerColumns
                            delegate: Text {
                                Layout.preferredWidth: modelData[1] === 1 ? -1 : modelData[1]
                                Layout.fillWidth: modelData[1] === 1
                                text: modelData[0]
                                color: "white"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.events.length === 0
                    Column {
                        anchors.centerIn: parent
                        spacing: 8
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.engineerMode
                                  ? "source 4 当前没有完整事件记录"
                                  : "暂无历史事件"
                            color: Palette.secondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.engineerMode
                                  ? "不会为了填满时间线而生成示例事件"
                                  : "设备产生报警或状态变化后会显示在这里"
                            color: "#8B97A7"
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 11
                        }
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.events.length > 0
                    clip: true
                    model: root.events
                    spacing: 2
                    ScrollBar.vertical: ScrollBar { }
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: root.engineerMode ? 76 : 64
                        radius: 9
                        color: index % 2 === 0 ? "#F6F9FC" : "#FFFFFF"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 12
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: modelData.accent
                            }
                            Repeater {
                                model: root.rowColumns(modelData)
                                delegate: Text {
                                    Layout.preferredWidth: modelData[1] === 1 ? -1 : modelData[1]
                                    Layout.fillWidth: modelData[1] === 1
                                    text: modelData[0]
                                    color: Palette.secondary
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: root.engineerMode ? 9 : 10
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: root.engineerMode ? 3 : 2
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
