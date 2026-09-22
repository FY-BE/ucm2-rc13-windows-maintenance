import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "AppPalette.js" as Palette

Item {
    id: root
    required property QtObject backendObject
    readonly property QtObject probe: backendObject.ethercatProbe
    readonly property QtObject master: backendObject.ethercatMaster

    function number(text, fallback) {
        const value = Number(text)
        return Number.isFinite(value) && value >= 0 ? Math.floor(value) : fallback
    }

    function settings() {
        return {
            "periodMs": number(period.text, 1),
            "machineModel": machineModel.currentValue,
            "currentUtcMs": currentUtc.text,
            "clampingForceSetpointKn": forceSet.text,
            "moldThickness": moldThickness.text,
            "machineState": machineState.currentIndex,
            "deviceEnabledUtcMs": enabledUtc.text,
            "accumulatedRuntimeMs": runtime.text
        }
    }

    function forceText(value) {
        return Number(value) < 0 || Number(value) === 4294967295 ? "--"
             : Number(value).toLocaleString(Qt.locale("zh_CN"), 'f', 0)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Column {
                spacing: 3
                Text { text: "EtherCAT 模拟主站"; color: Palette.text; font.family: "Microsoft YaHei UI"; font.pixelSize: 27; font.weight: Font.DemiBold }
                Text { text: "FQX V1.0 小端 · DC-SYNC0 1 ms · 测量结果约 20 ms 更新 · 33/35 字节 PDO"; color: Palette.secondary; font.family: "Microsoft YaHei UI"; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: stateRow.implicitWidth + 28; implicitHeight: 34; radius: 17
                color: master.running ? "#E8F8F0" : Palette.surface
                border.color: master.running ? "#A6E3C5" : Palette.border
                Row { id: stateRow; anchors.centerIn: parent; spacing: 7
                    Rectangle { width: 8; height: 8; radius: 4; color: master.running ? Palette.green : Palette.orange; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: master.alState + (master.workingCounter > 0 ? " · WKC " + master.workingCounter : ""); color: master.running ? Palette.green : Palette.secondary; font.family: "Microsoft YaHei UI"; font.pixelSize: 11 }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            GlassPanel {
                Layout.preferredWidth: 430
                Layout.fillHeight: true
                elevated: false; glassColor: Palette.surface; strokeColor: Palette.border; cornerRadius: 18
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 20; spacing: 10
                    Text { text: "主站输出参数 · RxPDO"; color: Palette.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text { text: "FQX V1.0 固定 33 字节 · 仅在点击开始后周期发送"; color: Palette.secondary; font.pixelSize: 10 }
                    GridLayout {
                        Layout.fillWidth: true; columns: 2; columnSpacing: 10; rowSpacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 4; Label { text: "EtherCAT 周期 (ms)"; color: Palette.secondary } AppTextField { id: period; Layout.fillWidth: true; text: "1"; enabled: !master.busy; validator: IntValidator { bottom: 1; top: 1000 } } }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4; Label { text: "机器型号"; color: Palette.secondary } AppComboBox { id: machineModel; Layout.fillWidth: true; model: master.machineModels; textRole: "label"; valueRole: "value"; currentIndex: 0; enabled: !master.busy } }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4; Label { text: "当前 UTC ms（0=自动）"; color: Palette.secondary } AppTextField { id: currentUtc; Layout.fillWidth: true; text: "0"; enabled: !master.busy } }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4; Label { text: "锁模力设定值 (kN)"; color: Palette.secondary } AppTextField { id: forceSet; Layout.fillWidth: true; text: "0"; enabled: !master.busy; validator: IntValidator { bottom: 0; top: 180000 } } }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4; Label { text: "模厚 (mm)"; color: Palette.secondary } AppTextField { id: moldThickness; Layout.fillWidth: true; text: "662"; enabled: !master.busy; validator: IntValidator { bottom: 1; top: 6553 } } }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4; Label { text: "机器状态 (0–4)"; color: Palette.secondary } AppComboBox { id: machineState; Layout.fillWidth: true; model: ["0", "1", "2", "3", "4"]; currentIndex: 4; enabled: !master.busy } }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4; Label { text: "设备启用 UTC ms"; color: Palette.secondary } AppTextField { id: enabledUtc; Layout.fillWidth: true; text: "0"; enabled: !master.busy } }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4; Label { text: "累计运行时间 (ms)"; color: Palette.secondary } AppTextField { id: runtime; Layout.fillWidth: true; text: "0"; enabled: !master.busy } }
                    }
                    Text { Layout.fillWidth: true; text: probe.detected ? probe.portName + " · " + probe.macAddress : "请先检测设备网口"; color: probe.detected ? Palette.text : Palette.orange; wrapMode: Text.Wrap }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        AppButton { text: probe.busy ? "检测中…" : "检测网口"; enabled: !master.busy && !probe.busy; onClicked: probe.scan() }
                        AppButton { Layout.fillWidth: true; text: master.busy ? "启动中…" : "开始周期通信"; enabled: probe.detected && !master.busy; onClicked: master.start(probe.adapterId, probe.macAddress, root.settings()) }
                        AppButton { text: "停止"; enabled: master.busy; onClicked: master.stop() }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14
                GridLayout {
                    Layout.fillWidth: true; columns: 3; columnSpacing: 12; rowSpacing: 12
                    Repeater {
                        model: [
                            {label:"拉杆 1", value:root.forceText(master.rod1), unit:"kN"}, {label:"拉杆 2", value:root.forceText(master.rod2), unit:"kN"},
                            {label:"拉杆 3", value:root.forceText(master.rod3), unit:"kN"}, {label:"拉杆 4", value:root.forceText(master.rod4), unit:"kN"},
                            {label:"设备 Total", value:root.forceText(master.total), unit:"kN"}, {label:"偏载率", value:Number(master.imbalancePercent).toLocaleString(Qt.locale("zh_CN"), 'f', 2), unit:"%"}
                        ]
                        GlassPanel {
                            required property var modelData
                            Layout.fillWidth: true; Layout.preferredHeight: 112
                            elevated: false; glassColor: Palette.surface; strokeColor: Palette.border; cornerRadius: 16
                            Column { anchors.fill: parent; anchors.margins: 16; spacing: 7
                                Text { text: parent.parent.modelData.label; color: Palette.secondary; font.pixelSize: 11 }
                                Row { spacing: 6
                                    Text { text: parent.parent.parent.modelData.value; color: Palette.text; font.pixelSize: 25; font.weight: Font.DemiBold }
                                    Text { anchors.baseline: parent.children[0].baseline; text: parent.parent.parent.modelData.unit; color: Palette.secondary; font.pixelSize: 11 }
                                }
                            }
                        }
                    }
                }
                GlassPanel {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    elevated: false; glassColor: Palette.surface; strokeColor: Palette.border; cornerRadius: 18
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 20; spacing: 10
                        Text { text: "通信诊断"; color: Palette.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                        Text { Layout.fillWidth: true; text: master.statusText; color: master.alStatusCode === 0 ? Palette.secondary : Palette.red; wrapMode: Text.Wrap }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Palette.border }
                        GridLayout {
                            columns: 4; Layout.fillWidth: true; columnSpacing: 18
                            Column { Text { text: "协议"; color: Palette.tertiary; font.pixelSize: 10 } Text { text: master.profile || "待识别"; color: Palette.text } }
                            Column { Text { text: "有效周期"; color: Palette.tertiary; font.pixelSize: 10 } Text { text: master.completedCycles; color: Palette.text } }
                            Column { Text { text: "丢失周期"; color: Palette.tertiary; font.pixelSize: 10 } Text { text: master.droppedCycles; color: Palette.text } }
                            Column { Text { text: "AL 错误码"; color: Palette.tertiary; font.pixelSize: 10 } Text { text: "0x" + master.alStatusCode.toString(16).padStart(4, "0").toUpperCase(); color: master.alStatusCode ? Palette.red : Palette.text } }
                        }
                        Text { Layout.fillWidth: true; text: "数据 UTC ms：" + master.dataTimestampUtcMs + " · 故障标志 " + master.faultFlag + " · Windows 四杆和 " + root.forceText(master.windowsTotal) + " kN"; color: master.faultFlag ? Palette.red : Palette.secondary; wrapMode: Text.Wrap }
                        Text { Layout.fillWidth: true; text: "设备错误 0x" + master.deviceErrorCode.toString(16).padStart(4, "0").toUpperCase() + " · 配置错误 0x" + master.configErrorCode.toString(16).padStart(4, "0").toUpperCase(); color: (master.deviceErrorCode || master.configErrorCode) ? Palette.red : Palette.secondary }
                        Text { Layout.fillWidth: true; visible: master.evidencePath.length > 0; text: "证据：" + master.evidencePath; color: Palette.secondary; font.pixelSize: 10; elide: Text.ElideMiddle }
                        Item { Layout.fillHeight: true }
                        Text { Layout.fillWidth: true; text: "设备 Total 为 FQX TxPDO 独立字段；Windows 四杆和仅用于一致性复核。0xFFFFFFFF 表示无效力值。停止或异常时主站请求设备返回 INIT。"; color: Palette.tertiary; font.pixelSize: 10; wrapMode: Text.Wrap }
                    }
                }
            }
        }
    }
}
