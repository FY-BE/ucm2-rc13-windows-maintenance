import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components/AppPalette.js" as Palette

ApplicationWindow {
    id: window
    required property bool systemBackdropRequested
    font.family: "Microsoft YaHei UI"
    width: 1366
    height: 768
    minimumWidth: 1180
    minimumHeight: 700
    visible: true
    title: "启真传感 · UCM"
    color: Palette.canvas

    property bool engineerPreview: false
    readonly property bool engineerPresentation: backend.engineerMode || (engineerPreview && backend.offlinePreview)
    property int selectedPage: 0
    property bool nativeBackdropActive: false
    readonly property color brandBlue: Palette.blue
    readonly property var customerNavigation: [
        { "page": 0, "icon": "⌂", "title": "监测总览" },
        { "page": 1, "icon": "⌁", "title": "实时趋势" },
        { "page": 5, "icon": "◷", "title": "历史事件" }
    ]
    readonly property var engineerNavigation: [
        { "page": 0, "icon": "⌂", "title": "设备概览" },
        { "page": 1, "icon": "⌁", "title": "实时趋势" },
        { "page": 2, "icon": "◇", "title": "四杆诊断" },
        { "page": 3, "icon": "⚙", "title": "设备配置" },
        { "page": 4, "icon": "≡", "title": "日志与事件" },
        { "page": 7, "icon": "▣", "title": "标准力标定" },
        { "page": 6, "icon": "✓", "title": "维护与证据" },
        { "page": 9, "icon": "↔", "title": "模拟主站" }
    ]
    readonly property var navigationItems: window.engineerPresentation
                                                   ? engineerNavigation
                                                   : customerNavigation

    function openEngineerDialog() {
        pinField.text = ""
        confirmationField.text = ""
        engineerDialog.open()
    }

    onSelectedPageChanged: {
        backend.setActivePage(selectedPage)
        if (selectedPage === 4) {
            backend.refreshLogSources()
            backend.refreshTimeline()
        } else if (selectedPage === 5)
            backend.refreshTimeline()
        else if (selectedPage === 6) {
            backend.refreshEvidenceSummary()
        }
    }
    Component.onCompleted: backend.setActivePage(selectedPage)

    Connections {
        target: backend
        function onAccessModeChanged() {
            if (!window.engineerPresentation)
                window.selectedPage = 0
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Palette.canvas
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 18
        visible: window.engineerPresentation

        GlassPanel {
            id: sidebar
            elevated: false
            Layout.preferredWidth: 218
            Layout.fillHeight: true
            cornerRadius: 24
            glassColor: Palette.surface
            strokeColor: Palette.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 62
                    spacing: 11
                    Rectangle {
                        width: 44
                        height: 44
                        radius: 13
                        color: Palette.canvas
                        clip: true
                        Image {
                            anchors.fill: parent
                            anchors.margins: 5
                            source: "assets/qizhen-ucm-icon.png"
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }
                    }
                    Column {
                        spacing: 1
                        Text {
                            text: "启真传感"
                            color: Palette.text
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: "QIZHEN · UCM"
                            color: Palette.tertiary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 9
                            font.letterSpacing: 1.1
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Palette.border
                    Layout.bottomMargin: 8
                }

                Repeater {
                    model: window.navigationItems
                    delegate: NavButton {
                        Layout.fillWidth: true
                        iconText: modelData.icon
                        label: modelData.title
                        selected: window.selectedPage === modelData.page
                        onClicked: window.selectedPage = modelData.page
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: window.engineerPresentation ? 108 : 116
                    radius: 17
                    color: Palette.surfaceMuted
                    border.color: Palette.border
                    Column {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 7
                        Row {
                            spacing: 7
                            Rectangle {
                                width: 7; height: 7; radius: 4
                                color: window.engineerPresentation ? Palette.blue : Palette.green
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: window.engineerPreview ? "离线工程预览" : window.engineerPresentation ? "工程师模式" : "客户模式"
                                color: Palette.text
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            width: parent.width
                            text: window.engineerPresentation
                                  ? "诊断、配置与交付工具\n操作权限以 ARM 回报为准"
                                  : "日常监测与历史事件\n工程配置和诊断入口已锁定"
                            color: Palette.secondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            lineHeight: 1.35
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 54
                Layout.minimumHeight: 54
                Layout.maximumHeight: 54
                radius: 16
                color: Palette.surface
                border.color: Palette.border

                RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 10
                spacing: 10

                Row {
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: window.engineerPresentation ? Palette.blue : Palette.green
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: window.engineerPresentation ? "工程师工作台" : "客户监测"
                        color: Palette.text
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: window.engineerPreview ? "离线预览 · 未取得工程权限"
                            : window.engineerPresentation
                                ? "ARM · " + ((backend.runtimeProgress || ({})).available
                                    ? backend.runtimeProgress.stageText + " · "
                                      + (Number(backend.runtimeProgress.overallPermille) / 10).toFixed(1) + "%"
                                    : backend.runtimeStateText)
                            : "只显示运行所需信息"
                        color: Palette.tertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Rectangle {
                    width: window.engineerPresentation ? 126 : 102
                    height: 32
                    radius: 16
                    color: Palette.surface
                    border.color: Palette.border
                    Row {
                        anchors.centerIn: parent
                        spacing: 7
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: backend.connected ? Palette.green : Palette.orange
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: window.engineerPresentation ? backend.usbLabel
                                                       : backend.connected ? "设备在线" : "设备离线"
                            color: Palette.secondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 10
                        }
                    }
                }

                Rectangle {
                    visible: window.engineerPresentation
                    width: 144
                    height: 32
                    radius: 16
                    color: Palette.surface
                    border.color: Palette.border
                    Row {
                        anchors.centerIn: parent
                        spacing: 7
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: backend.referenceForceReady ? Palette.green : Palette.orange
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: backend.referenceForceReady ? "标准力在线" : "标准力待连接"
                            color: Palette.secondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 10
                        }
                    }
                }

                Rectangle {
                    visible: window.engineerPresentation
                    width: 136
                    height: 32
                    radius: 16
                    color: Palette.surface
                    border.color: Palette.border
                    Row {
                        anchors.centerIn: parent
                        spacing: 7
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: backend.ethercatProbe.detected ? Palette.green : Palette.orange
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: backend.ethercatProbe.detected
                                  ? "主站 · " + backend.ethercatProbe.slaveCount + " 从站"
                                  : "主站设备待检测"
                            color: Palette.secondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 10
                        }
                    }
                }

                AppButton {
                    text: backend.connected ? "重新连接" : "连接设备"
                    Layout.preferredWidth: 98
                    Layout.preferredHeight: 34
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
                                           : parent.hovered ? "#2369C8" : window.brandBlue
                    }
                    onClicked: backend.reconnect()
                }

                AppButton {
                    visible: window.engineerPresentation
                    text: backend.referenceForceRunning ? "断开标准力" : "连接标准力"
                    Layout.preferredWidth: 106
                    Layout.preferredHeight: 34
                    contentItem: Text {
                        text: parent.text
                        color: window.brandBlue
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 11
                        color: parent.down ? "#DDE9F7" : parent.hovered ? "#EEF4FC" : Palette.surface
                        border.color: "#8BAED8"
                    }
                    onClicked: backend.toggleReferenceForce()
                }

                AppButton {
                    text: window.engineerPreview ? "返回客户预览" : window.engineerPresentation ? "退出工程师模式" : "进入工程师模式"
                    Layout.preferredWidth: 132
                    Layout.preferredHeight: 34
                    contentItem: Text {
                        text: parent.text
                        color: window.engineerPresentation ? "#43536A" : window.brandBlue
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 11
                        color: parent.down ? "#DDE5EE" : parent.hovered ? "#EDF2F8" : Palette.surface
                        border.color: Palette.borderStrong
                    }
                    onClicked: {
                        if (window.engineerPresentation)
                            backend.leaveEngineerMode()
                        else
                            window.openEngineerDialog()
                    }
                }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.selectedPage

                ColumnLayout {
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        Layout.minimumHeight: 52
                        Layout.maximumHeight: 52
                        Column {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: window.engineerPresentation ? "设备概览" : "监测总览"
                                color: Palette.text
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 27
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: window.engineerPresentation
                                      ? "四杆测量、过程状态与链路健康"
                                      : "设备当前拉力与运行状态"
                                color: Palette.secondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 11
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(180, Math.max(160, window.height * 0.18))
                        Layout.minimumHeight: Layout.preferredHeight
                        Layout.maximumHeight: Layout.preferredHeight
                        spacing: 12
                        MetricCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 1
                            title: window.engineerPresentation ? "正式总力" : "总拉力"
                            symbol: "▮"
                            value: backend.totalForceText
                            unit: "N"
                            subtitle: window.engineerPresentation
                                      ? "仅显示 ARM FORMAL_VALID，不拼接局部值"
                                      : "来自设备的正式有效结果"
                            hero: true
                        }
                        MetricCard {
                            visible: window.engineerPresentation
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            title: "标准四杆合计"
                            symbol: "◎"
                            value: backend.referenceTotalForceText
                            unit: "kN"
                            subtitle: backend.referenceTotalDifferenceText
                            accent: "#E96A4A"
                        }
                        MetricCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            title: "不平衡指数"
                            symbol: "∿"
                            value: backend.imbalanceText
                            unit: "%"
                            subtitle: window.engineerPresentation ? "跟随正式总力有效门" : "四根拉杆受力均匀程度"
                            accent: Palette.green
                        }
                        MetricCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            title: "过程状态"
                            symbol: "⌁"
                            value: backend.processStateText
                            subtitle: window.engineerPresentation ? "ARM 九态过程状态" : "设备当前运行阶段"
                            accent: "#7B68C8"
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(188, Math.max(170, window.height * 0.19))
                        Layout.minimumHeight: Layout.preferredHeight
                        Layout.maximumHeight: Layout.preferredHeight
                        spacing: 12
                        Repeater {
                            model: [Palette.rod1, Palette.rod2, Palette.rod3, Palette.rod4]
                            delegate: ForceComparisonCard {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                title: window.engineerPresentation
                                       ? "拉杆 " + (index + 1) + " · ADC" + index
                                       : "拉杆 " + (index + 1)
                                ucmValue: backend.rodForceTexts[index]
                                referenceValue: backend.referenceForceTexts[index]
                                comparisonText: backend.referenceDifferenceTexts[index]
                                stateText: window.engineerPresentation
                                           ? backend.rodStateTexts[index]
                                           : backend.telemetryReady
                                             ? "数据正常" : "等待设备数据"
                                showReference: window.engineerPresentation
                                accent: modelData
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 12
                        TrendCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 3
                            source: backend.trendBuffer
                        }
                        GlassPanel {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 1
                            performanceSensitive: true
                            Column {
                                anchors.fill: parent
                                anchors.margins: 18
                                spacing: 7
                                Text {
                                    text: window.engineerPresentation ? "最近事件" : "设备状态"
                                    color: Palette.text
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                }
                                Repeater {
                                    model: window.engineerPresentation ? [
                                        ["USB", backend.usbLabel,
                                         backend.connected ? Palette.green : Palette.orange],
                                        ["ARM", backend.processStateText,
                                         backend.telemetryReady ? Palette.green : Palette.orange],
                                        ["运行链", backend.runtimeStateText,
                                         backend.runtimeReady ? Palette.green : Palette.orange],
                                        ["标准力", backend.referenceForceStatus,
                                         backend.referenceForceReady ? Palette.green : Palette.orange],
                                        ["测量", backend.eventText, Palette.tertiary]
                                    ] : [
                                        ["设备连接", backend.connected ? "在线" : "离线",
                                         backend.connected ? Palette.green : Palette.orange],
                                        ["测量状态", backend.telemetryReady ? "数据正常" : "等待有效数据",
                                         backend.telemetryReady ? Palette.green : Palette.orange],
                                        ["过程状态", backend.processStateText,
                                         backend.telemetryReady ? "#4778E7" : Palette.tertiary]
                                    ]
                                    delegate: Row {
                                        width: parent.width
                                        spacing: 9
                                        Rectangle {
                                            width: 8; height: 8; radius: 4
                                            color: modelData[2]
                                            anchors.top: parent.top
                                            anchors.topMargin: 5
                                        }
                                        Column {
                                            width: parent.parent.width - 20
                                            spacing: 2
                                            Text {
                                                text: modelData[0]
                                                color: Palette.secondary
                                                font.family: "Microsoft YaHei UI"
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                            }
                                            Text {
                                                text: modelData[1]
                                                color: Palette.tertiary
                                                font.family: "Microsoft YaHei UI"
                                                font.pixelSize: 10
                                                width: parent.width
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }
                                Item { width: 1; height: 2; visible: parent.height >= 360 }
                                Rectangle { width: parent.width; height: 1; color: Palette.border; visible: parent.height >= 360 }
                                Text {
                                    width: parent.width
                                    visible: parent.height >= 360
                                    text: window.engineerPresentation
                                          ? "UCM 只认 ARM 有效数据；标准力独立显示，不回写设备"
                                          : backend.telemetryReady
                                            ? "设备数据正在正常更新"
                                            : "设备在线不代表测量已经就绪"
                                    color: "#7E8B9C"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                    lineHeight: 1.3
                                }
                            }
                        }
                    }
                }

                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 12
                        Column {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            spacing: 2
                            Text {
                                text: "实时趋势"
                                color: Palette.text
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 27
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: window.engineerPresentation
                                      ? "最近 60 秒四杆力值与原始波形，光标按同一坐标联动"
                                      : "最近 60 秒四杆力值，移动光标可同时查看四杆"
                                color: Palette.secondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 11
                            }
                        }
                        TrendCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: window.engineerPresentation ? 1 : 2
                            source: backend.trendBuffer
                        }
                        WaveformCard {
                            visible: window.engineerPresentation
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: 1
                            series: backend.waveformSeries
                            windowStart: backend.waveformStart
                            sampleRateHz: backend.waveformSampleRateHz
                            statusText: backend.waveformStatus
                        }
                    }
                }

                DiagnosticPage {
                    backendObject: backend
                    rods: backend.diagnosticRods
                    summary: backend.diagnosticSummary
                    telemetryReady: backend.telemetryReady
                }
                ConfigurationPage {
                    backendObject: backend
                    afeFields: backend.afeConfigurationFields
                    algorithmFields: backend.algorithmConfigurationFields
                    lockedFields: backend.lockedConfigurationFields
                    usbV2Available: backend.usbV2Available
                    usbV2CatalogReady: backend.usbV2CatalogReady
                    usbV2Status: backend.usbV2Status
                    usbV2SummaryItems: backend.usbV2SummaryItems
                    usbV2ParameterFields: backend.usbV2ParameterFields
                    writableConfigurationFields: backend.writableConfigurationFields
                    statusText: backend.configurationStatus
                    identityText: backend.configurationIdentity
                    changes: backend.configurationChanges
                    errors: backend.configurationErrors
                    hasChanges: backend.configurationHasChanges
                    prepared: backend.configurationPrepared
                    canApply: backend.configurationCanApply
                    busy: backend.configurationBusy
                    authorityAvailable: backend.controlAuthorityAvailable
                    controlModeText: backend.deviceControlModeText
                    controlPhaseText: backend.deviceControlPhaseText
                    authorityStatus: backend.controlAuthorityStatus
                    leaseText: backend.controlLeaseText
                    controlModeBusy: backend.controlModeBusy
                    canRequestManual: backend.canRequestManualControl
                    canRequestAutonomous: backend.canRequestAutonomousControl
                    canResumeManual: backend.canResumeManualControl
                    onFieldEdited: function(key, value) { backend.setConfigurationField(key, value) }
                    onResetRequested: backend.resetConfigurationDraft()
                    onPrepareRequested: backend.prepareConfiguration()
                    onApplyRequested: backend.applyPreparedConfiguration()
                    onManualControlRequested: backend.requestManualControl()
                    onAutonomousControlRequested: backend.requestAutonomousControl()
                    onResumeManualControlRequested: backend.resumeManualControl()
                }
                LogPage {
                    backendObject: backend
                    sources: backend.logSources
                    statusText: backend.logStatus
                    contentText: backend.logContent
                    selectedTitle: backend.selectedLogTitle
                    selectedSourceId: backend.selectedLogSourceId
                    onRefreshRequested: backend.refreshLogSources()
                    onSourceRequested: function(sourceId) { backend.loadLogSource(sourceId) }
                    onOpenTimelineRequested: window.selectedPage = 5
                }
                TimelinePage {
                    engineerMode: window.engineerPresentation
                    events: backend.timelineEvents
                    statusText: backend.timelineStatus
                    onRefreshRequested: backend.refreshTimeline()
                    onBackRequested: window.selectedPage = 4
                }
                EvidencePage {
                    backendObject: backend
                    items: backend.evidenceItems
                    statusText: backend.evidenceStatus
                    packagePath: backend.evidencePath
                    canExport: backend.canExportEvidence
                    onRefreshRequested: backend.refreshEvidenceSummary()
                    onExportRequested: function(destination) { backend.exportDiagnosticPackage(destination) }
                    onOpenConnectionRequested: window.selectedPage = 8
                }
                CalibrationPage {
                    calibration: backend.calibration
                    productState: backend.productState
                    backendObject: backend
                }
                ConnectionPage {
                    backendObject: backend
                    onBackRequested: window.selectedPage = 6
                }
                EthercatMasterPage {
                    backendObject: backend
                }
            }
        }
    }

    CustomerModeShell {
        anchors.fill: parent
        visible: !window.engineerPresentation
        backendObject: backend
        initialPage: window.selectedPage === 5 ? 2
                                               : Math.min(window.selectedPage, 2)
        onEngineerRequested: window.openEngineerDialog()
    }

    AppDialog {
        id: engineerDialog
        width: 430
        height: backend.engineerPinConfigured ? 330 : 410
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 0
        background: Rectangle {
            radius: 24
            color: "#FBFFFFFF"
            border.color: "#D4DFEC"
            border.width: 1
        }
        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 26
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                Rectangle {
                    width: 42; height: 42; radius: 13
                    color: "#E8F1FC"
                    Text {
                        anchors.centerIn: parent
                        text: "⚙"
                        color: window.brandBlue
                        font.family: "Segoe UI Symbol"
                        font.pixelSize: 18
                    }
                }
                Column {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: backend.engineerPinConfigured
                              ? "进入工程师模式" : "首次设置工程师 PIN"
                        color: Palette.text
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 21
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: backend.engineerPinConfigured
                              ? "验证后开放诊断、配置、日志和交付工具"
                              : "首次安装由工程师设置；以后每次启动仍默认客户模式"
                        color: Palette.secondary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 10
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#E2E8F0" }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: "工程师 PIN"
                    color: Palette.secondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                AppTextField {
                    id: pinField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    placeholderText: "请输入 6–12 位数字"
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhDigitsOnly
                    maximumLength: 12
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,12}/ }
                    selectByMouse: true
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 15
                    background: Rectangle {
                        radius: 11
                        color: Palette.canvasRaised
                        border.color: pinField.activeFocus ? window.brandBlue : Palette.border
                    }
                    onAccepted: {
                        if (backend.engineerPinConfigured
                                && backend.enterEngineerMode(text))
                            engineerDialog.close()
                    }
                }
            }

            ColumnLayout {
                visible: !backend.engineerPinConfigured
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: "再次输入 PIN"
                    color: Palette.secondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                AppTextField {
                    id: confirmationField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    placeholderText: "请再次输入"
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhDigitsOnly
                    maximumLength: 12
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,12}/ }
                    selectByMouse: true
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 15
                    background: Rectangle {
                        radius: 11
                        color: Palette.canvasRaised
                        border.color: confirmationField.activeFocus ? window.brandBlue : Palette.border
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: backend.accessMessage
                color: window.engineerPresentation ? "#168878" : "#778598"
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.fillWidth: true }
                AppButton {
                    text: "取消"
                    Layout.preferredWidth: 86
                    Layout.preferredHeight: 38
                    onClicked: engineerDialog.close()
                    background: Rectangle {
                        radius: 11
                        color: parent.hovered ? "#E9EEF5" : Palette.canvas
                        border.color: Palette.borderStrong
                    }
                }
                AppButton {
                    text: backend.engineerPinConfigured ? "验证并进入" : "设置并进入"
                    Layout.preferredWidth: 112
                    Layout.preferredHeight: 38
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
                                           : parent.hovered ? "#2369C8" : window.brandBlue
                    }
                    onClicked: {
                        const accepted = backend.engineerPinConfigured
                            ? backend.enterEngineerMode(pinField.text)
                            : backend.configureEngineerPin(pinField.text,
                                                           confirmationField.text)
                        if (accepted)
                            engineerDialog.close()
                    }
                }
            }
        }
        onOpened: pinField.forceActiveFocus()
    }
}
