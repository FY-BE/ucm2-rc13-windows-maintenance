import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "AppPalette.js" as Palette

Item {
    id: root
    required property QtObject calibration
    required property var productState
    required property QtObject backendObject
    property string acceptedSessionId: ""
    property string validatedSessionId: ""
    property string applyAckSessionId: ""
    property string appliedSessionId: ""
    property string saveAckSessionId: ""
    property string savedSessionId: ""
    property int pendingOperation: 0
    property int requestedOperation: 0
    property url manualPhoto: ""
    readonly property var candidate: calibration.candidate || ({})
    readonly property var activeDevice: productState.deviceModel || ({})
    readonly property var readbackState: backendObject.calibrationReadback || ({})
    readonly property bool readbackAvailable: backendObject.connected === true
        && backendObject.offlinePreview !== true && readbackState.outcome === "succeeded"
        && readbackState.active_document !== undefined
    readonly property var capabilities: backendObject.productCapabilities || ({})
    readonly property bool candidateReady: candidate.status === "candidate"
        && candidate.fit_constraint === "shared_kmat_shared_b" && candidate.session_id
        && candidate.schema === "ucm-windows-calibration-candidate/v3"
        && calibration.sessionPath.endsWith(candidate.session_id)
        && candidate.b_total_ns === undefined
        && Array.isArray(candidate.coupling_bias_ns) && candidate.coupling_bias_ns.length === 4
        && candidate.coupling_bias_ns.every(function(value) { return typeof value === "number" && isFinite(value) })
    readonly property bool identityMatches: productState.success === true && activeDevice.available === true
        && !!activeDevice.active_identity && !!candidate.active_model_identity
        && ["identitySha256", "generation", "bootId", "authorityGeneration", "sessionId"]
             .every(function(key) { return String(activeDevice.active_identity[key]) === String(candidate.active_model_identity[key]) })
    readonly property bool canSubmit: backendObject.engineerMode === true && backendObject.connected === true
        && backendObject.offlinePreview !== true && backendObject.productCanWrite === true
        && !calibration.running && !calibration.busy && pendingOperation === 0
    function currentHasCandidate() {
        let doc = activeDevice.active_document || ({})
        let correction = candidate.force_correction || ({model:"IDENTITY",knot_count:0,input_force_n:[],output_force_n:[]})
        let input = (correction.input_force_n || []).slice()
        let output = (correction.output_force_n || []).slice()
        while (input.length < 8) input.push(0)
        while (output.length < 8) output.push(0)
        return Number(doc.kmat_unified) === Number(candidate.kmat_unified)
            && Array.isArray(candidate.coupling_bias_ns)
            && candidate.coupling_bias_ns.length === 4
            && JSON.stringify(doc.coupling_bias_ns) === JSON.stringify(candidate.coupling_bias_ns)
            && Number(doc.force_correction_knot_count || 0) === Number(correction.knot_count || 0)
            && JSON.stringify(doc.force_correction_input_n || [0,0,0,0,0,0,0,0]) === JSON.stringify(input)
            && JSON.stringify(doc.force_correction_output_n || [0,0,0,0,0,0,0,0]) === JSON.stringify(output)
    }
    function parameterSummary(document) {
        if (!document || document.kmat_unified === undefined) return "尚未取得 ARM 参数"
        let points = []
        for (let i = 0; i < Number(document.force_correction_knot_count || 0); ++i)
            points.push(String((document.force_correction_input_n || [])[i]) + " → "
                + String((document.force_correction_output_n || [])[i]))
        return "型号：" + String(document.model_name || document.pack_id || document.device_model_id || "--")
            + "\nKmat：" + String(document.kmat_unified)
            + "\n四路耦合/电路偏置 (ns)：" + JSON.stringify(document.coupling_bias_ns || [])
            + "\n分段校正 (N)：" + (points.length ? points.join("；") : "恒等，无分段点")
    }
    function checkReadback() {
        if (applyAckSessionId === candidate.session_id && activeDevice.available === true
            && !identityMatches && currentHasCandidate()) appliedSessionId = candidate.session_id
        if (saveAckSessionId === candidate.session_id && appliedSessionId === candidate.session_id
            && activeDevice.startup_identity && activeDevice.active_identity
            && activeDevice.startup_identity.identitySha256 === activeDevice.active_identity.identitySha256
            && JSON.stringify(activeDevice.startup_document) === JSON.stringify(activeDevice.active_document))
            savedSessionId = candidate.session_id
    }
    function checkOperation() {
        let result = backendObject.calibrationOperation || ({})
        if (pendingOperation === 0 || result.outcome === "pending") return
        if (result.operation === 0 && result.outcome === "failed") { pendingOperation = 0; return }
        if (result.operation !== pendingOperation || result.busy === true) return
        if (result.outcome === "succeeded" && !result.evidenceWarning
            && acceptedSessionId === candidate.session_id) {
            if (pendingOperation === 3) validatedSessionId = candidate.session_id
            if (pendingOperation === 1) applyAckSessionId = candidate.session_id
            if (pendingOperation === 2) saveAckSessionId = candidate.session_id
        }
        pendingOperation = 0
        checkReadback()
    }
    Connections {
        target: root.backendObject
        function onStateChanged() { root.checkOperation(); root.checkReadback() }
        function onConnectionChanged() {
            if (!root.backendObject.connected) {
                root.validatedSessionId = ""
                root.applyAckSessionId = ""
                root.appliedSessionId = ""
                root.saveAckSessionId = ""
                root.savedSessionId = ""
                root.pendingOperation = 0
            }
        }
    }
    property var workingRois: ({})
    property string selectedRoi: "S1"
    property string recognizedRoisJson: ""
    readonly property var roiNames: ["S1", "S2", "S3", "S4", "Total", "Other"]

    function copyRois() {
        let next = {}
        for (let name of roiNames) {
            let box = calibration.rois[name]
            next[name] = box ? [box[0], box[1], box[2], box[3]] : [0, 0, 1, 1]
        }
        workingRois = next
    }
    Component.onCompleted: copyRois()

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label { text: "标准力标定"; font.pixelSize: 23; font.bold: true; color: Palette.text }
            Label {
                text: "先核对六个 ROI 和 S1–S4，再单独开始标定。拟合候选经人工确认、ARM 校验后，方可单独应用到当前运行；开机保存另行确认。"
                color: Palette.secondary; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            RowLayout {
                AppButton { text: "打开 ROI / 拍照"; enabled: !calibration.running && !calibration.busy
                    onClicked: { root.copyRois(); root.recognizedRoisJson = ""; roiDialog.open(); calibration.capturePreview() } }
                Label { text: calibration.roiConfirmed ? "ROI 已确认" : "ROI 待确认"; color: calibration.roiConfirmed ? "#168672" : "#B47721" }
                AppTextField { id: operatorField; placeholderText: "操作者姓名或工号"; Layout.preferredWidth: 190 }
                AppButton { text: "开始标定"; enabled: calibration.roiConfirmed && !calibration.running && !calibration.busy && operatorField.text.trim().length > 0
                    onClicked: calibration.startCalibration(operatorField.text, root.productState) }
                AppButton { text: "人工输入兜底"; enabled: !calibration.running && !calibration.busy
                        && operatorField.text.trim().length > 0 && backendObject.connected
                    onClicked: calibration.startManualCalibration(operatorField.text, root.productState) }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Palette.border }
            RowLayout {
                visible: calibration.manualMode && calibration.running
                AppTextField { id: manualForce1; placeholderText: "杆1 kN"; Layout.preferredWidth: 100 }
                AppTextField { id: manualForce2; placeholderText: "杆2 kN"; Layout.preferredWidth: 100 }
                AppTextField { id: manualForce3; placeholderText: "杆3 kN"; Layout.preferredWidth: 100 }
                AppTextField { id: manualForce4; placeholderText: "杆4 kN"; Layout.preferredWidth: 100 }
                AppButton { text: root.manualPhoto.toString().length > 0 ? "已选现场照片" : "选择现场照片";
                    onClicked: manualPhotoDialog.open() }
                AppButton { text: "开始人工本档"; enabled: !calibration.stageRecording
                    && root.manualPhoto.toString().length > 0
                    onClicked: calibration.beginManualStage(
                        [manualForce1.text, manualForce2.text, manualForce3.text, manualForce4.text], root.manualPhoto) }
            }
            RowLayout {
                AppButton { text: "开始记录本档"; enabled: calibration.running && !calibration.stageRecording
                        && !calibration.manualMode
                    onClicked: calibration.beginStage() }
                AppButton { text: "结束本档"; enabled: calibration.stageRecording
                    onClicked: calibration.endStage() }
                AppButton { text: "结束并拟合"; enabled: calibration.running && calibration.stageCount > 0
                    onClicked: calibration.finishCalibration() }
                Label { text: "已划定 " + calibration.stageCount + " 档"; color: Palette.text }
            }
            Label { text: "相机帧 " + calibration.cameraCount + "（档内有效 " + calibration.cameraValid
                          + "，识别剔除 " + calibration.cameraRejected + "） · USB 帧 " + calibration.armCount
                          + "（测量位有效 " + calibration.armValid + "）"; color: Palette.secondary }
            FitLivePlot {
                Layout.fillWidth: true
                report: calibration.candidate.status !== undefined ? calibration.candidate : calibration.liveFit
            }
            Label { text: calibration.status; color: calibration.status.indexOf("不完整") >= 0 ? Palette.red : Palette.blue; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: "会话：" + (calibration.sessionPath || "尚未建立"); color: Palette.secondary; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            AppButton { text: "导出会话证据"; enabled: !calibration.running && calibration.sessionPath.length > 0
                onClicked: exportDialog.open() }
            Rectangle { Layout.fillWidth: true; height: 1; color: Palette.border }
            FitProcessPanel {
                Layout.fillWidth: true
                report: calibration.candidate.status !== undefined ? calibration.candidate : calibration.liveFit
                progress: calibration.fitProgress
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Palette.border }
            Label { text: "拟合结果"; font.pixelSize: 18; font.bold: true; color: Palette.text }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                text: calibration.candidate.status === "candidate"
                    ? "共享 Kmat = " + calibration.candidate.kmat_unified + "；共同拟合截距 b = " + calibration.candidate.b_shared_ns
                      + " ns（保留为证据，不在相对 Δt 上再次扣除）"
                      + "；分段校正点 " + ((calibration.candidate.force_correction || {}).knot_count || 0)
                      + "；通过档数 " + calibration.candidate.accepted_stages + "；配对 " + calibration.candidate.accepted_pairs
                      + "。仅供候选审查，未经计量批准。"
                    : (calibration.candidate.reason || "结束采集后显示候选及筛查原因。")
            }
            Label {
                visible: calibration.candidate.status === "candidate"
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                text: "参数标准不确定度：" + JSON.stringify(calibration.candidate.standard_uncertainty)
                    + "；逐杆残差：" + JSON.stringify(calibration.candidate.residuals_by_rod)
            }
            Label {
                visible: calibration.candidate.status === "candidate"
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                text: "载荷范围 (kN)：" + JSON.stringify(calibration.candidate.load_range_kN_by_rod)
                    + "；逐档留一验证：" + JSON.stringify(calibration.candidate.independent_validation)
                    + "；时间配对差：" + JSON.stringify(calibration.candidate.pair_gap_ms)
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Palette.border }
            Label { text: "ARM 当前参数回读"; font.pixelSize: 18; font.bold: true; color: Palette.text }
            RowLayout {
                AppButton { objectName: "calibrationReadbackButton"; text: "回读 ARM 当前参数"
                    enabled: backendObject.connected === true && backendObject.offlinePreview !== true
                        && root.readbackState.busy !== true
                    onClicked: root.backendObject.refreshCalibrationReadback() }
                Label { objectName: "calibrationReadbackStatus"; Layout.fillWidth: true; wrapMode: Text.Wrap
                    color: root.readbackState.outcome === "failed" ? Palette.red : Palette.secondary
                    text: String(root.readbackState.message || "点击回读，核对 ARM 当前运行与开机保存值。")
                        + (root.readbackState.readUtc ? " · " + root.readbackState.readUtc : "") }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                text: "以下来自 ARM USB 回读。拟合截距 b 只保留在本地标定证据中；下发 Kmat 和分段校正时保持四路 coupling_bias_ns。" }
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
                    Label { text: "当前运行"; font.bold: true; color: Palette.text }
                    Label { objectName: "calibrationActiveValues"; Layout.fillWidth: true; wrapMode: Text.Wrap
                        color: Palette.text
                        text: root.readbackAvailable ? root.parameterSummary(root.readbackState.active_document)
                            : "未连接或本次读取失败；不将本地候选显示为当前参数。" }
                    Label { objectName: "calibrationActiveIdentity"; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                        color: Palette.secondary
                        text: root.readbackAvailable ? "配置 SHA：" + String((root.readbackState.active_identity || {}).identitySha256 || "--") : "" }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
                    Label { text: "开机保存"; font.bold: true; color: Palette.text }
                    Label { objectName: "calibrationStartupValues"; Layout.fillWidth: true; wrapMode: Text.Wrap
                        color: Palette.text
                        text: root.readbackAvailable ? root.parameterSummary(root.readbackState.startup_document)
                            : "尚未取得有效 ARM 回读。" }
                    Label { objectName: "calibrationStartupIdentity"; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                        color: Palette.secondary
                        text: root.readbackAvailable ? "配置 SHA：" + String((root.readbackState.startup_identity || {}).identitySha256 || "--") : "" }
                }
            }
            Label { objectName: "calibrationParametersMatch"; visible: root.readbackAvailable
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                text: root.readbackState.parametersMatch === true
                    ? "当前运行与开机保存的标定参数一致。"
                    : "当前运行与开机保存的标定参数不同；请核对是否尚未保存开机配置。" }
            Rectangle { Layout.fillWidth: true; height: 1; color: Palette.border }
            Label { text: "确认与下发"; font.pixelSize: 18; font.bold: true; color: Palette.text }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                text: !root.candidateReady ? "尚无可下发的拟合候选。"
                    : !root.identityMatches && root.appliedSessionId !== root.candidate.session_id
                        ? "ARM 当前配置身份与拟合时不同；请重新核对，旧候选不能下发。"
                        : "确认只接受本次候选，不代表计量批准。ARM 写入仍要求工程师登录、主机托管和硬件安全停止。" }
            RowLayout {
                AppButton { objectName: "calibrationAcceptButton"; text: "确认拟合候选"
                    enabled: root.candidateReady && !calibration.running && root.acceptedSessionId !== root.candidate.session_id
                    onClicked: candidateConfirmation.open() }
                AppButton { objectName: "calibrationValidateButton"; text: "请 ARM 校验"
                    enabled: root.canSubmit && root.identityMatches && root.capabilities.modelValidate === true
                        && root.acceptedSessionId === root.candidate.session_id
                        && root.validatedSessionId !== root.candidate.session_id
                    onClicked: { root.requestedOperation = 3; writeConfirmation.open() } }
                AppButton { objectName: "calibrationApplyButton"; text: "应用到当前运行"
                    enabled: root.canSubmit && root.identityMatches && root.capabilities.modelApply === true
                        && root.validatedSessionId === root.candidate.session_id
                        && root.appliedSessionId !== root.candidate.session_id
                    onClicked: { root.requestedOperation = 1; writeConfirmation.open() } }
                AppButton { objectName: "calibrationSaveButton"; text: "保存为开机配置"
                    enabled: root.canSubmit && root.capabilities.modelSave === true
                        && root.appliedSessionId === root.candidate.session_id && root.currentHasCandidate()
                        && root.savedSessionId !== root.candidate.session_id
                    onClicked: { root.requestedOperation = 2; writeConfirmation.open() } }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                text: root.savedSessionId === root.candidate.session_id ? "ARM 开机配置已回读为本次候选。"
                    : root.appliedSessionId === root.candidate.session_id ? "ARM 当前运行配置已回读为本次候选；开机配置尚未保存。"
                    : root.applyAckSessionId === root.candidate.session_id ? "ARM 已回执应用成功，等待当前配置回读。"
                    : root.validatedSessionId === root.candidate.session_id ? "ARM 已校验候选；尚未应用。"
                    : root.acceptedSessionId === root.candidate.session_id ? "操作者已确认候选；尚未发送 ARM。"
                    : "候选尚未确认。" }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                text: String((backendObject.calibrationOperation || ({})).message || "")
                    + ((backendObject.calibrationOperation || ({})).evidenceWarning
                        ? "；" + backendObject.calibrationOperation.evidenceWarning : "") }
        }
    }

    AppDialog {
        id: candidateConfirmation
        objectName: "calibrationCandidateConfirmation"
        title: "确认本次拟合候选"
        modal: true; anchors.centerIn: parent; width: 560
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (root.candidateReady && calibration.acceptCandidate()) {
            root.acceptedSessionId = root.candidate.session_id
            root.validatedSessionId = ""; root.applyAckSessionId = ""; root.appliedSessionId = ""
            root.saveAckSessionId = ""; root.savedSessionId = ""
        }
        contentItem: Label { width: 520; wrapMode: Text.Wrap
            text: "会话：" + root.candidate.session_id + "\nKmat：" + root.candidate.kmat_unified
                + "\n共同拟合截距 b (ns)：" + root.candidate.b_shared_ns + "（本地拟合证据，不写入 ARM 偏置）"
                + "\n保持四路 coupling_bias_ns：" + JSON.stringify(root.candidate.coupling_bias_ns)
                + "\n共享分段校正点：" + (((root.candidate.force_correction || {}).knot_count) || 0)
                + "\n通过档数：" + root.candidate.accepted_stages
                + "；配对：" + root.candidate.accepted_pairs
                + "\n请先查看上方残差、不确定度、载荷范围和逐档验证。确认只接受候选，不执行设备写入，也不代表计量批准。" }
    }
    AppDialog {
        id: writeConfirmation
        objectName: "calibrationWriteConfirmation"
        title: root.requestedOperation === 3 ? "确认请 ARM 校验" : root.requestedOperation === 1
            ? "确认应用到当前运行" : "确认保存为开机配置"
        modal: true; anchors.centerIn: parent; width: 560
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            let op = root.requestedOperation
            if (!root.canSubmit || root.acceptedSessionId !== root.candidate.session_id) return
            if (op === 3 && (!root.identityMatches || root.capabilities.modelValidate !== true)) return
            if (op === 1 && (!root.identityMatches || root.validatedSessionId !== root.candidate.session_id
                             || root.capabilities.modelApply !== true)) return
            if (op === 2 && (root.appliedSessionId !== root.candidate.session_id || !root.currentHasCandidate()
                             || root.capabilities.modelSave !== true)) return
            root.pendingOperation = op
            root.backendObject.submitCalibrationCandidate(op, true)
        }
        contentItem: Label { width: 520; wrapMode: Text.Wrap
            text: root.requestedOperation === 3
                ? "ARM 将校验本次候选参数与完整型号文档；不会改变运行配置。"
                : root.requestedOperation === 1
                    ? "将把本次共享 Kmat 和分段校正应用到当前运行配置，保持四路 coupling_bias_ns。拟合 b 不写入 ARM 偏置。配置身份变化、设备未安全停止或能力不足时禁止提交。应用结果须等待 ARM 回执和配置回读。"
                    : "将把 ARM 当前已生效的配置写为开机配置。此操作与运行应用分开确认，成功须等待 ARM 回执和开机配置回读。" }
    }

    FolderDialog {
        id: exportDialog
        title: "选择标定会话导出目录"
        onAccepted: calibration.exportSession(selectedFolder)
    }

    FileDialog {
        id: manualPhotoDialog
        title: "选择本档现场照片"
        nameFilters: ["图像文件 (*.png *.jpg *.jpeg *.bmp)", "所有文件 (*)"]
        onAccepted: root.manualPhoto = selectedFile
    }

    AppDialog {
        id: roiDialog
        title: "六个 ROI 与 S1–S4 核对"
        modal: true
        width: Math.min(root.width - 32, 1120)
        height: Math.min(root.height - 24, 690)
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        property bool roiWasConfirmed: false
        onOpened: roiWasConfirmed = false
        onClosed: { if (!roiWasConfirmed) calibration.cancelRoi() }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label { text: "拖动大图重画选中的框；可直接沿用上次确认的框。识别错误时请重画或重拍。"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: root.roiNames
                    AppButton { required property string modelData; text: modelData; checkable: true; checked: root.selectedRoi === modelData
                        onClicked: root.selectedRoi = modelData }
                }
                AppButton { text: "重拍"; enabled: !calibration.busy
                    onClicked: { root.recognizedRoisJson = ""; calibration.capturePreview() } }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.preferredWidth: roiDialog.width - roiDialog.leftPadding - roiDialog.rightPadding
                Rectangle {
                    id: canvas
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.preferredWidth: Math.max(320, roiDialog.width - 260)
                    color: "#172333"; radius: 6
                    Image {
                        id: preview
                        anchors.fill: parent; source: calibration.previewUrl
                        fillMode: Image.PreserveAspectFit
                        cache: false
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: calibration.imageWidth > 0 && preview.status !== Image.Ready
                        text: preview.status === Image.Error ? "预览图加载失败，请重拍" : "正在加载预览图…"
                        color: "white"
                    }
                    Item {
                        id: imageSurface
                        x: (canvas.width - preview.paintedWidth) / 2
                        y: (canvas.height - preview.paintedHeight) / 2
                        width: preview.paintedWidth; height: preview.paintedHeight
                        Repeater {
                            model: root.roiNames
                            Rectangle {
                                required property string modelData
                                readonly property var box: root.workingRois[modelData] || [0, 0, 0, 0]
                                x: box[0] * imageSurface.width / Math.max(1, calibration.imageWidth)
                                y: box[1] * imageSurface.height / Math.max(1, calibration.imageHeight)
                                width: box[2] * imageSurface.width / Math.max(1, calibration.imageWidth)
                                height: box[3] * imageSurface.height / Math.max(1, calibration.imageHeight)
                                color: "transparent"; border.width: modelData === root.selectedRoi ? 3 : 2
                                border.color: modelData === root.selectedRoi ? "#F6B72B" : "#32CEAA"
                                Text { text: modelData; color: "white"; font.bold: true; style: Text.Outline; styleColor: "black" }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            property real startX: 0
                            property real startY: 0
                            onPressed: { startX = mouse.x; startY = mouse.y }
                            onReleased: {
                                if (Math.abs(mouse.x - startX) < 3 || Math.abs(mouse.y - startY) < 3) return
                                let sx = calibration.imageWidth / Math.max(1, imageSurface.width)
                                let sy = calibration.imageHeight / Math.max(1, imageSurface.height)
                                let x1 = Math.max(0, Math.floor(Math.min(startX, mouse.x) * sx))
                                let y1 = Math.max(0, Math.floor(Math.min(startY, mouse.y) * sy))
                                let x2 = Math.min(calibration.imageWidth, Math.ceil(Math.max(startX, mouse.x) * sx))
                                let y2 = Math.min(calibration.imageHeight, Math.ceil(Math.max(startY, mouse.y) * sy))
                                let next = Object.assign({}, root.workingRois)
                                next[root.selectedRoi] = [x1, y1, x2 - x1, y2 - y1]
                                root.workingRois = next
                                root.recognizedRoisJson = ""
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: 220; Layout.fillHeight: true
                    Label { text: "识别候选"; font.bold: true }
                    Repeater {
                        model: root.roiNames
                        Label { required property string modelData
                            text: modelData + "  " + (calibration.recognition.values && calibration.recognition.values[modelData] !== undefined
                                  && calibration.recognition.values[modelData] !== null
                                  ? calibration.recognition.values[modelData] + (modelData === "Other" ? "" : " kN") : "--") }
                    }
                    Label { text: "四杆置信度 " + (calibration.recognition.confidence === undefined ? "--" : (100 * calibration.recognition.confidence).toFixed(1) + "%") }
                    Label { text: "Total 置信度 " + (!calibration.recognition.auxiliary || !calibration.recognition.auxiliary.Total ? "--" : (100 * calibration.recognition.auxiliary.Total.confidence).toFixed(1) + "%") }
                    Label { text: "Other 置信度 " + (!calibration.recognition.auxiliary || !calibration.recognition.auxiliary.Other ? "--" : (100 * calibration.recognition.auxiliary.Other.confidence).toFixed(1) + "%") }
                    Label { text: "Total / Other 仅作画面核对"; color: Palette.secondary }
                    Label { text: calibration.status; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Item { Layout.fillHeight: true }
                    AppButton { text: "拍照识别"; enabled: calibration.imageWidth > 0 && !calibration.busy
                        onClicked: { root.recognizedRoisJson = JSON.stringify(root.workingRois); calibration.recognizeRois(root.workingRois) } }
                    AppButton { text: "确认 ROI 并关闭"; enabled: !calibration.busy && root.recognizedRoisJson === JSON.stringify(root.workingRois)
                            && calibration.recognition.status === "ok" && calibration.recognition.confidence >= 0.9
                        onClicked: { calibration.confirmRois(); if (calibration.roiConfirmed) { roiDialog.roiWasConfirmed = true; roiDialog.close() } } }
                    AppButton { text: "取消"; onClicked: { calibration.cancelRoi(); roiDialog.close() } }
                }
            }
        }
    }
}
