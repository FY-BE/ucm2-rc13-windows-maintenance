import QtQuick
import QtQuick.Layouts
import "AppPalette.js" as Palette

Item {
    id: root
    property var backendObject
    property var rods: []
    property var summary: ({})
    property bool telemetryReady: false

    EngineerDiagnosticsPanel {
        anchors.fill: parent
        backendObject: root.backendObject
        visible: Boolean(root.backendObject && (root.backendObject.productReadOnly || root.backendObject.offlinePreview))
    }

    ColumnLayout {
        visible: !root.backendObject || (!root.backendObject.productReadOnly && !root.backendObject.offlinePreview)
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            Column {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: "四杆诊断"
                    color: Palette.text
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "T0、NCC、SNR、delay、饱和、原因与前端回执"
                    color: Palette.secondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 12
                }
            }
            Rectangle {
                width: 126
                height: 34
                radius: 17
                color: Palette.surface
                border.color: Palette.surface
                Row {
                    anchors.centerIn: parent
                    spacing: 7
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: root.telemetryReady ? Palette.green : Palette.orange
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: root.telemetryReady ? "真实遥测在线" : "等待快照"
                        color: Palette.secondary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 11
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 112
            spacing: 12
            Repeater {
                model: [
                    ["过程与有效门", root.summary.process || "--",
                     (root.summary.trust || "--") + "\n" + (root.summary.gate || "--"), Palette.blue],
                    ["四类有效掩码", "0xF 才表示四杆齐全",
                     root.summary.masks || "--", Palette.green],
                    ["采集与前端身份", "回执必须跟随当前会话",
                     (root.summary.frontend || "--") + "\n" + (root.summary.identity || "--"), "#8B6FCB"]
                ]
                delegate: GlassPanel {
                    performanceSensitive: true
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    elevated: false
                    Column {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 5
                        Row {
                            spacing: 8
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: modelData[3]
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: modelData[0]
                                color: "#655F5A"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            text: modelData[1]
                            color: Palette.text
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            text: modelData[2]
                            color: "#857E78"
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 12

            Repeater {
                model: root.rods
                delegate: GlassPanel {
                    performanceSensitive: true
                    id: rodCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 214
                    elevated: true
                    property color rodAccent: modelData.accent ||
                        ["#F06449", "#4D7CFE", Palette.green, "#8B6FCB"][index]

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: rodCard.rodAccent
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.title || ("拉杆 " + (index + 1))
                                color: Palette.text
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: modelData.state || "--"
                                color: "#827A74"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 10
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: modelData.force || "--"
                                color: Palette.text
                                font.family: "Segoe UI Variable Display Display"
                                font.pixelSize: 32
                                font.weight: Font.Medium
                            }
                            Text {
                                text: "N"
                                color: "#918A84"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 13
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 5
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "reason  " + (modelData.reason || "--")
                                color: "#8B847E"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 9
                                elide: Text.ElideRight
                                Layout.maximumWidth: 250
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: "#24766F69"
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            columnSpacing: 18
                            rowSpacing: 8
                            Repeater {
                                model: [
                                    ["T0 参考 / 当前", modelData.t0 || "--"],
                                    ["NCC 峰值 / 峰比", modelData.ncc || "--"],
                                    ["lag / delay", modelData.lagDelay || "--"],
                                    ["SNR / 饱和数", modelData.snrSaturation || "--"],
                                    ["measurement flags", modelData.flags || "--"]
                                ]
                                delegate: Column {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: modelData[0]
                                        color: "#9A938D"
                                        font.family: "Microsoft YaHei UI"
                                        font.pixelSize: 9
                                    }
                                    Text {
                                        text: modelData[1]
                                        color: "#514C48"
                                        font.family: "Microsoft YaHei UI"
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
