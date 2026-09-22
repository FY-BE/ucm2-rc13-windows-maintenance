import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "CustomerSettings.js" as CustomerSettings
import "AppPalette.js" as Palette

Item {
    id: root
    property var backendObject
    property string statusText: "基础机械值为未标定候选草稿；提交权限取决于连接、设备能力与当前状态。参考坐标由工程师设置。"
    readonly property bool canWrite: backendObject && !backendObject.offlinePreview && backendObject.connected && backendObject.productCanWrite
    property bool statusError: false
    signal activeDiameterConfirmed(real diameterMm)

    function stateDocument(section, which) {
        if (!backendObject || !backendObject.connected || !backendObject.productState)
            return "--（离线）"
        const state = backendObject.productState[section]
        if (!state || !state.available || !state[which]) return "--（未成功读取）"
        return JSON.stringify(state[which], null, 2)
    }

    function basicState(which) {
        if (!backendObject || !backendObject.connected || !backendObject.productState) return null
        const state = backendObject.productState.deviceModel
        if (!state || !state.available || !state[which] || Object.keys(state[which]).length === 0) return null
        return CustomerSettings.basicDocument(state[which])
    }
    readonly property var activeModelDocument: backendObject && backendObject.connected && backendObject.productState && backendObject.productState.deviceModel && backendObject.productState.deviceModel.available ? backendObject.productState.deviceModel.active_document : null
    readonly property bool modelContractKnown: activeModelDocument && CustomerSettings.bodyReferenceContractError(activeModelDocument)===""
    readonly property var referenceCheck: modelContractKnown ? CustomerSettings.bodyReferenceProjection(Object.assign({},activeModelDocument,customerDraft())) : ({valid:false,error:"尚未读取schema 2参考坐标，无法核对A_ref"})
    readonly property var currentBasic: basicState("active_document")
    readonly property var startupBasic: basicState("startup_document")
    readonly property var draftDifferences: currentBasic ? CustomerSettings.fieldDifferences(currentBasic, customerDraft(), CustomerSettings.basicKeys) : []
    readonly property var startupDifferences: currentBasic && startupBasic ? CustomerSettings.fieldDifferences(startupBasic, currentBasic, CustomerSettings.basicKeys) : []

    property int pendingOperation: 1
    function customerDraft() {
        return CustomerSettings.customerCandidate(deviceModel.text, totalLength.text, bLength.text, dLength.text, eLength.text)
    }
    function validateLocally() {
        const error = CustomerSettings.validateCustomerReference(customerDraft(),activeModelDocument)
        statusError = error.length > 0
        statusText = error.length ? error : (root.modelContractKnown ? "本地基础字段与A_ref检查通过；尚未提交ARM校验，不代表配置可应用或标定完成" : "本地基础字段检查通过；尚未读取schema 2参考坐标，尚未提交ARM校验")
        return error.length === 0
    }
    function saveAndApply() {
        if (!validateLocally()) return
        pendingOperation = 1
        productConfirmation.open()
    }
    AppDialog {
        id: productConfirmation
        width: 490
        anchors.centerIn: Overlay.overlay
        modal: true
        title: root.pendingOperation === 1 ? "确认应用机台基础参数" : "确认保存当前配置为开机配置"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (root.backendObject) root.backendObject.submitCustomerProduct(root.pendingOperation, root.customerDraft(), true)
        contentItem: Label {
            width: 440
            wrapMode: Text.Wrap
            text: root.pendingOperation === 1
                ? "将应用基础参数草稿：" + JSON.stringify(root.customerDraft()) + "。只有ARM确认终态才视为成功。"
                : "将ARM当前已生效配置写为开机配置；不保存尚未应用的本地草稿。"
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 18

        Rectangle {
            Layout.preferredWidth: Math.max(480, root.width * 0.43)
            Layout.fillHeight: true
            color: "#FFFFFF"
            border.color: Palette.border
            radius: 20

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 10
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }

                ColumnLayout {
                    width: Math.max(430, parent.width - 8)
                    spacing: 16

                    Column {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            text: "设备参数"
                            color: Palette.text
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 24
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: "客户基础参数草稿 · 不自动应用或保存"
                            color: Palette.tertiary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 11
                        }
                    }

                    Item { Layout.preferredHeight: 2 }

                    CustomerConfigField {
                        id: deviceModel
                        Layout.fillWidth: true
                        label: "设备型号"
                        numeric: false
                        placeholder: "请输入设备型号"
                        text: "DE168"
                    }
                    CustomerConfigField {
                        id: totalLength
                        objectName: "customerTotalLength"
                        Layout.fillWidth: true
                        label: "拉杆总长度"
                        placeholder: "总长"
                        text: "2500"
                        unit: "mm"
                    }
                    CustomerConfigField {
                        id: bLength
                        objectName: "customerBLength"
                        Layout.fillWidth: true
                        label: "B段长度"
                        placeholder: "B"
                        text: "90"
                        unit: "mm"
                    }
                    CustomerConfigField {
                        id: dLength
                        Layout.fillWidth: true
                        label: "D段长度"
                        placeholder: "D"
                        text: "90"
                        unit: "mm"
                    }
                    CustomerConfigField {
                        id: eLength
                        Layout.fillWidth: true
                        label: "E段长度"
                        placeholder: "E"
                        text: "40"
                        unit: "mm"
                    }
                    CustomerConfigField {
                        id: diameter
                        objectName: "customerDiameterReadOnly"
                        enabled: false
                        Layout.fillWidth: true
                        label: "拉杆直径"
                        placeholder: "--"
                        text: root.activeModelDocument && root.activeModelDocument.rod_diameter_mm ? root.activeModelDocument.rod_diameter_mm.join(", ") : "--"
                        unit: "mm"
                    }

                    Text { Layout.fillWidth:true; text:"杆径、等效面积及参考坐标由工程师分别设置；杆径不自动改写面积。"; color:Palette.tertiary; font.pixelSize:11; wrapMode:Text.Wrap }
                    Text { objectName:"customerReferenceProjection"; Layout.fillWidth:true; text:root.referenceCheck.valid ? "本地草稿A_ref="+root.referenceCheck.referenceA+" mm（使用ARM回读参考坐标；非正式ARM值）" : root.referenceCheck.error; color:root.referenceCheck.valid ? Palette.blue : "#8A6A36"; font.pixelSize:11; wrapMode:Text.Wrap }
                    Text {
                        Layout.fillWidth: true
                        objectName: "customerLocalValidationStatus"
                        text: root.statusText
                        color: root.statusError ? "#B84B42" : Palette.secondary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.backendObject && root.backendObject.productOperation ? String(root.backendObject.productOperation.message || "") : ""
                        color: Palette.secondary
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Text { text: "草稿相对当前运行的基础字段差异"; color: Palette.blue; font.pixelSize: 13 }
                    Text { text: !root.currentBasic ? "尚未读取当前配置，无法比较" : root.draftDifferences.length ? "当前 → 草稿" : "基础字段无差异"; color: Palette.tertiary; font.pixelSize: 11 }
                    Repeater {
                        model: root.draftDifferences
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.label + "：" + modelData.before + " → " + modelData.after
                            color: Palette.secondary; font.pixelSize: 11; wrapMode: Text.WrapAnywhere
                        }
                    }
                    Text { text: "当前运行相对开机配置的基础字段差异"; color: Palette.blue; font.pixelSize: 13 }
                    Text { text: !root.currentBasic || !root.startupBasic ? "当前/开机配置尚未完整读取，无法比较" : root.startupDifferences.length ? "开机 → 当前" : "基础字段无差异"; color: Palette.tertiary; font.pixelSize: 11 }
                    Repeater {
                        model: root.startupDifferences
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.label + "：" + modelData.before + " → " + modelData.after
                            color: Palette.secondary; font.pixelSize: 11; wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
            CustomerActionButton {
                objectName: "customerValidateButton"
                Layout.fillWidth: true
                compact: true
                text: "本地校验草稿"
                onClicked: root.validateLocally()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                CustomerActionButton {
                    objectName: "customerApplyButton"
                    Layout.fillWidth: true
                    compact: true
                    text: "应用当前运行"
                    enabled: root.canWrite && root.backendObject.productCapabilities.modelApply && root.modelContractKnown && root.referenceCheck.valid
                    primary: true
                    onClicked: root.saveAndApply()
                }
                CustomerActionButton {
                    objectName: "customerSaveButton"
                    Layout.fillWidth: true
                    compact: true
                    text: "保存为开机配置"
                    enabled: root.canWrite && root.backendObject.productCapabilities.modelSave && root.modelContractKnown && CustomerSettings.bodyReferenceProjection(root.activeModelDocument).valid
                    onClicked: { root.pendingOperation = 2; productConfirmation.open() }
                }
            }
            Text {
                Layout.fillWidth: true
                text: "升级与参考坐标仅工程师可操作；候选配置不代表标定完成"
                color: Palette.tertiary
                font.pixelSize: 10
                wrapMode: Text.Wrap
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
                anchors.margins: 24
                spacing: 8

                Text {
                    text: "机台几何示意"
                    color: Palette.text
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "同一参考状态：C=C_ref+(M−M_ref)；示意图不授予标定资格"
                    color: "#8290A2"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 11
                }
                Text {
                    Layout.fillWidth: true
                    text: root.backendObject && root.backendObject.productState
                          ? root.backendObject.productState.message : "等待产品状态"
                    color: Palette.tertiary
                    wrapMode: Text.Wrap
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 260
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        Repeater {
                            model: [
                                { title: "当前运行型号", section: "deviceModel", document: "active_document" },
                                { title: "开机型号", section: "deviceModel", document: "startup_document" },
                                { title: "当前输入策略", section: "inputPolicy", document: "active_document" },
                                { title: "开机输入策略", section: "inputPolicy", document: "startup_document" }
                            ]
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Text { text: modelData.title; color: Palette.blue; font.pixelSize: 14 }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.stateDocument(modelData.section, modelData.document)
                                    color: Palette.secondary
                                    font.pixelSize: 11
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                        }
                    }
                }
                Image {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    source: "../assets/customer-machine-side.png"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
            }
        }
    }
}
