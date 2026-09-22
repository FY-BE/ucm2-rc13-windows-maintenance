import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls
import QtQuick.Layouts
import "AppPalette.js" as Palette

Item {
    id: root
    property var backendObject

    FileDialog {
        id: csvDestination
        title: "导出当前窗口的ARM杆力"
        fileMode: FileDialog.SaveFile
        nameFilters: ["CSV (*.csv)"]
        defaultSuffix: "csv"
        onAccepted: if (root.backendObject) root.backendObject.exportCsv(selectedFile)
    }
    FolderDialog {
        id: diagnosticDestination
        title: "选择诊断包保存目录"
        onAccepted: if (root.backendObject) root.backendObject.exportDiagnosticPackage(selectedFolder)
    }

    function usbSummaryValue(index, key, fallback) {
        if (!backendObject || !backendObject.usbV2SummaryItems
                || backendObject.usbV2SummaryItems.length <= index)
            return fallback
        const item = backendObject.usbV2SummaryItems[index]
        return item[key] ? String(item[key]) : fallback
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        ColumnLayout {
            Layout.preferredWidth: Math.max(390, root.width * 0.36)
            Layout.maximumWidth: Math.max(390, root.width * 0.36)
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 198
                color: "#FFFFFF"
                border.color: Palette.border
                radius: 20

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 7
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle {
                            width: 11
                            height: 11
                            radius: 6
                            color: root.backendObject && root.backendObject.connected
                                   ? Palette.green : Palette.orange
                        }
                        Text {
                            text: root.backendObject && root.backendObject.connected
                                  ? "设备已连接" : "设备未连接"
                            color: Palette.text
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        CustomerActionButton {
                            compact: true
                            text: root.backendObject && root.backendObject.connected
                                  ? "重新连接" : "连接设备"
                            onClicked: if (root.backendObject) root.backendObject.reconnect()
                        }
                    }
                    CustomerActionButton {
                        compact: true
                        text: "断开设备"
                        ToolTip.visible: hovered
                        ToolTip.text: "断开设备并暂停自动重连"
                        enabled: root.backendObject && root.backendObject.connected
                        onClicked: root.backendObject.disconnectDevice()
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.backendObject ? root.backendObject.usbLabel : "USB 待连接"
                        color: "#66788E"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 12
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.backendObject ? root.backendObject.statusText : "等待设备状态"
                        color: Palette.tertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 190
                color: "#FFFFFF"
                border.color: Palette.border
                radius: 20

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 8
                    Text {
                        text: "运行状态"
                        color: Palette.text
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: root.backendObject ? root.backendObject.runtimeStateText : "等待状态"
                        color: Palette.blue
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 15
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.backendObject ? root.backendObject.runtimeDetailText : ""
                        color: "#7F8C9D"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Palette.border }
                    Text {
                        text: "过程状态：" + (root.backendObject
                              ? root.backendObject.processStateText : "待机")
                        color: "#566A81"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 12
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#FFFFFF"
                border.color: Palette.border
                radius: 20

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 7
                    Text {
                        text: "系统信息"
                        color: Palette.text
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "设备身份：" + root.usbSummaryValue(
                                  0, "note", "等待设备识别")
                        color: "#5D7188"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 12
                    }
                    Text {
                        text: "协议版本：" + root.usbSummaryValue(
                                  0, "value", "--")
                        color: Palette.tertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 11
                    }
                    Text {
                        text: "启真传感 · Windows 客户监测终端"
                        color: Palette.tertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 11
                    }
                    CustomerActionButton {
                        compact: true
                        text: "导出 CSV"
                        ToolTip.visible: hovered
                        ToolTip.text: "导出ARM结果与帧身份，数值单位N"
                        onClicked: csvDestination.open()
                    }
                    CustomerActionButton {
                        compact: true
                        text: "导出诊断包"
                        enabled: root.backendObject && root.backendObject.canExportEvidence
                        onClicked: diagnosticDestination.open()
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.backendObject ? root.backendObject.evidenceStatus : ""
                        font.pixelSize: 10
                        color: Palette.tertiary
                        wrapMode: Text.Wrap
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#FFFFFF"
            border.color: Palette.border
            radius: 20

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "历史事件"
                        color: Palette.text
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 21
                        font.weight: Font.DemiBold
                    }
                    Item { Layout.fillWidth: true }
                    CustomerActionButton {
                        compact: true
                        text: "刷新"
                        onClicked: if (root.backendObject) root.backendObject.refreshTimeline()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.backendObject ? root.backendObject.timelineStatus : "等待读取历史事件"
                    color: "#8995A4"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Palette.border }

                ListView {
                    id: eventList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: root.backendObject ? root.backendObject.timelineEvents : []
                    delegate: Rectangle {
                        width: eventList.width
                        height: 62
                        radius: 8
                        color: index % 2 === 0 ? "#F7FAFE" : "#FFFFFF"
                        border.color: "#E6EDF6"
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12
                            Rectangle {
                                width: 7
                                height: 34
                                radius: 4
                                color: "#4A78BA"
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData[0] || "设备事件"
                                    color: "#34475E"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData[1] || ""
                                    color: "#8995A4"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    Text {
                        visible: eventList.count === 0
                        anchors.centerIn: parent
                        text: root.backendObject ? root.backendObject.eventText : "暂无历史事件"
                        color: "#99A3AF"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 13
                    }
                }
            }
        }
    }
}
