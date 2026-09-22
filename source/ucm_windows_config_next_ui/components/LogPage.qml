import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "AppPalette.js" as Palette

Item {
    id: root
    property var backendObject
    FolderDialog {
        id: exportFolder
        title: "保存完整日志诊断包"
        onAccepted: root.backendObject.exportDiagnosticPackage(selectedFolder)
    }
    property var sources: []
    property string statusText: ""
    property string contentText: ""
    property string selectedTitle: "尚未选择日志源"
    property int selectedSourceId: 0
    signal refreshRequested()
    signal sourceRequested(int sourceId)
    signal openTimelineRequested()

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary; text: root.backendObject ? String(root.backendObject.evidenceStatus || "选择日志可预览首块；完整文件请导出诊断包") : "" }
            AppButton { text: "导出完整诊断包"; enabled: Boolean(root.backendObject && root.backendObject.engineerMode && root.backendObject.canExportEvidence); onClicked: exportFolder.open() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            Column {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: "运行日志"
                    color: Palette.text
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "设备日志目录 · 可变文件标识 · 二进制首块预览"
                    color: Palette.secondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 12
                }
            }
            AppButton {
                text: "查看事件时间线"
                Layout.preferredWidth: 132
                Layout.preferredHeight: 38
                onClicked: root.openTimelineRequested()
            }
            AppButton {
                text: "刷新日志清单"
                Layout.preferredWidth: 132
                Layout.preferredHeight: 38
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
                    radius: 12
                    color: parent.down ? "#083F83"
                                       : parent.hovered ? "#1762B1" : Palette.blue
                }
            }
        }

        ListView {
            id: logCatalog
            Layout.fillWidth: true
            Layout.preferredHeight: 124
            Layout.minimumHeight: 124
            Layout.maximumHeight: 124
            orientation: ListView.Horizontal
            clip: true
            spacing: 10
            model: root.sources
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: GlassPanel {
                    width: 240
                    height: 112
                    elevated: false
                    glassColor: root.selectedSourceId === modelData.sourceId
                        ? Palette.surface : "#CFFFFFFF"
                    border.color: root.selectedSourceId === modelData.sourceId
                        ? "#500B4E9E" : Palette.surface
                    Column {
                        anchors.fill: parent
                        anchors.margins: 13
                        spacing: 4
                        Row {
                            width: parent.width
                            spacing: 7
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: modelData.available ? Palette.green : "#B9B0A9"
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                width: parent.width - 15
                                text: "source " + modelData.sourceId + " · " + modelData.name
                                color: Palette.text
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                        }
                        Text {
                            text: modelData.sizeText + " · " + modelData.state
                            color: "#736C66"
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 10
                        }
                        Text {
                            text: "identity " + modelData.identityCrc
                            color: "#9A938D"
                            font.family: "Cascadia Mono"
                            font.pixelSize: 8
                        }
                        AppButton {
                            width: 88
                            height: 26
                            text: "读取首块"
                            enabled: modelData.available
                            onClicked: root.sourceRequested(modelData.sourceId)
                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled ? Palette.blue : "#AAA39D"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 9
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 9
                                color: parent.enabled ? "#140B4E9E" : "#0A000000"
                                border.color: parent.enabled ? "#300B4E9E" : "#10000000"
                            }
                        }
                    }
                }
        }

        GlassPanel {
            elevated: false
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: root.selectedTitle
                        color: Palette.text
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "只读 · 首块最多4 KiB · 完整导出需hash验证"
                        color: "#8E8781"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 9
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.statusText
                    color: "#827A74"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 9
                    elide: Text.ElideMiddle
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: "#28766F69"
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    TextArea {
                        text: root.contentText.length > 0
                            ? root.contentText
                            : "选择可用日志源后显示真实 ARM 日志。\n无数据时不会生成示例内容。"
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.NoWrap
                        color: root.contentText.length > 0 ? "#373330" : "#A29A94"
                        font.family: "Cascadia Mono"
                        font.pixelSize: 10
                        background: Rectangle {
                            radius: 12
                            color: "#50FFFFFF"
                            border.color: "#24FFFFFF"
                        }
                    }
                }
            }
        }
    }
}
