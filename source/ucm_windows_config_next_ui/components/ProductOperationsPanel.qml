import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import "EngineerConfig.js" as Config
import "CustomerSettings.js" as Geometry
import "AppPalette.js" as Palette

ScrollView {
    id: root
    property var backendObject
    property int selectedDomain: 0
    property int requestedOperation: 3
    property url packageFile
    property var draftTexts: ["", ""]
    property bool advancedExpanded: false
    property bool runtimeParametersExpanded: false
    property string localMessage: ""
    readonly property bool engineerLogged: backendObject && backendObject.engineerMode === true
    readonly property bool deviceConnected: backendObject && backendObject.connected === true
    readonly property bool preview: backendObject && backendObject.offlinePreview === true
    readonly property var operation: backendObject && backendObject.productOperation ? backendObject.productOperation : ({})
    readonly property var capabilities: backendObject && backendObject.productCapabilities ? backendObject.productCapabilities : ({})
    readonly property var fields: operation.fields || ({})
    readonly property bool canWrite: !preview && deviceConnected && engineerLogged && backendObject && backendObject.productCanWrite === true
    readonly property var burstControl: backendObject && backendObject.runtimeBurstControl ? backendObject.runtimeBurstControl : ({})
    readonly property var runtimeParameters: backendObject && backendObject.runtimeConfigurationFields ? backendObject.runtimeConfigurationFields : []
    readonly property bool runtimeParametersCanWrite: backendObject && backendObject.runtimeConfigurationCanWrite === true
    readonly property bool runtimeParametersBusy: backendObject && backendObject.runtimeConfigurationBusy === true
    readonly property bool runtimeParametersDirty: backendObject && backendObject.runtimeConfigurationHasChanges === true
    readonly property string runtimeParametersStatus: backendObject ? String(backendObject.runtimeConfigurationStatus || "") : ""
    readonly property var activeDocument: stateDocument("active_document")
    readonly property var startupDocument: stateDocument("startup_document")
    readonly property var draftObject: Config.parse(document.text)
    readonly property bool formContractKnown: Config.knownContract(selectedDomain,draftObject)
    readonly property bool gwFrozenProfile: selectedDomain===0 && draftObject
        && Number(draftObject.device_model_id)===37 && String(draftObject.model_name)==="GW1850R"
        && Number(draftObject.schema_version)===3
        && String(draftObject.geometry_model)==="GW_DRAWING_FE_ENGINEERING_V1"
    readonly property var referenceProjection: gwFrozenProfile
        ? ({valid:true,error:"",description:"GW1850R使用PLC模厚和冻结的图纸/有限元结构公式；结构预览只读。"})
        : selectedDomain===0 && draftObject ? Geometry.bodyReferenceProjection(draftObject, previewMold.text.trim().length ? Number(previewMold.text) : undefined) : ({valid:false,error:"尚未载入型号草稿"})
    readonly property var referenceAdmission: gwFrozenProfile
        ? ({valid:true,error:""})
        : selectedDomain===0 && draftObject ? Geometry.bodyReferenceProjection(draftObject) : ({valid:false,error:"尚未载入型号草稿"})
    readonly property var draftDifferences: Config.diff(selectedDomain,activeDocument,draftObject)
    readonly property var startupDifferences: Config.diff(selectedDomain,startupDocument,activeDocument)
    readonly property int upgradeState: fields.totalBytes !== undefined ? Number(fields.state || 0) : 0
    clip: true
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    property string pendingAuthorityAction: ""
    property int pendingBurstOperation: 0
    property int pendingRuntimeOperation: 0
    function authorityAllowed(action) {
        if(preview || !engineerLogged || !deviceConnected || !backendObject || backendObject.controlModeBusy) return false
        const key=action==="manual" ? "canRequestManualControl" : action==="autonomous" ? "canRequestAutonomousControl" : ""
        return key.length>0 && backendObject[key]===true
    }
    function confirmAuthority(action) {
        if(!authorityAllowed(action)) return
        pendingAuthorityAction=action
        authorityConfirmation.open()
    }
    function dispatchConfirmedAuthority() {
        const action=pendingAuthorityAction
        pendingAuthorityAction=""
        if(!authorityAllowed(action)) return
        if(action==="manual") backendObject.requestManualControl()
        else if(action==="autonomous") backendObject.requestAutonomousControl()
    }
    AppDialog {
        id: authorityConfirmation
        objectName: "engineerAuthorityConfirmation"
        width: 510
        anchors.centerIn: Overlay.overlay
        modal: true
        title: root.pendingAuthorityAction==="manual" ? "确认手动接管设备" : "确认切换为设备自主控制"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.dispatchConfirmedAuthority()
        onRejected: root.pendingAuthorityAction=""
        contentItem: Label { width:460; wrapMode:Text.Wrap; text:"本次操作将请求改变ARM控制权或恢复状态。请确认现场允许操作；设备实时校验通过并回报后才视为成功。配置写入和升级仍需分别确认。" }
    }
    AppDialog {
        id: burstConfirmation
        objectName: "runtimeBurstConfirmation"
        width: 510
        anchors.centerIn: Overlay.overlay
        modal: true
        title: root.pendingBurstOperation===1 ? "确认应用Burst周期" : "确认保存当前运行配置"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            const operation=root.pendingBurstOperation
            root.pendingBurstOperation=0
            if(operation===1) root.backendObject.applyRuntimeBurst(true)
            else if(operation===2) root.backendObject.saveRuntimeBurstStartup(true)
        }
        onRejected: root.pendingBurstOperation=0
        contentItem: Label {
            width:460; wrapMode:Text.Wrap
            text:root.pendingBurstOperation===1
                 ? "将把每帧Burst周期改为 "+String(root.burstControl.draft||1)+"。ARM必须处于HOST_MANAGED、硬件安全停止且明确卸载；只有配置回读和硬件实际值一致才视为成功。"
                 : "只保存ARM当前已经应用并回读一致的完整运行配置到A/B启动槽，不保存尚未应用的本地候选。"
        }
    }
    AppDialog {
        id: runtimeParametersConfirmation
        objectName: "runtimeParametersConfirmation"
        width: 530
        anchors.centerIn: Overlay.overlay
        modal: true
        title: root.pendingRuntimeOperation===1
             ? "确认应用运行参数" : "确认保存当前运行参数"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            const operation=root.pendingRuntimeOperation
            root.pendingRuntimeOperation=0
            if(operation===1) root.backendObject.applyRuntimeConfiguration(true)
            else if(operation===2) root.backendObject.saveRuntimeConfigurationStartup(true)
        }
        onRejected: root.pendingRuntimeOperation=0
        contentItem: Label {
            width:480; wrapMode:Text.Wrap
            text:root.pendingRuntimeOperation===1
                 ? "把本地候选作为一个CFG2事务提交。ARM必须保持HOST_MANAGED、租约有效、硬件安全停止且明确卸载；LNA/PGA/VCNTL/HV等控制量还要与UHW2硬件实际值逐项一致。"
                 : "只保存已经应用并回读一致的ARM活动配置到A/B启动槽；尚未应用的本地候选不会被保存。"
        }
    }
    function stateDocument(which) {
        if(!deviceConnected || !backendObject.productState) return null
        const state=backendObject.productState[selectedDomain===0 ? "deviceModel" : "inputPolicy"]
        return state && state.available && state[which] && Object.keys(state[which]).length ? state[which] : null
    }
    function rememberDraft() {
        const next=draftTexts.slice(); next[selectedDomain]=document.text; draftTexts=next
    }
    function switchDomain(value) {
        if(value===selectedDomain) return
        rememberDraft(); selectedDomain=value; document.text=draftTexts[value]; localMessage=""
    }
    function loadCurrent() {
        if(!activeDocument) { localMessage="尚未读取当前文档，未生成草稿。"; return }
        document.text=JSON.stringify(activeDocument,null,2)
        localMessage="已把ARM当前文档载入本地草稿；设备状态刷新不会覆盖修改。"
    }
    function editField(key,value) {
        const result=Config.edit(document.text,selectedDomain,key,value)
        if(result.ok) { document.text=result.text; localMessage="修改仅保留在本地草稿，尚未应用。" }
        else localMessage=result.error
    }
    function fieldEditable(key) {
        if(!formContractKnown || !draftObject || draftObject[key]===undefined || operation.busy) return false
        if(!gwFrozenProfile) return true
        return ["kmat_unified","coupling_bias_ns","force_correction_knot_count",
                "force_correction_input_n","force_correction_output_n"].indexOf(key)>=0
    }
    function operationAllowed(op) {
        const key=(selectedDomain===0 ? "model" : "policy")+(op===1 ? "Apply" : op===2 ? "Save" : "Validate")
        return canWrite && Boolean(capabilities[key]) && (op===2 ? Config.knownContract(selectedDomain,activeDocument) && (selectedDomain===1 || Geometry.bodyReferenceProjection(activeDocument).valid) : formContractKnown && (selectedDomain===1 || referenceAdmission.valid))
    }
    function safeBackendText(key,fallback) {
        return backendObject && backendObject[key] !== undefined && String(backendObject[key]).length ? String(backendObject[key]) : fallback
    }
    AppDialog {
        id: reloadConfirmation
        width: 510
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "用ARM当前配置替换本域草稿？"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.loadCurrent()
        contentItem: Label { width:440; wrapMode:Text.Wrap; text:"只替换当前选中域的本地草稿。另一域的草稿不变；不会向设备写入。" }
    }
    FileDialog {
        id: packageDialog
        title: "选择ARM升级包"
        fileMode: FileDialog.OpenFile
        nameFilters: ["UCM2 R2S 升级包 (*.u2p)"]
        onAccepted: root.packageFile = selectedFile
    }
    AppDialog {
        id: configConfirmation
        width: 510
        anchors.centerIn: Overlay.overlay
        modal: true
        title: root.requestedOperation === 1 ? "确认应用当前草稿" : root.requestedOperation === 2 ? "确认保存当前配置为开机配置" : "确认由ARM校验草稿"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if(root.engineerLogged) root.backendObject.submitProductDocument(root.selectedDomain, root.requestedOperation, document.text, true)
        contentItem: Label {
            width: 460
            wrapMode: Text.Wrap
            text: root.requestedOperation === 2
                ? "保存ARM当前已生效配置为开机配置，不保存尚未应用的本地草稿。持久化成功以ARM回执为准。"
                : "将向ARM发送表单对应的完整草稿。应用会改变设备当前配置；只有ARM终态回执确认后才显示成功。"
        }
    }
    AppDialog {
        id: stageConfirmation
        width: 510
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "确认传输并暂存升级包"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if(root.engineerLogged) root.backendObject.stageProductUpgrade(root.packageFile, Number(version.text), model.text, build.text, true)
        contentItem: Label { width: 460; wrapMode: Text.Wrap; text: "包：" + root.packageFile + "\n型号：" + model.text + " · 版本：" + version.text + "\n本步只传输、校验并暂存，不自动激活。" }
    }
    AppDialog {
        id: abortConfirmation
        width: 510
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "确认中止未激活升级"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if(root.engineerLogged) root.backendObject.abortProductUpgrade(true)
        contentItem: Label { width: 430; wrapMode: Text.Wrap; text: "请求ARM中止接收、校验或暂存中的升级。激活中和已激活状态不允许中止。" }
    }
    AppDialog {
        id: activationConfirmation
        width: 510
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "确认激活已校验升级包"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if(root.engineerLogged) root.backendObject.activateProductUpgrade(true)
        contentItem: Label { width: 460; wrapMode: Text.Wrap; text: "将激活ARM已验证的暂存包，可能重启设备。只有设备回报完成才视为成功；断连不会自动重发。" }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label { text:"产品配置与升级"; font.pixelSize:26; font.weight:Font.DemiBold; color:Palette.text }
                Label { text:"先核对权限与当前配置，再编辑、比较并提交"; font.pixelSize:12; color:"#7A8AA0" }
            }
            Label { text:root.preview ? "离线工程预览" : root.deviceConnected ? "设备在线" : "设备离线"; color:root.deviceConnected ? "#168975" : Palette.tertiary; font.pixelSize:12 }
        }
        GridLayout {
            Layout.fillWidth:true
            columns: root.availableWidth>780 ? 3 : 1
            columnSpacing:12
            rowSpacing:10
            Repeater {
                model:[
                    {title:"01  工程师登录",value:root.engineerLogged ? "已登录" : "未登录",note:root.preview ? "离线预览不授予工程写入权限" : "界面身份与设备授权独立",ready:root.engineerLogged},
                    {title:"02  ARM控制权",value:root.deviceConnected && root.backendObject.controlAuthorityAvailable ? root.safeBackendText("deviceControlModeText","未知") : "未确认",note:root.deviceConnected ? root.safeBackendText("deviceControlPhaseText","控制阶段未知") : "连接设备后读取；不以本机登录代替",ready:root.deviceConnected && root.backendObject.controlAuthorityAvailable===true},
                    {title:"03  当前允许操作",value:root.canWrite ? "按设备能力开放" : "只读与本地编辑",note:Config.writeStatus(root.preview,root.engineerLogged,root.deviceConnected,root.capabilities,root.operation,root.canWrite),ready:root.canWrite}
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth:true; implicitHeight:108; radius:9; color:modelData.ready ? "#EDF5FF" : Palette.canvas; border.color:"#DFE8F2"
                    ColumnLayout {
                        anchors.fill:parent; anchors.margins:14; spacing:5
                        Label { text:modelData.title; color:"#718198"; font.pixelSize:11 }
                        Label { text:modelData.value; color:modelData.ready ? Palette.blue : Palette.secondary; font.pixelSize:17; font.weight:Font.DemiBold }
                        Label { text:modelData.note; color:Palette.tertiary; font.pixelSize:11; Layout.fillWidth:true; wrapMode:Text.Wrap }
                    }
                }
            }
        }
        EngineerSection {
            Layout.fillWidth:true
            title:"设备控制权"
            description:"手动接管和切回自主均需明确确认；配置与升级要求ARM确认主机托管且硬件安全停止。"
            Flow {
                Layout.fillWidth:true; spacing:10
                AppButton { objectName:"engineerManualControlButton"; text:"手动接管"; enabled:root.authorityAllowed("manual"); onClicked:root.confirmAuthority("manual") }
                AppButton { objectName:"engineerAutonomousControlButton"; text:"切换自主控制"; enabled:root.authorityAllowed("autonomous"); onClicked:root.confirmAuthority("autonomous") }
            }
            Label { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Palette.secondary; font.pixelSize:12; text:root.preview ? "离线预览不发送控制请求" : !root.engineerLogged ? "请先登录工程师" : !root.deviceConnected ? "设备未连接，等待连接恢复" : root.safeBackendText("controlAuthorityStatus","等待ARM控制权状态") }
            Label { objectName:"productBlockReason"; Layout.fillWidth:true; wrapMode:Text.Wrap; color:"#8A6A36"; font.pixelSize:12; visible:String(root.capabilities.blockReason||"").length>0; text:String(root.capabilities.blockReason||"") }
        }
        EngineerSection {
            Layout.fillWidth:true
            title:"每帧 Burst 周期"
            description:"field 98可配置1–8周期；field 133显示ARM硬件实际值。自主模式保持只读。"
            RowLayout {
                Layout.fillWidth:true; spacing:12
                Label { text:"本地候选"; color:Palette.secondary; font.pixelSize:12 }
                SpinBox {
                    id:burstCycles
                    objectName:"runtimeBurstCycles"
                    from:1; to:8; stepSize:1; editable:true
                    value:Number(root.burstControl.draft||1)
                    enabled:root.burstControl.canWrite===true && root.burstControl.busy!==true
                    onValueModified:root.backendObject.setRuntimeBurstDraft(value)
                }
                Label {
                    text:root.burstControl.valid===true
                         ? "ARM配置 "+String(root.burstControl.configured)+" · 硬件实际 "+String(root.burstControl.actual)
                           +" · AGC "+String(root.burstControl.agcStatus||"")
                         : "ARM实际值尚未读取"
                    color:root.burstControl.valid===true ? Palette.blue : Palette.tertiary
                    font.pixelSize:12
                }
                Item { Layout.fillWidth:true }
                AppButton {
                    objectName:"runtimeBurstApplyButton"
                    text:root.burstControl.busy===true ? "正在回读…" : "应用到当前运行"
                    enabled:root.burstControl.canWrite===true && root.burstControl.dirty===true
                    onClicked:{root.pendingBurstOperation=1;burstConfirmation.open()}
                }
                AppButton {
                    objectName:"runtimeBurstSaveButton"
                    text:"保存当前为开机配置"
                    enabled:root.burstControl.canWrite===true && root.burstControl.valid===true
                            && root.burstControl.dirty!==true
                    onClicked:{root.pendingBurstOperation=2;burstConfirmation.open()}
                }
            }
            Label {
                Layout.fillWidth:true; wrapMode:Text.Wrap; font.pixelSize:12
                color:root.burstControl.canWrite===true ? Palette.blue : "#8A6A36"
                text:String(root.burstControl.status||"等待读取ARM当前burst配置")
            }
        }
        EngineerSection {
            Layout.fillWidth:true
            title:"revision 9 运行参数目录"
            description:"LNA、PGA、VCNTL、标称HV、AGC范围和算法门限使用同一个CFG2事务；数字增益与数字TGC固定0，只读诊断。"
            RowLayout {
                Layout.fillWidth:true; spacing:10
                AppButton {
                    objectName:"runtimeParametersExpandButton"
                    text:root.runtimeParametersExpanded ? "收起运行参数" : "展开运行参数 "+String(root.runtimeParameters.length)+" 项"
                    onClicked:root.runtimeParametersExpanded=!root.runtimeParametersExpanded
                }
                Label {
                    Layout.fillWidth:true; wrapMode:Text.Wrap; font.pixelSize:11
                    color:root.runtimeParametersCanWrite ? Palette.blue : "#8A6A36"
                    text:root.runtimeParametersStatus.length ? root.runtimeParametersStatus
                         : "等待ARM运行配置与安全状态"
                }
            }
            GridLayout {
                Layout.fillWidth:true
                visible:root.runtimeParametersExpanded
                columns:root.availableWidth>900 ? 2 : 1
                columnSpacing:12; rowSpacing:9
                Repeater {
                    model:root.runtimeParameters
                    delegate:Rectangle {
                        required property var modelData
                        Layout.fillWidth:true; implicitHeight:92; radius:8
                        color:"#F7FAFE"; border.color:modelData.dirty ? "#78A9DE" : "#DFE8F2"
                        ColumnLayout {
                            anchors.fill:parent; anchors.margins:10; spacing:4
                            RowLayout {
                                Layout.fillWidth:true
                                Label { Layout.fillWidth:true; text:modelData.label; color:Palette.secondary; font.pixelSize:12; font.weight:Font.DemiBold }
                                Label { text:"#"+String(modelData.fieldId)+" · "+String(modelData.group); color:Palette.tertiary; font.pixelSize:9 }
                            }
                            RowLayout {
                                Layout.fillWidth:true; spacing:8
                                Label { text:"当前 "+String(modelData.current); color:Palette.tertiary; font.pixelSize:10 }
                                ComboBox {
                                    Layout.fillWidth:true
                                    visible:modelData.allowedValues
                                            && modelData.allowedValues.length>0
                                    model:modelData.allowedValues || []
                                    currentIndex:model.indexOf(Number(modelData.draft))
                                    enabled:modelData.canWrite===true
                                            && !root.runtimeParametersBusy
                                    onActivated:root.backendObject.setRuntimeConfigurationField(
                                        Number(modelData.fieldId),
                                        Number(modelData.elementIndex),
                                        Number(currentValue))
                                }
                                SpinBox {
                                    Layout.fillWidth:true
                                    visible:!modelData.allowedValues
                                        || modelData.allowedValues.length===0
                                    from:Number(modelData.minimum)
                                    to:Number(modelData.maximum)
                                    stepSize:Math.max(1,Number(modelData.step))
                                    editable:true
                                    value:Number(modelData.draft)
                                    enabled:modelData.canWrite===true && !root.runtimeParametersBusy
                                    onValueModified:root.backendObject.setRuntimeConfigurationField(
                                        Number(modelData.fieldId), Number(modelData.elementIndex), value)
                                }
                                Label { text:modelData.dirty ? "待应用" : "一致"; color:modelData.dirty ? Palette.blue : Palette.tertiary; font.pixelSize:10 }
                            }
                            Label { Layout.fillWidth:true; text:String(modelData.constraint||""); elide:Text.ElideRight; color:Palette.tertiary; font.pixelSize:9 }
                        }
                    }
                }
            }
            Flow {
                Layout.fillWidth:true; spacing:10
                AppButton {
                    objectName:"runtimeParametersResetButton"
                    text:"放弃本地候选"
                    enabled:root.runtimeParametersDirty && !root.runtimeParametersBusy
                    onClicked:root.backendObject.resetRuntimeConfigurationDraft()
                }
                AppButton {
                    objectName:"runtimeParametersApplyButton"
                    text:root.runtimeParametersBusy ? "正在回读…" : "应用全部候选"
                    enabled:root.runtimeParametersCanWrite && root.runtimeParametersDirty && !root.runtimeParametersBusy
                    onClicked:{root.pendingRuntimeOperation=1;runtimeParametersConfirmation.open()}
                }
                AppButton {
                    objectName:"runtimeParametersSaveButton"
                    text:"保存当前为开机配置"
                    enabled:root.runtimeParametersCanWrite && !root.runtimeParametersDirty && !root.runtimeParametersBusy
                    onClicked:{root.pendingRuntimeOperation=2;runtimeParametersConfirmation.open()}
                }
            }
        }
        EngineerSection {
            Layout.fillWidth:true
            title:"配置草稿"
            description:"型号与输入策略分别保存本地草稿。只有明确点击载入，才会复制ARM当前文档。"
            RowLayout {
                Layout.fillWidth:true
                Repeater {
                    model:["设备型号", "输入策略"]
                    delegate: AppButton {
                        required property int index
                        required property string modelData
                        text:modelData; checkable:true; checked:root.selectedDomain===index
                        onClicked:root.switchDomain(index)
                    }
                }
                Item { Layout.fillWidth:true }
                AppButton {
                    text:"载入ARM当前配置"
                    enabled:root.activeDocument!==null && !root.operation.busy
                    onClicked:document.text.trim().length ? reloadConfirmation.open() : root.loadCurrent()
                }
            }
            Label {
                Layout.fillWidth:true; wrapMode:Text.Wrap; font.pixelSize:12; color:"#8A6A36"
                text:!root.draftObject ? "尚未载入草稿。表单不使用示例值冒充设备配置。" : !root.formContractKnown ? "当前文档版本尚未接入表单合同，暂不开放编辑与提交。" : "正在编辑本地草稿；应用与保存为不同操作。"
            }
            Label { visible:root.localMessage.length>0; text:root.localMessage; Layout.fillWidth:true; wrapMode:Text.Wrap; color:Palette.blue; font.pixelSize:12 }
        }
        Repeater {
            model:Config.groups(root.selectedDomain)
            delegate: EngineerSection {
                required property var modelData
                Layout.fillWidth:true
                title:modelData.title
                description:modelData.note
                GridLayout {
                    Layout.fillWidth:true
                    columns:root.availableWidth>820 ? 2 : 1
                    columnSpacing:24; rowSpacing:14
                    Repeater {
                        model:modelData.fields
                        delegate: EngineerField {
                            required property var modelData
                            Layout.fillWidth:true
                            descriptor:modelData
                            value:root.draftObject ? root.draftObject[modelData.key] : undefined
                            editable:root.fieldEditable(modelData.key)
                            onEdited:function(key,value){root.editField(key,value)}
                        }
                    }
                }
            }
        }
        EngineerSection {
            Layout.fillWidth:true
            visible:root.selectedDomain===0
            title:"几何与现场坐标"
            description:"BODY_REFERENCE_V1 · 本地参考检查，不是ARM正式输出"
            Label { Layout.fillWidth:true; wrapMode:Text.Wrap; color:"#8A6A36"; font.pixelSize:12; text:"M_ref与C_ref必须来自同一物理状态。C=C_ref+(M−M_ref)，A=总长−B−D−E−C；A不是传感器直接读数。配置数值通过或SHA匹配不代表获得标定资格，应用后正式测量仍可能无效。" }
            Label { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Palette.secondary; font.pixelSize:12; text:"评审候选：M_ref 1075、C_ref 2000、总长2500、B/D各90、E40 mm；四杆直径90 mm、三项面积约6361.7251235 mm²；phi_B/phi_D 0.46，A_ref=280 mm。以下按钮只改本地草稿，根径保留当前ARM回读值；这些候选值尚未标定。" }
            AppButton {
                objectName:"engineerReferenceDefaults"
                text:"填入候选参考值（未标定）"
                enabled:root.selectedDomain===0 && root.formContractKnown && !root.gwFrozenProfile && !root.operation.busy
                onClicked:{const result=Config.referenceDefaults(document.text);if(result.ok){document.text=result.text;root.localMessage="已更新本地候选参考，尚未应用、保存或取得标定资格。"}else root.localMessage=result.error}
            }
            RowLayout {
                Layout.fillWidth:true
                Label { text:"试算模厚 M"; color:Palette.secondary; font.pixelSize:12 }
                AppTextField { id:previewMold; objectName:"engineerPreviewMold"; Layout.preferredWidth:180; placeholderText:"留空使用M_ref"; selectByMouse:true }
                Label { text:"mm · 仅本地试算"; color:Palette.tertiary; font.pixelSize:11 }
            }
            Label {
                objectName:"engineerReferenceProjection"
                Layout.fillWidth:true; wrapMode:Text.Wrap; color:root.referenceProjection.valid ? Palette.blue : "#B44A42"; font.pixelSize:12
                text:root.referenceProjection.valid ? "本地草稿：A_ref="+root.referenceProjection.referenceA+" mm；C="+root.referenceProjection.body+" mm；A="+root.referenceProjection.a+" mm（非ARM正式值）" : root.referenceProjection.error
            }
            Label { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Palette.secondary; font.pixelSize:12; text:"工程师可独立编辑四杆直径、螺纹根径与三项面积。等效面积以mm²计，不自动由杆径换算；修改杆径不会覆盖面积、phi系数或标定资格。" }
            Label { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Palette.tertiary; font.pixelSize:11; text:"schema 1与旧scale/offset/Aoffset不能混入新文档。外部型号文档没有verified开关；独立标定批准不由本界面授予。" }
        }
        EngineerSection {
            Layout.fillWidth:true
            title:"提交前核对差异"
            description:"箭头左侧是比较基准，右侧是目标；型号与输入策略分别比较。"
            Label { text:"当前运行 → 本地草稿"; color:Palette.blue; font.pixelSize:13; font.weight:Font.DemiBold }
            Label { text:!root.activeDocument || !root.draftObject ? "文档尚未齐备，无法比较" : root.draftDifferences.length ? root.draftDifferences.length+" 项差异" : "全部字段一致"; color:Palette.tertiary; font.pixelSize:12 }
            Repeater {
                model:root.draftDifferences
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth:true; implicitHeight:diffBody.implicitHeight+18; color:"#F7FAFE"; radius:6
                    ColumnLayout {
                        id:diffBody; anchors.fill:parent; anchors.margins:9; spacing:4
                        Label { text:modelData.label; color:Palette.secondary; font.pixelSize:12 }
                        Label { text:modelData.before+"  →  "+modelData.after; font.pixelSize:11; color:"#718198"; Layout.fillWidth:true; wrapMode:Text.WrapAnywhere }
                    }
                }
            }
            Label { text:"开机配置 → 当前运行"; color:Palette.blue; font.pixelSize:13; font.weight:Font.DemiBold }
            Label { text:!root.activeDocument || !root.startupDocument ? "当前与开机文档尚未齐备，无法比较" : root.startupDifferences.length ? root.startupDifferences.length+" 项差异" : "全部字段一致"; color:Palette.tertiary; font.pixelSize:12 }
            Repeater {
                model:root.startupDifferences
                delegate: Label { required property var modelData; text:modelData.label+"："+modelData.before+" → "+modelData.after; Layout.fillWidth:true; wrapMode:Text.WrapAnywhere; color:"#718198"; font.pixelSize:11 }
            }
            Flow {
                Layout.fillWidth:true; spacing:10
                Repeater {
                    model:[{label:"请ARM校验草稿",op:3},{label:"应用到当前运行",op:1},{label:"保存当前为开机配置",op:2}]
                    delegate: AppButton {
                        required property var modelData
                        text:modelData.label
                        enabled:root.operationAllowed(modelData.op)
                        onClicked:{root.requestedOperation=modelData.op;configConfirmation.open()}
                    }
                }
            }
            Label { text:"保存操作写入ARM当前已生效配置，不会把尚未应用的草稿直接写为开机配置。"; color:Palette.tertiary; font.pixelSize:11; Layout.fillWidth:true; wrapMode:Text.Wrap }
        }
        EngineerSection {
            Layout.fillWidth:true
            title:"固件升级"
            description:"传输、暂存和激活分别确认；需ARM确认主机托管且硬件安全停止，选择文件不会自动传输。"
            Label { objectName:"upgradeBlockReason"; Layout.fillWidth:true; wrapMode:Text.Wrap; color:"#8A6A36"; font.pixelSize:12; visible:String(root.capabilities.upgradeBlockReason||"").length>0; text:String(root.capabilities.upgradeBlockReason||"") }
            GridLayout {
                Layout.fillWidth:true; columns:root.availableWidth>820 ? 3 : 1; columnSpacing:10; rowSpacing:8
                Repeater {
                    model:Config.upgradeSteps(root.operation,root.upgradeState,String(root.packageFile).length>0)
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth:true; implicitHeight:76; radius:7; color:"#F1F6FD"
                        ColumnLayout { anchors.fill:parent; anchors.margins:12; spacing:5
                            Label { text:modelData.title; color:Palette.blue; font.pixelSize:13; font.weight:Font.DemiBold }
                            Label { text:modelData.note; Layout.fillWidth:true; wrapMode:Text.Wrap; color:"#7A8AA0"; font.pixelSize:11 }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth:true
                AppButton { text:"选择升级包"; enabled:!root.operation.busy; onClicked:packageDialog.open() }
                Label { text:String(root.packageFile)||"尚未选择文件"; Layout.fillWidth:true; elide:Text.ElideMiddle; color:Palette.secondary; font.pixelSize:12 }
            }
            GridLayout {
                Layout.fillWidth:true; columns:root.availableWidth>820 ? 3 : 1; columnSpacing:12; rowSpacing:8
                ColumnLayout { Layout.fillWidth:true; Label { text:"包版本"; color:Palette.secondary; font.pixelSize:12 } AppTextField { id:version; Layout.fillWidth:true; placeholderText:"包内声明的正整数版本"; validator:IntValidator { bottom:1 } } }
                ColumnLayout { Layout.fillWidth:true; Label { text:"目标型号"; color:Palette.secondary; font.pixelSize:12 } AppTextField { id:model; Layout.fillWidth:true; placeholderText:"包的目标型号" } }
                ColumnLayout { Layout.fillWidth:true; Label { text:"包构建标识"; color:Palette.secondary; font.pixelSize:12 } AppTextField { id:build; Layout.fillWidth:true; placeholderText:"包内Build ID" } }
            }
            Flow {
                Layout.fillWidth:true; spacing:10
                AppButton { objectName:"upgradeStageButton"; text:"传输并暂存"; enabled:root.engineerLogged && root.canWrite && root.capabilities.upgradeStage===true && String(root.packageFile).length>0 && version.acceptableInput && model.text.trim().length>0 && build.text.trim().length>0; onClicked:stageConfirmation.open() }
                AppButton { text:"查询设备事务"; enabled:root.deviceConnected && !root.operation.busy; onClicked:root.backendObject.queryProductOperation() }
                AppButton { text:"中止未激活升级"; enabled:!root.preview && root.engineerLogged && root.deviceConnected && root.capabilities.commissioned===true && root.upgradeState>=1 && root.upgradeState<=3; onClicked:abortConfirmation.open() }
                AppButton { objectName:"upgradeActivateButton"; text:"激活已暂存包"; enabled:root.engineerLogged && root.canWrite && root.capabilities.upgradeActivate===true && root.upgradeState===3; onClicked:activationConfirmation.open() }
            }
            ProgressBar { Layout.fillWidth:true; visible:root.fields.totalBytes!==undefined; from:0; to:Math.max(1,Number(root.fields.totalBytes||0)); value:Number(root.fields.receivedBytes||0) }
            Label { visible:root.fields.totalBytes!==undefined; text:String(root.fields.receivedBytes||"0")+" / "+String(root.fields.totalBytes||"0")+" 字节"; color:Palette.secondary; font.pixelSize:11 }
            Label { text:String(root.operation.message||"尚未发起产品事务"); color:root.operation.ok===false ? "#B44A42" : Palette.blue; Layout.fillWidth:true; wrapMode:Text.Wrap; font.pixelSize:12 }
            Label { text:"恢复与回滚尚未提供一键操作。结果未知时保留事务供人工对账，不自动续传、重发或激活。"; color:"#8A6A36"; font.pixelSize:12; Layout.fillWidth:true; wrapMode:Text.Wrap }
        }
        EngineerSection {
            Layout.fillWidth:true
            title:"高级诊断"
            description:"原始文档与事务字段用于核对，不作为普通配置入口。"
            AppButton { text:root.advancedExpanded ? "收起JSON与事务详情" : "展开JSON与事务详情"; onClicked:root.advancedExpanded=!root.advancedExpanded }
            TextArea {
                id:document
                objectName:"engineerProductDraft"
                visible:root.advancedExpanded
                Layout.fillWidth:true; Layout.preferredHeight:240
                text:""; readOnly:true; selectByMouse:true; wrapMode:TextEdit.Wrap; font.family:"Cascadia Mono"; font.pixelSize:11
                placeholderText:"本域尚未载入草稿"
                onTextChanged:root.rememberDraft()
            }
            Label { visible:root.advancedExpanded; text:JSON.stringify(root.fields,null,2); Layout.fillWidth:true; wrapMode:Text.WrapAnywhere; font.pixelSize:11; color:Palette.secondary }
        }
        Item { Layout.preferredHeight:6 }
    }
}
