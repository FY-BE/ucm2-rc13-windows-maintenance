import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "AppPalette.js" as Palette

Item {
    id: root
    required property QtObject backendObject
    signal backRequested()
    readonly property QtObject ethercat: backendObject.ethercatProbe
    readonly property QtObject ethercatMaster: backendObject.ethercatMaster

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        Column {
            Layout.fillWidth: true
            spacing: 3
            Text {
                text: "连接管理"
                color: Palette.text
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 27
                font.weight: Font.DemiBold
            }
            Text {
                text: "相机与模拟主站是两条独立、可选的网络链路，任一路缺失不阻塞另一路。"
                color: Palette.secondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 11
            }
        }

        AppButton { text: "返回维护与证据"; onClicked: root.backRequested() }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 250
            spacing: 14

            GlassPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                elevated: false
                glassColor: Palette.surface
                strokeColor: Palette.border
                cornerRadius: 18
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5
                            color: root.backendObject.referenceForceReady ? Palette.green : Palette.orange }
                        Text { text: "标准力相机"; color: Palette.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        Text { text: root.backendObject.referenceForceReady ? "在线" : "未连接";
                            color: root.backendObject.referenceForceReady ? Palette.green : Palette.secondary }
                    }
                    Text { text: "用途：拍照、六个 ROI 核对、S1–S4 识别与标定采样";
                        color: Palette.secondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Palette.border }
                    Text { text: root.backendObject.referenceForceStatus; color: Palette.secondary;
                        wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Item { Layout.fillHeight: true }
                    AppButton {
                        text: root.backendObject.referenceForceRunning ? "断开相机" : "连接相机"
                        enabled: !root.backendObject.offlinePreview
                        onClicked: root.backendObject.toggleReferenceForce()
                    }
                }
            }

            GlassPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                elevated: false
                glassColor: Palette.surface
                strokeColor: Palette.border
                cornerRadius: 18
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5
                            color: root.ethercat.detected ? Palette.green : Palette.orange }
                        Text { text: "模拟主站设备"; color: Palette.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        Text { text: root.ethercat.detected ? root.ethercat.alStateText : "未检测";
                            color: root.ethercat.detected ? Palette.green : Palette.secondary }
                    }
                    Text { text: "用途：通过设备网口发现 LAN9252；此处只读 AL Status，主站操作在“模拟主站”页进行";
                        color: Palette.secondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Palette.border }
                    Text { text: root.ethercat.statusText; color: Palette.secondary;
                        wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Text {
                        visible: root.ethercat.detected
                        text: root.ethercat.portName + " · " + root.ethercat.portDescription
                              + "\nMAC " + root.ethercat.macAddress + " · 从站 " + root.ethercat.slaveCount
                        color: Palette.text; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    Item { Layout.fillHeight: true }
                    AppButton {
                        text: root.ethercat.busy ? "正在检测…" : "检测设备网口"
                        enabled: !root.ethercat.busy && !root.ethercatMaster.busy
                                 && !root.backendObject.offlinePreview
                        onClicked: root.ethercat.scan()
                    }
                }
            }
        }

        GlassPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            elevated: false
            glassColor: Palette.surface
            strokeColor: Palette.border
            cornerRadius: 18
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                Text { text: "工程通信通道"; color: Palette.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    text: "USB：" + root.backendObject.usbLabel
                          + "。现有 WinUSB 继续承载工程配置、诊断和标定所需 nccDeltaNs；EtherCAT 模拟主站与它分别显示。"
                    color: Palette.secondary; wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: root.ethercatMaster.busy
                          ? "模拟主站正在使用该网口，只读重新检测已暂停。"
                          : "设备网口检测成功只证明链路上有 EtherCAT 从站回应。请在“模拟主站”页核对身份并执行 PREOP/SAFEOP/OP 与 PDO 收发。"
                    color: Palette.secondary; wrapMode: Text.Wrap
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
