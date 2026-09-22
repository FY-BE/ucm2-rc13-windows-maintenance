import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "AppPalette.js" as Palette

Item {
    id: root
    property var backendObject
    property var afeFields: []
    property var algorithmFields: []
    property var lockedFields: []
    property bool usbV2Available: false
    property bool usbV2CatalogReady: false
    property string usbV2Status: ""
    property var usbV2SummaryItems: []
    property var usbV2ParameterFields: []
    property var writableConfigurationFields: []
    property bool usbV2CatalogExpanded: false
    property bool writableCatalogExpanded: false
    property string statusText: ""
    property string identityText: ""
    property var changes: []
    property var errors: []
    property bool hasChanges: false
    property bool prepared: false
    property bool canApply: false
    property bool busy: false
    property bool authorityAvailable: false
    property string controlModeText: "未读取"
    property string controlPhaseText: "状态未知"
    property string authorityStatus: ""
    property string leaseText: ""
    property bool controlModeBusy: false
    property bool canRequestManual: false
    property bool canRequestAutonomous: false
    property bool canResumeManual: false
    readonly property bool anyBusy: busy || controlModeBusy

    signal fieldEdited(string key, var value)
    signal resetRequested()
    signal prepareRequested()
    signal applyRequested()
    signal manualControlRequested()
    signal autonomousControlRequested()
    signal resumeManualControlRequested()

    AppDialog {
        id: applyConfirmation
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "确认易失应用"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.applyRequested()
        contentItem: Column {
            spacing: 10
            width: 430
            Text {
                width: parent.width
                text: "即将把已校验的完整白名单配置发送到ARM。"
                color: Palette.text
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 13
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: "程序会立即独立回读并核对字段与SHA。该操作只修改易失运行配置，不写FPGA、不写非易失存储，设备断电后恢复固件默认/启动配置。"
                color: Palette.secondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                lineHeight: 1.3
            }
        }
    }

    AppDialog {
        id: manualConfirmation
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "确认切换到USB手动模式"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.manualControlRequested()
        contentItem: Text {
            width: 460
            text: "ARM会先安全停机，再把配置与控制权交给本软件。软件每秒续租；失联约5秒后，ARM安全停机成功才恢复自主模式，否则保持故障安全态。"
            color: Palette.secondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            lineHeight: 1.35
        }
    }

    AppDialog {
        id: autonomousConfirmation
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "确认交还ARM自主模式"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.autonomousControlRequested()
        contentItem: Text {
            width: 460
            text: "ARM会先安全停发，然后从冷启动检查、无模板采集和建模流程重新开始。切换完成后USB保持只读，不再覆盖ARM的自动增益、发射和重找波决策。"
            color: Palette.secondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            lineHeight: 1.35
        }
    }

    function optionIndex(options, value) {
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === value)
                return i
        }
        return -1
    }

    component SectionTitle: RowLayout {
        property string title: ""
        property string caption: ""
        Layout.fillWidth: true
        Text {
            text: parent.title
            color: Palette.text
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        Text {
            text: parent.caption
            color: "#8A837D"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 10
        }
        Item { Layout.fillWidth: true }
    }

    component ChoiceCard: GlassPanel {
        property var field: ({})
        Layout.fillWidth: true
        Layout.preferredHeight: 106
        elevated: false
        glassColor: Palette.surface
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: field.label || "--"
                    color: Palette.secondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    text: field.note || ""
                    color: "#9A928C"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 9
                }
            }
            AppComboBox {
                id: choice
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                model: field.options || []
                textRole: "label"
                valueRole: "value"
                currentIndex: root.optionIndex(field.options || [], field.value)
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 11
                enabled: !root.anyBusy
                onActivated: root.fieldEdited(field.key, currentValue)
            }
        }
    }

    component NumberCard: GlassPanel {
        property var field: ({})
        Layout.fillWidth: true
        Layout.preferredHeight: 100
        elevated: false
        glassColor: Palette.surface
        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: field.label || "--"
                    color: Palette.secondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: (field.note || "") + "\n范围 " + field.min + " – " + field.max
                    color: "#938C86"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 9
                    lineHeight: 1.25
                }
            }
            Rectangle {
                Layout.preferredWidth: 112
                Layout.preferredHeight: 42
                radius: 13
                color: "#E9F8F6F4"
                border.color: "#E2DAD4CE"
                AppTextField {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    text: field.value === undefined ? "" : String(field.value)
                    horizontalAlignment: TextInput.AlignRight
                    verticalAlignment: TextInput.AlignVCenter
                    color: "#2F2B28"
                    font.family: "Segoe UI Variable Display Display"
                    font.pixelSize: 15
                    selectByMouse: true
                    enabled: !root.anyBusy
                    background: Item {}
                    onEditingFinished: root.fieldEdited(field.key, text)
                }
            }
            Text {
                Layout.preferredWidth: 28
                text: field.unit || ""
                color: "#766F69"
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 10
            }
        }
    }

    component CapabilityCard: Rectangle {
        property var item: ({})
        Layout.fillWidth: true
        Layout.preferredHeight: 76
        radius: 16
        color: item.active ? "#E8F5F1" : "#F6F1EC"
        border.color: item.active ? "#B8DDD4" : "#DFD5CC"
        Column {
            anchors.fill: parent
            anchors.margins: 11
            spacing: 3
            Text {
                width: parent.width
                text: item.label || "--"
                color: "#8A817A"
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 9
            }
            Text {
                width: parent.width
                text: item.value || "--"
                color: item.active ? "#187864" : "#5E554E"
                font.family: "Segoe UI Variable Display Display"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: item.note || ""
                color: "#928982"
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 8
                elide: Text.ElideRight
            }
        }
    }

    component ParameterCard: Rectangle {
        property var field: ({})
        Layout.fillWidth: true
        Layout.preferredHeight: 82
        radius: 15
        color: "#AFFFFFFF"
        border.color: field.writeActive ? "#99CDBFA4" : "#DED6D0CA"
        RowLayout {
            anchors.fill: parent
            anchors.margins: 11
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 42
                radius: 13
                color: field.writeActive ? "#E8F5F1" : "#F1ECE7"
                Text {
                    anchors.centerIn: parent
                    text: "#" + (field.fieldId || "--")
                    color: field.writeActive ? "#187864" : "#7D746D"
                    font.family: "Cascadia Mono"
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: field.label || "--"
                        color: "#403A36"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        text: (field.scope || "--") + " · " + (field.kind || "--")
                        color: "#887F78"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 8
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: (field.group || "--") + " · " + (field.access || "--")
                          + " · " + (field.writeChannel || "--")
                    color: field.writeActive ? "#248B75" : "#8B6550"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 8
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: field.constraint || "typed字段"
                    color: "#9A928C"
                    font.family: "Cascadia Mono"
                    font.pixelSize: 8
                    elide: Text.ElideRight
                }
            }
        }
    }

    ProductOperationsPanel {
        anchors.fill: parent
        backendObject: root.backendObject
        visible: root.backendObject && (root.backendObject.productReadOnly || root.backendObject.offlinePreview)
    }

    ColumnLayout {
        anchors.fill: parent
        visible: !root.backendObject || (!root.backendObject.productReadOnly && !root.backendObject.offlinePreview)
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            Column {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: "设备配置"
                    color: Palette.text
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "完整白名单对象 · 本地校验 · 易失提交边界"
                    color: Palette.secondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 12
                }
            }
            Rectangle {
                width: 152; height: 34; radius: 17
                color: root.anyBusy ? "#E54D7CFE"
                      : root.prepared ? "#E52AA198" : Palette.surface
                border.color: Palette.surface
                Text {
                    anchors.centerIn: parent
                    text: root.anyBusy ? "正在与ARM通信"
                          : root.prepared ? "候选已校验" : "尚未准备"
                    color: root.anyBusy || root.prepared ? "white" : "#6C655F"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
        }

        GlassPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 116
            elevated: true
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16
                Rectangle {
                    width: 10; height: 60; radius: 5
                    color: root.errors.length > 0 ? "#D84F4F"
                          : root.prepared ? Palette.green : "#4D7CFE"
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: root.statusText
                        color: "#383430"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.errors.length > 0
                              ? root.errors.join(" · ")
                              : root.identityText
                        color: root.errors.length > 0 ? Palette.red : "#837B75"
                        font.family: "Cascadia Mono"
                        font.pixelSize: 9
                        elide: Text.ElideMiddle
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.changes.length > 0
                              ? "差异：" + root.changes.join("；")
                              : "当前没有候选差异"
                        color: "#9A928C"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }
                BusyIndicator {
                    visible: root.anyBusy
                    running: root.anyBusy
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                }
                AppButton {
                    text: "恢复活动值"
                    enabled: root.hasChanges && !root.anyBusy
                    onClicked: root.resetRequested()
                }
                AppButton {
                    text: "准备并本地校验"
                    enabled: root.hasChanges && !root.prepared && !root.anyBusy
                    highlighted: true
                    onClicked: root.prepareRequested()
                }
                AppButton {
                    Layout.preferredWidth: 160
                    Layout.preferredHeight: 50
                    text: root.busy ? "正在提交并回读…"
                          : root.controlModeBusy ? "正在切换控制模式…"
                          : root.canApply ? "易失应用并回读" : "等待本地校验"
                    enabled: root.canApply && !root.anyBusy
                    onClicked: applyConfirmation.open()
                }
            }
        }

        ScrollView {
            id: configurationScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: configurationScroll.availableWidth
                spacing: 10

                SectionTitle {
                    title: "设备控制模式"
                    caption: "工程师权限≠设备控制权；模式由ARM回读确认"
                }
                GlassPanel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 176
                    elevated: true
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 16
                        Rectangle {
                            Layout.preferredWidth: 220
                            Layout.fillHeight: true
                            radius: 18
                            color: root.controlModeText === "USB手动模式"
                                   ? "#183249" : "#E8F5F1"
                            Column {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 7
                                Text {
                                    text: "ARM实际状态"
                                    color: root.controlModeText === "USB手动模式"
                                           ? "#9CB7CE" : "#6F837C"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 10
                                }
                                Text {
                                    width: parent.width
                                    text: root.controlModeText
                                    color: root.controlModeText === "USB手动模式"
                                           ? "white" : "#187864"
                                    font.family: "Segoe UI Variable Display Display"
                                    font.pixelSize: 22
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: root.controlPhaseText
                                    color: root.controlModeText === "USB手动模式"
                                           ? Palette.orange : "#4F7168"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 7
                            Text {
                                Layout.fillWidth: true
                                text: root.leaseText
                                color: Palette.secondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.authorityStatus
                                color: root.authorityAvailable ? "#827970" : Palette.red
                                font.family: "Cascadia Mono"
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                            Item { Layout.fillHeight: true }
                            RowLayout {
                                Layout.fillWidth: true
                                AppButton {
                                    text: "ARM自主"
                                    enabled: root.canRequestAutonomous
                                    onClicked: autonomousConfirmation.open()
                                }
                                AppButton {
                                    text: "USB手动"
                                    enabled: root.canRequestManual
                                    highlighted: root.canRequestManual
                                    onClicked: manualConfirmation.open()
                                }
                                AppButton {
                                    visible: root.canResumeManual
                                    text: "恢复手动控制"
                                    highlighted: true
                                    onClicked: root.resumeManualControlRequested()
                                }
                                BusyIndicator {
                                    visible: root.controlModeBusy
                                    running: root.controlModeBusy
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                }

                SectionTitle {
                    title: "USB V2 后端能力"
                    caption: "严格按ARM返回的supported / active位显示，不从字段名猜能力"
                }
                GlassPanel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.usbV2SummaryItems.length > 3 ? 232 : 142
                    elevated: false
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            Rectangle {
                                width: 8; height: 28; radius: 4
                                color: root.usbV2CatalogReady ? Palette.green : Palette.orange
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.usbV2Status
                                color: "#554E48"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                width: 110; height: 28; radius: 14
                                color: root.usbV2CatalogReady ? "#E8F5F1" : "#FFF0DD"
                                Text {
                                    anchors.centerIn: parent
                                    text: root.usbV2CatalogReady ? "V2目录已校验" : "V2只读未就绪"
                                    color: root.usbV2CatalogReady ? "#187864" : "#A96B20"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            columns: 3
                            columnSpacing: 9
                            rowSpacing: 9
                            Repeater {
                                model: root.usbV2SummaryItems
                                delegate: CapabilityCard { item: modelData }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.usbV2CatalogReady
                    Text {
                        Layout.fillWidth: true
                        text: "产品可写配置目录合并型号几何/标定与运行参数；Apply和SaveStartup是两个独立动作。滤波系数和安全状态机不开放。"
                        color: "#6E7F94"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                    AppButton {
                        text: root.writableCatalogExpanded
                              ? "收起可写目录"
                              : "查看可写目录 " + root.writableConfigurationFields.length + " 项"
                        onClicked: root.writableCatalogExpanded = !root.writableCatalogExpanded
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    visible: root.usbV2CatalogReady && root.writableCatalogExpanded
                    columns: 3
                    columnSpacing: 9
                    rowSpacing: 9
                    Repeater {
                        model: root.writableConfigurationFields
                        delegate: ParameterCard { field: modelData }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.usbV2CatalogReady
                    Text {
                        Layout.fillWidth: true
                        text: "完整msg17 wire目录固定64项，包含只读运行态和冻结的安全顺序字段。"
                        color: "#8A817A"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 9
                    }
                    AppButton {
                        text: root.usbV2CatalogExpanded
                              ? "收起参数目录"
                              : "查看全部 " + root.usbV2ParameterFields.length + " 项"
                        onClicked: root.usbV2CatalogExpanded = !root.usbV2CatalogExpanded
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    visible: root.usbV2CatalogReady && root.usbV2CatalogExpanded
                    columns: 3
                    columnSpacing: 9
                    rowSpacing: 9
                    Repeater {
                        model: root.usbV2ParameterFields
                        delegate: ParameterCard { field: modelData }
                    }
                }

                SectionTitle {
                    title: "采集与前端"
                    caption: "只有协议与产品共同允许的离散值"
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 10
                    Repeater {
                        model: root.afeFields
                        delegate: ChoiceCard { field: modelData }
                    }
                }

                SectionTitle {
                    Layout.topMargin: 4
                    title: "算法与正式值门控"
                    caption: "数值先进入候选对象，准备后统一校验"
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 10
                    Repeater {
                        model: root.algorithmFields
                        delegate: NumberCard { field: modelData }
                    }
                }

                SectionTitle {
                    Layout.topMargin: 4
                    title: "锁定的产品合同"
                    caption: "可查看、随完整对象提交，但不能在界面中自由改写"
                }
                GlassPanel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 210
                    elevated: false
                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        columns: 2
                        columnSpacing: 24
                        rowSpacing: 10
                        Repeater {
                            model: root.lockedFields
                            delegate: Column {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: modelData.label || "--"
                                    color: "#8C847E"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 9
                                }
                                Text {
                                    width: parent.width
                                    text: modelData.value || "--"
                                    color: "#45403C"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: modelData.note || ""
                                    color: "#A09892"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 8
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
                Item { Layout.preferredHeight: 4 }
            }
        }
    }
}
