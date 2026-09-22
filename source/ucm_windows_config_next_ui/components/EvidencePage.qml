import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "AppPalette.js" as Palette

Item {
    id: root
    required property QtObject backendObject
    property var items: []
    property string statusText: ""
    property string packagePath: ""
    property bool canExport: false

    signal refreshRequested()
    signal exportRequested(url destination)
    signal openConnectionRequested()

    Timer {
        interval: 250
        running: true
        repeat: false
        onTriggered: root.refreshRequested()
    }

    FolderDialog {
        id: destinationDialog
        title: "选择完整诊断包保存位置"
        onAccepted: root.exportRequested(selectedFolder)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            Column {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: "证据与交付"
                    color: Palette.text
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "冻结原始对象、外设日志、当前画面、manifest 与 SHA"
                    color: Palette.secondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 12
                }
            }
            Rectangle {
                width: 164; height: 34; radius: 17
                color: root.canExport ? "#D82AA198" : Palette.surface
                border.color: Palette.surface
                Row {
                    anchors.centerIn: parent
                    spacing: 7
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: root.canExport ? "white" : Palette.orange
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: root.canExport ? "可发起冻结" : "等待真实USB"
                        color: root.canExport ? "white" : "#6C655F"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        GlassPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 128
            elevated: false
            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    Text { text: "ARM运行：" + ((root.backendObject.runtimeProgress || ({})).stageText || "等待进度")
                        color: Palette.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true; color: Palette.secondary; wrapMode: Text.Wrap
                        text: (root.backendObject.runtimeProgress || ({})).available
                            ? "总体 " + (Number(root.backendObject.runtimeProgress.overallPermille) / 10).toFixed(1)
                              + "% · 质量 " + root.backendObject.runtimeProgress.qualityText
                              + " · PLC " + (root.backendObject.runtimeProgress.plcFresh ? "新鲜" : "失联/未知")
                              + " · 模板掩码 0x" + Number(root.backendObject.runtimeProgress.templateValidMask).toString(16).toUpperCase()
                            : String((root.backendObject.runtimeProgress || ({})).message || "等待ARM消息48")
                    }
                    Text {
                        Layout.fillWidth: true; color: Palette.secondary; wrapMode: Text.Wrap
                        visible: (root.backendObject.runtimeProgress || ({})).available
                        text: "项 " + root.backendObject.runtimeProgress.stageItem + "/"
                            + root.backendObject.runtimeProgress.stageItemCount
                            + " · 已用 " + root.backendObject.runtimeProgress.stageElapsedMs + " ms"
                            + " · 上限 " + root.backendObject.runtimeProgress.stageLimitMs + " ms"
                            + " · 预计剩余 " + root.backendObject.runtimeProgress.estimatedRemainingMs + " ms"
                            + " · 等待原因 " + root.backendObject.runtimeProgress.waitReason
                            + " · 当前 " + JSON.stringify(root.backendObject.runtimeProgress.current)
                            + " · 最佳 " + JSON.stringify(root.backendObject.runtimeProgress.best)
                            + " · 零载参考 " + root.backendObject.runtimeProgress.tareState + "/"
                            + root.backendObject.runtimeProgress.tareGeneration
                            + " · fault " + root.backendObject.runtimeProgress.faultCode
                    }
                    Text { color: Palette.secondary; text: String((root.backendObject.runtimeActionResult || ({})).message || "") }
                }
                AppButton { text: "手动重建零载参考"; enabled: root.backendObject.productCanWrite
                        && (root.backendObject.runtimeProgress || ({})).available
                    onClicked: root.backendObject.requestRuntimeAction(1) }
                AppButton { text: "重启AGC"; enabled: root.backendObject.productCanWrite
                        && (root.backendObject.runtimeProgress || ({})).available
                    onClicked: root.backendObject.requestRuntimeAction(3) }
            }
        }

        GlassPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 112
            elevated: true
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 14
                Rectangle {
                    width: 9; height: 58; radius: 5
                    color: root.canExport ? Palette.green : Palette.orange
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text {
                        Layout.fillWidth: true
                        text: root.statusText
                        color: "#3D3935"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.packagePath.length > 0
                              ? root.packagePath
                              : "导出目录由用户选择；软件只创建带时间戳的新目录，不覆盖已有包。"
                        color: "#817A74"
                        font.family: root.packagePath.length > 0
                                     ? "Cascadia Mono" : "Microsoft YaHei UI"
                        font.pixelSize: 9
                        elide: Text.ElideMiddle
                    }
                }
                AppButton {
                    text: "连接与升级"
                    onClicked: root.openConnectionRequested()
                }
                AppButton {
                    text: "重新核对"
                    onClicked: root.refreshRequested()
                }
                AppButton {
                    text: "生成完整诊断包"
                    highlighted: true
                    enabled: root.canExport
                    onClicked: destinationDialog.open()
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 286
            columns: 3
            columnSpacing: 12
            rowSpacing: 12
            Repeater {
                model: root.items
                delegate: GlassPanel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 132
                    Layout.minimumHeight: 132
                    Layout.maximumHeight: 132
                    elevated: false
                    glassColor: "#C8FFFFFF"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: modelData.accent || "#4D7CFE"
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.title || "--"
                                color: "#4A4541"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: modelData.ready ? "READY" : "CHECK"
                                color: modelData.ready ? "#278C83" : "#B9782C"
                                font.family: "Cascadia Mono"
                                font.pixelSize: 8
                                font.weight: Font.Bold
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.state || "--"
                            color: "#292623"
                            font.family: "Segoe UI Variable Display Display"
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: modelData.detail || "--"
                            color: "#8B837D"
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            GlassPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                elevated: false
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8
                    Text {
                        text: "每个完成包包含"
                        color: "#3D3935"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 18
                        rowSpacing: 7
                        Repeater {
                            model: [
                                "config-receipt-512.bin",
                                "telemetry-512.bin",
                                "waveform-16448.bin",
                                "log-1..4-tail.txt",
                                "screen.png",
                                "config-evidence.json",
                                "manifest.json",
                                "SHA256SUMS.txt"
                            ]
                            delegate: Row {
                                Layout.fillWidth: true
                                spacing: 7
                                Text {
                                    text: "✓"
                                    color: Palette.green
                                    font.pixelSize: 10
                                }
                                Text {
                                    text: modelData
                                    color: "#716A64"
                                    font.family: "Cascadia Mono"
                                    font.pixelSize: 9
                                }
                            }
                        }
                    }
                }
            }
            GlassPanel {
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                elevated: false
                glassColor: "#D83B3937"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 17
                    spacing: 7
                    Text {
                        text: "完成门"
                        color: "white"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "必须同时取得严格字节数的配置回执、遥测与2048×4波形；日志按同一snapshot读取，界面截图和全部文件SHA写入manifest。"
                        color: "#D8D0CBC7"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        lineHeight: 1.28
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "只要中途任何一步失败，目录保留 PACKAGE_INCOMPLETE 标记，不能冒充完整证据。"
                        color: "#F0B6A9"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
