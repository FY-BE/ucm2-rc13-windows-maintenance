import QtQuick
import QtTest
import "../components"
import "../components/EngineerConfig.js" as ProductStatus

TestCase {
    name: "ProductUi"
    when: windowShown
    width: 1300
    height: 900
    QtObject {
        id: fake
        property bool connected: false
        property bool productCanWrite: false
        property bool engineerMode: false
        property bool offlinePreview: false
        property bool controlModeBusy: false
        property bool canRequestManualControl: false
        property bool canRequestAutonomousControl: false
        property bool canResumeManualControl: false
        property int authorityRequests: 0
        property string lastAuthorityRequest: ""
        function requestManualControl() { authorityRequests++;lastAuthorityRequest="manual" }
        function requestAutonomousControl() { authorityRequests++;lastAuthorityRequest="autonomous" }
        function resumeManualControl() { authorityRequests++;lastAuthorityRequest="resume" }
        property bool telemetryReady: false
        property bool canExportEvidence: true
        property string evidenceStatus: "尚未导出"
        property var rodForceTexts: ["--", "--", "--", "--"]
        property var rodStrainTexts: ["--", "--", "--", "--"]
        property var rodStateTexts: ["无效", "无效", "无效", "无效"]
        property var productState: ({message: "mock state"})
        property var productOperation: ({})
        property var productCapabilities: ({modelApply: false, modelSave: false, commissioned: false, upgradeStage: false, upgradeActivate: false})
        property var algorithmConfigurationFields: []
        property var afeMaintenanceFields: []
        property bool afeMaintenanceCanWrite: false
        property bool afeMaintenanceHasChanges: false
        property bool runtimeConfigurationBusy: false
        property string runtimeConfigurationStatus: "等待安全写门"
        property bool waveformBusy: false
        property bool waveformViewportAvailable: false
        property bool waveformViewportCanRead: false
        property int waveformSliceOffset: 0
        property string waveformViewportStatus: "等待ARM WVS1切片合同"
        property var waveformSeries: []
        property int waveformStart: 9781
        property real waveformSampleRateHz: 50000000
        property string waveformStatus: "等待波形"
        property int waveformReads: 0
        function captureLatestWaveform() { waveformReads++ }
        function shiftWaveformViewport(direction) {}
        function setRuntimeConfigurationField(fieldId,elementIndex,value) {}
        function resetAfeMaintenanceDraft() {}
        function applyAfeMaintenance(authorized) {}
    }
    Component { id: settingsComponent; CustomerSettingsPage { width: 1280; height: 850; backendObject: fake } }
    Component { id: operationsComponent; ProductOperationsPanel { width: 1280; height: 850; backendObject: fake } }
    Component { id: diagnosticsComponent; EngineerDiagnosticsPanel { width: 1280; height: 850; backendObject: fake } }

    function init() {
        fake.connected = false
        fake.controlModeBusy=false
        fake.canRequestManualControl=false
        fake.canRequestAutonomousControl=false
        fake.canResumeManualControl=false
        fake.authorityRequests=0
        fake.lastAuthorityRequest=""
        fake.engineerMode = false
        fake.offlinePreview = false
        fake.telemetryReady = false
        fake.productCanWrite = false
        fake.productState = ({message: "offline"})
        fake.productOperation = ({})
        fake.productCapabilities = ({modelApply:false,modelSave:false,commissioned:false,upgradeStage:false,upgradeActivate:false})
        fake.waveformReads=0
    }

    function bodyDocument() {
        return {schema_version:2,geometry_model:"BODY_REFERENCE_V1",model_name:"TEST-BODY",mold_reference_mm:1075,body_reference_mm:2000,l_total_mm:2500,l_b_mm:90,l_d_mm:90,l_e_mm:40,phi_b:0.5,phi_d:0.5,rod_diameter_mm:[90,90,90,90]}
    }

    function test_authorityRequiresConfirmationAndRechecksPermission() {
        fake.connected=true;fake.engineerMode=true
        fake.canRequestManualControl=true;fake.canRequestAutonomousControl=true
        fake.productCapabilities={blockReason:"控制状态不允许配置",upgradeBlockReason:"等待主机托管安全停机"}
        const page=createTemporaryObject(operationsComponent,this)
        verify(findChild(page,"engineerManualControlButton").enabled)
        verify(findChild(page,"engineerAutonomousControlButton").enabled)
        verify(findChild(page,"engineerResumeControlButton")===null)
        compare(findChild(page,"productBlockReason").text,"控制状态不允许配置")
        compare(findChild(page,"upgradeBlockReason").text,"等待主机托管安全停机")
        page.confirmAuthority("manual")
        compare(fake.authorityRequests,0)
        verify(findChild(page,"engineerAuthorityConfirmation").visible)
        fake.offlinePreview=true
        findChild(page,"engineerAuthorityConfirmation").accept()
        compare(fake.authorityRequests,0)
        verify(!page.authorityAllowed("manual"))
        fake.offlinePreview=false
        page.confirmAuthority("autonomous")
        findChild(page,"engineerAuthorityConfirmation").accept()
        compare(fake.authorityRequests,1);compare(fake.lastAuthorityRequest,"autonomous")
        fake.controlModeBusy=true
        verify(!page.authorityAllowed("manual"))
        fake.controlModeBusy=false
        verify(!page.authorityAllowed("resume"))
        compare(fake.authorityRequests,1)
        fake.engineerMode=false
        fake.engineerMode=true;fake.connected=false
        verify(!page.authorityAllowed("manual"))
    }
    function test_offlineNeverGrantsWriteDespiteBackendFlags() {
        fake.connected=true;fake.engineerMode=true;fake.productCanWrite=true;fake.offlinePreview=true
        fake.productCapabilities={commissioned:true,modelApply:true,modelSave:true,upgradeStage:true,upgradeActivate:true}
        fake.productOperation={fields:{totalBytes:100,state:3}}
        const page=createTemporaryObject(operationsComponent,this)
        verify(!page.canWrite)
        fake.productState={deviceModel:{available:true,active_document:bodyDocument()}}
        const customer=createTemporaryObject(settingsComponent,this)
        verify(!findChild(customer,"customerApplyButton").enabled)
        verify(!findChild(customer,"customerSaveButton").enabled)
        verify(!findChild(page,"upgradeActivateButton").enabled)
        verify(!findChild(page,"upgradeStageButton").enabled)
        verify(ProductStatus.writeStatus(true,true,true,{commissioned:true},{},true).indexOf("离线")>=0)
    }
    function test_monitoringGapAndUpgradeActivationQueryConfirmation() {
        verify(ProductStatus.monitoringStatus(false,true,true).indexOf("50 Hz（20 ms）")>=0)
        verify(ProductStatus.monitoringStatus(false,false,false).indexOf("断点")>=0)
        verify(ProductStatus.monitoringStatus(false,true,false).indexOf("断点")>=0)
        verify(ProductStatus.monitoringStatus(true,true,true).indexOf("未采集")>=0)
        compare(ProductStatus.upgradeSteps({},5,true)[5].note,"尚未确认激活终态")
        compare(ProductStatus.upgradeSteps({phase:"reboot_query"},5,true)[5].note,"正在查询激活终态；若断连，重连后继续")
        compare(ProductStatus.upgradeSteps({phase:"complete"},5,true)[5].note,"激活后查询已确认")
        compare(ProductStatus.upgradeSteps({phase:"checking"},0,true).length,6)
        verify(ProductStatus.writeStatus(false,true,true,{commissioned:true},{outcome:"unresolved"},false).indexOf("禁止新写入")>=0)
    }
    function test_engineerDraftDomainsAndRoleGate() {
        fake.connected = true
        fake.productCanWrite = true
        fake.productState = {deviceModel: {available: true, active_document: bodyDocument()}, inputPolicy: {available: true, active_document: {schema_version: 1, plc_interlock_policy: "ARM_ONLY"}}}
        const page = createTemporaryObject(operationsComponent, this)
        page.loadCurrent()
        page.editField("phi_b", "0.46")
        compare(page.draftObject.phi_b, 0.46)
        verify(!page.canWrite)
        fake.engineerMode = true
        verify(page.canWrite)
        page.switchDomain(1)
        page.loadCurrent()
        page.editField("plc_interlock_policy", "PLC_REQUIRED_INTERLOCK")
        page.switchDomain(0)
        compare(page.draftObject.phi_b, 0.46)
        fake.productState = {deviceModel: {available: true, active_document: {schema_version: 1, phi_b: 0.8}}}
        compare(page.draftObject.phi_b, 0.46)
        page.loadCurrent()
        verify(!page.formContractKnown)
        page.editField("phi_b", "0.6")
        compare(page.draftObject.phi_b, 0.8)
        page.switchDomain(1)
        compare(page.draftObject.plc_interlock_policy, "PLC_REQUIRED_INTERLOCK")
        verify(findChild(page, "engineerProductDraft").readOnly)
    }

    function test_schema2GeometryPreviewAndDefaultsRemainDraft() {
        fake.connected=true
        fake.productState={deviceModel:{available:true,active_document:bodyDocument()}}
        const page=createTemporaryObject(operationsComponent,this)
        page.loadCurrent()
        verify(page.formContractKnown)
        compare(page.referenceProjection.referenceA,280)
        findChild(page,"engineerPreviewMold").text="1175"
        compare(page.referenceProjection.body,2100)
        compare(page.referenceProjection.a,180)
        findChild(page,"engineerPreviewMold").text="2000"
        verify(!page.referenceProjection.valid)
        verify(!page.canWrite)
        page.editField("mold_reference_mm","1075")
        verify(page.draftObject.verified===undefined)
        const mixed=bodyDocument();mixed.mold_to_a_end_scale=1
        fake.productState={deviceModel:{available:true,active_document:mixed}}
        page.loadCurrent()
        verify(!page.formContractKnown)
        verify(!page.operationAllowed(1))
    }

    function test_diagnosticIdentityAndDisconnectClear() {
        const page = createTemporaryObject(diagnosticsComponent, this)
        fake.connected = true
        fake.telemetryReady = true
        fake.productState = {pairedInputStatus: {rawThicknessUm: "9007199254740993", moldValid: 1}}
        compare(page.field("rawThicknessUm", true), "9007199254740993")
        compare(page.field("missing", true), "--")
        compare(page.field("rawThicknessUm", false), "--")
        verify(!findChild(page, "engineerDiagnosticExport").enabled)
        fake.engineerMode = true
        verify(findChild(page, "engineerDiagnosticExport").enabled)
        fake.telemetryReady = false
        compare(page.field("rawThicknessUm", true), "--")
        fake.telemetryReady = true
        fake.connected = false
        verify(!page.paired)
        compare(page.field("rawThicknessUm", true), "--")
    }

    function test_diagnosticWaveformRequiresRealCapabilityForWindowMove() {
        const page = createTemporaryObject(diagnosticsComponent, this)
        fake.connected = true
        fake.engineerMode = true
        verify(findChild(page, "waveformReadLatestButton").enabled)
        verify(!findChild(page, "waveformPreviousButton").enabled)
        verify(!findChild(page, "waveformNextButton").enabled)
        findChild(page, "waveformReadLatestButton").clicked()
        compare(fake.waveformReads, 1)
        fake.waveformViewportAvailable = true
        fake.waveformViewportCanRead = true
        verify(findChild(page, "waveformPreviousButton").enabled)
        verify(findChild(page, "waveformNextButton").enabled)
    }

    function test_customerDraftSurvivesConnectionAndNoPhi() {
        const page = createTemporaryObject(settingsComponent, this)
        verify(page !== null)
        const b = findChild(page, "customerBLength")
        compare(b.text, "90")
        compare(findChild(page, "customerTotalLength").text, "2500")
        b.text = "91"
        fake.connected = true
        fake.productState = {message: "mock updated", deviceModel: {available: true, active_document: bodyDocument()}}
        wait(0)
        compare(b.text, "91")
        verify(!findChild(page, "customerDiameterReadOnly").enabled)
        verify(page.customerDraft().rod_diameter_mm===undefined)
        verify(page.customerDraft().mold_reference_mm===undefined)
        verify(!findChild(page, "customerApplyButton").enabled)
        verify(!findChild(page, "customerSaveButton").enabled)
        verify(findChild(page, "customerValidateButton").enabled)
        verify(page.validateLocally())
        verify(page.statusText.indexOf("尚未提交ARM校验") >= 0)
        verify(!findChild(page, "customerApplyButton").enabled)
        verify(page.draftDifferences.length > 0)
        fake.connected = false
        fake.productState = ({message: "offline"})
        wait(0)
        compare(b.text, "91")
    }

    function test_upgradeCannotStageOrActivateWithoutCommissioning() {
        const page = createTemporaryObject(operationsComponent, this)
        verify(page !== null)
        verify(!findChild(page, "upgradeStageButton").enabled)
        verify(!findChild(page, "upgradeActivateButton").enabled)
        const draft = findChild(page, "engineerProductDraft")
        draft.text = '{"phi_b":0.46}'
        fake.productState = {message: "mock updated", deviceModel: {available: true, active_document: {phi_b: 0.8}}}
        wait(0)
        compare(draft.text, '{"phi_b":0.46}')
    }
}
