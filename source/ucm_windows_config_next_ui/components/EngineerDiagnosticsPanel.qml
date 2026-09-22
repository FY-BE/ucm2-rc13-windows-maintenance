import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import "AppPalette.js" as Palette

ScrollView {
    id: root
    property var backendObject
    readonly property bool online: Boolean(backendObject && backendObject.connected)
    readonly property var input: online && backendObject.telemetryReady && backendObject.productState
        ? (backendObject.productState.pairedInputStatus || {}) : ({})
    readonly property bool paired: Object.keys(input).length > 0
    readonly property var afeFields: backendObject && backendObject.afeMaintenanceFields
        ? backendObject.afeMaintenanceFields : []
    readonly property bool afeCanWrite: Boolean(backendObject
        && backendObject.afeMaintenanceCanWrite)
    readonly property bool afeDirty: Boolean(backendObject
        && backendObject.afeMaintenanceHasChanges)
    clip: true
    function field(key, valid) {
        return paired && valid && input[key] !== undefined && input[key] !== null ? String(input[key]) : "--"
    }
    function flag(key) { return field(key, true) === "--" ? "--" : Number(input[key]) === 1 ? "是" : "否" }
    FolderDialog {
        id: folder
        title: "保存工程诊断包"
        onAccepted: root.backendObject.exportDiagnosticPackage(selectedFolder)
    }
    FileDialog {
        id: csv
        title: "导出 ARM 正式值 CSV"
        fileMode: FileDialog.SaveFile
        nameFilters: ["CSV (*.csv)"]
        defaultSuffix: "csv"
        onAccepted: root.backendObject.exportCsv(selectedFile)
    }
    AppDialog {
        id: afeApplyConfirmation
        objectName: "afeMaintenanceConfirmation"
        width: 540
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "确认应用接收增益或采集起点"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (root.backendObject)
            root.backendObject.applyAfeMaintenance(true)
        contentItem: Label {
            width: 490
            wrapMode: Text.Wrap
            text: "只提交四路LNA、PGA、VCNTL和PL采集窗口起点候选。ARM必须保持HOST_MANAGED、租约有效、硬件安全停止且确认卸载；候选必须由ARM活动配置读回后再修改，不能用Windows默认值覆盖设备当前值。"
        }
    }
    ColumnLayout {
        width: root.availableWidth
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: "诊断与输入状态"; font.pixelSize: 25; font.bold: true; color: Palette.text }
                Label { text: "正式值、同帧输入和导出证据"; color: Palette.secondary }
            }
            Label { text: root.paired ? "● 已完成同帧核对" : "● 当前无可用输入帧"; color: root.paired ? "#27836A" : "#AC7939" }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: root.availableWidth > 1050 ? 4 : 2
            columnSpacing: 12; rowSpacing: 12
            Repeater {
                model: 4
                delegate: Frame {
                    required property int index
                    Layout.fillWidth: true
                    background: Rectangle { color: "white"; radius: 12; border.color: "#DFE7F2" }
                    padding: 16
                    ColumnLayout {
                        anchors.fill: parent
                        Label { text: "拉杆 " + (index + 1); color: ["#D9684B", "#3976D9", "#269C8F", "#8065B9"][index]; font.pixelSize: 14 }
                        Label {
                            text: root.online && root.backendObject.rodForceTexts ? root.backendObject.rodForceTexts[index] + " N" : "-- N"
                            font.pixelSize: 25; font.bold: true; color: Palette.text
                        }
                        Label {
                            text: "应变 " + (root.online && root.backendObject.rodStrainTexts
                                  ? root.backendObject.rodStrainTexts[index] : "--") + " με"
                            font.pixelSize: 14; color: Palette.secondary
                        }
                        Label {
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            text: root.online && root.backendObject.rodStateTexts ? root.backendObject.rodStateTexts[index] : "等待 ARM 正式结果"
                            color: "#7B889A"; font.pixelSize: 11
                        }
                    }
                }
            }
        }
        Frame {
            Layout.fillWidth: true
            padding: 18
            background: Rectangle { color: "white"; radius: 12; border.color: "#DFE7F2" }
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label { text: "原始回波窗口"; font.pixelSize: 18; font.bold: true; color: Palette.text }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: "“读取当前”读取ARM最近发布的2048点波形。向前/向后在ARM已保留的8192点帧内切片，不停止50 Hz、不重建模板，也不直接访问PL。"
                    color: Palette.secondary
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "waveformReadLatestButton"
                        text: root.backendObject && root.backendObject.waveformBusy
                              ? "读取中…" : "读取当前"
                        enabled: Boolean(root.backendObject && root.online
                            && root.backendObject.engineerMode
                            && !root.backendObject.waveformBusy)
                        onClicked: root.backendObject.captureLatestWaveform()
                    }
                    AppButton {
                        objectName: "waveformPreviousButton"
                        text: "向前2048点"
                        enabled: Boolean(root.backendObject
                            && root.backendObject.waveformViewportCanRead)
                        onClicked: root.backendObject.shiftWaveformViewport(-1)
                    }
                    AppButton {
                        objectName: "waveformNextButton"
                        text: "向后2048点"
                        enabled: Boolean(root.backendObject
                            && root.backendObject.waveformViewportCanRead)
                        onClicked: root.backendObject.shiftWaveformViewport(1)
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: root.backendObject
                              ? "帧内偏移 " + String(root.backendObject.waveformSliceOffset || 0)
                              : "帧内偏移 --"
                        color: Palette.secondary
                    }
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: root.backendObject
                          ? String(root.backendObject.waveformViewportStatus || "") : ""
                    color: root.backendObject && root.backendObject.waveformViewportAvailable
                           ? "#27836A" : "#9A763D"
                    font.pixelSize: 11
                }
                WaveformCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 430
                    series: root.backendObject ? root.backendObject.waveformSeries : []
                    windowStart: root.backendObject ? root.backendObject.waveformStart : 0
                    sampleRateHz: root.backendObject ? root.backendObject.waveformSampleRateHz : 0
                    statusText: root.backendObject ? root.backendObject.waveformStatus : "等待真实USB波形"
                }
            }
        }
        Frame {
            Layout.fillWidth: true
            padding: 18
            background: Rectangle { color: "white"; radius: 12; border.color: "#DFE7F2" }
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label { text: "接收增益与采集窗口维护"; font.pixelSize: 18; font.bold: true; color: Palette.text }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: "四路LNA、PGA、VCNTL和PL采集窗口起点复用正式CFG2事务。上方前后按钮只查看当前8192点帧；修改“PL采集窗口起点”会改变后续采集位置。应用仍要求HOST_MANAGED、安全停止和卸载，随后由ARM重新确认AGC与模板。"
                    color: Palette.secondary
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.availableWidth > 900 ? 2 : 1
                    columnSpacing: 12; rowSpacing: 9
                    Repeater {
                        model: root.afeFields
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true; implicitHeight: 92; radius: 8
                            color: "#F7FAFE"
                            border.color: modelData.dirty ? "#78A9DE" : "#DFE8F2"
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 12; spacing: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label { text: modelData.label; color: Palette.text; font.bold: true }
                                    Label { text: modelData.constraint; color: Palette.secondary; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                }
                                ComboBox {
                                    visible: modelData.allowedValues && modelData.allowedValues.length > 0
                                    model: modelData.allowedValues || []
                                    currentIndex: model.indexOf(Number(modelData.draft))
                                    enabled: modelData.canWrite === true
                                    onActivated: root.backendObject.setRuntimeConfigurationField(
                                        Number(modelData.fieldId), Number(modelData.elementIndex), Number(currentValue))
                                }
                                SpinBox {
                                    visible: !modelData.allowedValues || modelData.allowedValues.length === 0
                                    from: Number(modelData.minimum); to: Number(modelData.maximum)
                                    stepSize: Math.max(1, Number(modelData.step)); editable: true
                                    value: Number(modelData.draft)
                                    enabled: modelData.canWrite === true
                                    onValueModified: root.backendObject.setRuntimeConfigurationField(
                                        Number(modelData.fieldId), Number(modelData.elementIndex), value)
                                }
                                Label { text: modelData.dirty ? "待应用" : "一致"; color: modelData.dirty ? Palette.blue : Palette.tertiary; font.pixelSize: 10 }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "afeMaintenanceResetButton"
                        text: "放弃增益/窗口候选"
                        enabled: root.afeDirty && root.backendObject && !root.backendObject.runtimeConfigurationBusy
                        onClicked: root.backendObject.resetAfeMaintenanceDraft()
                    }
                    AppButton {
                        objectName: "afeMaintenanceApplyButton"
                        text: root.backendObject && root.backendObject.runtimeConfigurationBusy
                              ? "正在回读…" : "应用增益/窗口候选"
                        enabled: root.afeCanWrite && root.afeDirty
                            && root.backendObject && !root.backendObject.runtimeConfigurationBusy
                        onClicked: afeApplyConfirmation.open()
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: root.backendObject ? String(root.backendObject.runtimeConfigurationStatus || "") : ""
                        color: root.afeCanWrite ? Palette.blue : "#8A6A36"
                        font.pixelSize: 11
                    }
                }
            }
        }
        Frame {
            Layout.fillWidth: true
            padding: 18
            background: Rectangle { color: "white"; radius: 12; border.color: "#DFE7F2" }
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label { text: "模厚与 PLC 输入"; font.pixelSize: 18; font.bold: true; color: Palette.text }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "输入仅来自与正式结果匹配的 ARM 帧；USB 当前未提供正式实时 C 值。参考几何请查看设备配置。"; color: Palette.secondary }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 22; rowSpacing: 10
                    Repeater {
                        model: [
                            ["原始模厚（µm）", root.field("rawThicknessUm", Number(root.input.moldValid) === 1)],
                            ["生效模厚（µm）", root.field("effectiveThicknessUm", Number(root.input.moldValid) === 1)],
                            ["实际模厚源 · ARM 枚举", root.field("actualMoldSource", true)],
                            ["模厚有效 / 降级", root.flag("moldValid") + " / " + root.flag("moldDegraded")],
                            ["模厚原因码", root.field("moldReason", true)],
                            ["PLC 连接在线", root.flag("plcConnectionLive")],
                            ["PLC 原始状态码", root.field("plcRawStateCode", Number(root.input.plcValid) === 1)],
                            ["PLC 输入有效", root.flag("plcValid")],
                            ["PLC 开模许可输入", root.flag("plcOpenPermitted")],
                            ["外部联锁许可输入", root.flag("externalInterlockPermitted")]
                        ]
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 3
                            Label { text: modelData[0]; color: "#7D8AA0"; font.pixelSize: 11 }
                            Label { text: modelData[1]; color: "#243E60"; font.pixelSize: 15; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                        }
                    }
                }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "PLC 许可属于输入证据，不代表 Windows 已取得控制权，也不等同于设备过程状态。"; color: "#9A763D"; font.pixelSize: 11 }
            }
        }
        Frame {
            Layout.fillWidth: true; padding: 18
            background: Rectangle { color: "#F0F5FC"; radius: 12; border.color: "#DFE7F2" }
            ColumnLayout {
                anchors.fill: parent
                Label { text: "正式诊断边界"; font.pixelSize: 16; color: Palette.text }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "四杆应变只显示ARM消息31的同帧正式值；未发布或身份不匹配时显示 --。温度、NCC峰值和波形仍按设备实际开放能力显示，Windows不生成替代值。"; color: Palette.secondary }
            }
        }
        Frame {
            Layout.fillWidth: true; padding: 18
            background: Rectangle { color: "white"; radius: 12; border.color: "#DFE7F2" }
            ColumnLayout {
                anchors.fill: parent
                Label { text: "记录与导出"; font.pixelSize: 18; font.bold: true; color: Palette.text }
                RowLayout {
                    AppButton { text: "导出 CSV"; enabled: Boolean(root.backendObject); onClicked: csv.open() }
                    AppButton { objectName: "engineerDiagnosticExport"; text: "导出完整诊断包"; enabled: Boolean(root.backendObject && root.backendObject.engineerMode && root.backendObject.canExportEvidence); onClicked: folder.open() }
                }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.backendObject ? String(root.backendObject.evidenceStatus || "尚未导出") : "尚未导出"; color: Palette.secondary }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "诊断包包含原始日志、配置、能力、错误与 SHA 清单。读取不完整时明确标记，不表示设备端摘要已验证。"; color: Palette.secondary; font.pixelSize: 11 }
                CheckBox { id: identityToggle; text: "展开同帧身份与原始输入（高级）" }
                TextArea {
                    visible: identityToggle.checked
                    Layout.fillWidth: true; Layout.preferredHeight: 220
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                    text: root.paired ? JSON.stringify(root.input, null, 2) : "尚无匹配输入帧"
                    font.family: "Cascadia Mono"; font.pixelSize: 11
                }
            }
        }
    }
}
