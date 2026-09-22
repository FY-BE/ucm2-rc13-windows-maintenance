import QtQuick
import QtQuick.Controls
import QtTest
import "../components"

TestCase {
    name: "CalibrationWrite"
    when: windowShown
    width: 1300
    height: 900

    QtObject {
        id: camera
        property var candidate: ({})
        property var sourceCouplingBiasNs: [0.1,0.2,0.3,0.4]
        property var liveFit: ({})
        property var fitProgress: ({})
        function acceptCandidate() { return true }
        property string sessionPath: "C:/sessions/test-session"
        property bool running: false
        property bool stageRecording: false
        property bool busy: false
        property bool manualMode: false
        property bool roiConfirmed: false
        property var rois: ({})
        property var recognition: ({})
        property string status: ""
        property string previewUrl: ""
        property int imageWidth: 0
        property int imageHeight: 0
        property int stageCount: 0
        property int cameraCount: 0
        property int cameraValid: 0
        property int cameraRejected: 0
        property int armCount: 0
        property int armValid: 0
    }
    QtObject {
        id: fake
        signal stateChanged()
        signal connectionChanged()
        property bool engineerMode: true
        property bool connected: true
        property bool offlinePreview: false
        property bool productCanWrite: true
        property var productCapabilities: ({modelValidate:true, modelApply:true, modelSave:true})
        property var productState: ({})
        property var calibrationOperation: ({})
        property var calibrationReadback: ({})
        property int readbackRequests: 0
        property int lastOperation: 0
        function refreshCalibrationReadback() { ++readbackRequests }
        function submitCalibrationCandidate(operation, authorized) {
            if (!authorized) return
            lastOperation = operation
            calibrationOperation = {operation:operation, outcome:"succeeded", busy:false, message:"ARM 回执成功"}
            stateChanged()
        }
    }
    Component {
        id: pageComponent
        CalibrationPage {
            width: 1280; height: 850
            calibration: camera
            backendObject: fake
            productState: fake.productState
        }
    }
    function identity(id, generation) {
        return {identitySha256:id, generation:String(generation), bootId:"1",
                authorityGeneration:"2", sessionId:"3"}
    }
    function init() {
        const fitIdentity = identity("a".repeat(64), 5)
        camera.candidate = {schema:"ucm-windows-calibration-candidate/v3", status:"candidate",
            session_id:"test-session", fit_constraint:"shared_kmat_shared_b",
            kmat_unified:0.25, b_shared_ns:1, coupling_bias_ns:[0.1,0.2,0.3,0.4],
            force_correction:{model:"MONOTONE_PWL_ZERO_V1",knot_count:3,
                input_force_n:[0,10000,20000],output_force_n:[0,11000,25000]},
            active_model_identity:fitIdentity, accepted_stages:3, accepted_pairs:30}
        fake.engineerMode = true; fake.connected = true; fake.productCanWrite = true
        fake.productCapabilities = {modelValidate:true, modelApply:true, modelSave:true}
        fake.productState = {success:true, deviceModel:{available:true, active_identity:fitIdentity,
            active_document:{schema_version:2, geometry_model:"BODY_REFERENCE_V1", kmat_unified:0.1,
                coupling_bias_ns:[0.1,0.2,0.3,0.4]}}}
        fake.calibrationOperation = ({})
        fake.lastOperation = 0
        fake.readbackRequests = 0
        fake.calibrationReadback = ({})
    }
    function test_requiresConfirmationValidationAndReadback() {
        const page = createTemporaryObject(pageComponent, this)
        verify(page !== null)
        const accept = findChild(page, "calibrationCandidateConfirmation")
        const write = findChild(page, "calibrationWriteConfirmation")
        verify(findChild(page, "calibrationAcceptButton").enabled)
        verify(!findChild(page, "calibrationValidateButton").enabled)
        page.requestedOperation = 1; write.open(); write.accept()
        compare(fake.lastOperation, 0)
        accept.open(); accept.accept()
        verify(findChild(page, "calibrationValidateButton").enabled)
        page.requestedOperation = 3; write.open(); write.accept()
        compare(fake.lastOperation, 3)
        verify(findChild(page, "calibrationApplyButton").enabled)
        fake.productState = {success:true, deviceModel:{available:true,
            active_identity:identity("b".repeat(64), 6),
            active_document:{schema_version:2, geometry_model:"BODY_REFERENCE_V1", kmat_unified:0.1,
                coupling_bias_ns:[0.1,0.2,0.3,0.4]}}}
        verify(!findChild(page, "calibrationApplyButton").enabled)
        fake.productState = {success:true, deviceModel:{available:true,
            active_identity:identity("a".repeat(64), 5),
            active_document:{schema_version:2, geometry_model:"BODY_REFERENCE_V1", kmat_unified:0.1,
                coupling_bias_ns:[0.1,0.2,0.3,0.4]}}}
        page.requestedOperation = 1; write.open(); write.accept()
        compare(fake.lastOperation, 1)
        verify(!findChild(page, "calibrationSaveButton").enabled)
        const applied = {schema_version:3, geometry_model:"BODY_REFERENCE_V1", kmat_unified:0.25,
            coupling_bias_ns:[0.1,0.2,0.3,0.4],force_correction_knot_count:3,
            force_correction_input_n:[0,10000,20000,0,0,0,0,0],
            force_correction_output_n:[0,11000,25000,0,0,0,0,0]}
        fake.productState = {success:true, deviceModel:{available:true,
            active_identity:identity("c".repeat(64), 6), active_document:applied}}
        fake.stateChanged()
        verify(findChild(page, "calibrationSaveButton").enabled)
        page.requestedOperation = 2; write.open(); write.accept()
        compare(fake.lastOperation, 2)
        fake.productState = {success:true, deviceModel:{available:true,
            active_identity:identity("c".repeat(64), 6), active_document:applied,
            startup_identity:identity("c".repeat(64), 6), startup_document:applied}}
        fake.stateChanged()
        compare(page.savedSessionId, "test-session")
    }
    function test_offlineOrChangedIdentityNeverWrites() {
        const page = createTemporaryObject(pageComponent, this)
        const accept = findChild(page, "calibrationCandidateConfirmation")
        const write = findChild(page, "calibrationWriteConfirmation")
        accept.open(); accept.accept()
        fake.connected = false; fake.connectionChanged()
        page.requestedOperation = 3; write.open(); write.accept()
        compare(fake.lastOperation, 0)
        fake.connected = true
        fake.productState = {success:true, deviceModel:{available:true,
            active_identity:identity("z".repeat(64), 5), active_document:{}}}
        page.requestedOperation = 3; write.open(); write.accept()
        compare(fake.lastOperation, 0)
    }
    function test_legacyOffsetCannotBeAccepted() {
        camera.candidate = Object.assign({}, camera.candidate, {b_total_ns:[1,2,3,4]})
        const page = createTemporaryObject(pageComponent, this)
        verify(page !== null)
        verify(!findChild(page, "calibrationAcceptButton").enabled)
    }
    function test_readbackWithoutCandidateUsesOnlyArmValues() {
        camera.candidate = ({})
        const page = createTemporaryObject(pageComponent, this)
        verify(page !== null)
        const button = findChild(page, "calibrationReadbackButton")
        verify(button.enabled)
        button.clicked()
        compare(fake.readbackRequests, 1)
        const active = {model_name:"A001",kmat_unified:0.42,coupling_bias_ns:[1,2,3,4],
            force_correction_knot_count:2,force_correction_input_n:[0,10000],
            force_correction_output_n:[0,12000]}
        const startup = Object.assign({}, active, {kmat_unified:0.21})
        fake.calibrationReadback = {outcome:"succeeded",busy:false,readUtc:"2026-09-15T12:00:00Z",
            active_document:active,startup_document:startup,
            active_identity:identity("x".repeat(64),9),startup_identity:identity("y".repeat(64),8),
            parametersMatch:false,message:"ARM 回读成功"}
        verify(findChild(page, "calibrationActiveValues").text.indexOf("0.42") >= 0)
        verify(findChild(page, "calibrationStartupValues").text.indexOf("0.21") >= 0)
        verify(findChild(page, "calibrationActiveValues").text.indexOf("[1,2,3,4]") >= 0)
        verify(findChild(page, "calibrationActiveValues").text.indexOf("10000 → 12000") >= 0)
        verify(findChild(page, "calibrationParametersMatch").text.indexOf("不同") >= 0)
        fake.calibrationReadback = Object.assign({},fake.calibrationReadback,
            {outcome:"failed",message:"USB读取超时"})
        verify(findChild(page, "calibrationActiveValues").text.indexOf("0.42") < 0)
        fake.calibrationReadback = Object.assign({},fake.calibrationReadback,
            {outcome:"pending",busy:true})
        verify(!button.enabled)
        fake.connected = false; fake.connectionChanged()
        verify(!button.enabled)
        verify(findChild(page, "calibrationActiveValues").text.indexOf("0.42") < 0)
    }
    function test_fittedInterceptDoesNotReplaceIndependentCouplingBias() {
        const page = createTemporaryObject(pageComponent, this)
        const wrong = {kmat_unified:0.25,coupling_bias_ns:[1,1,1,1],force_correction_knot_count:3,
            force_correction_input_n:[0,10000,20000,0,0,0,0,0],
            force_correction_output_n:[0,11000,25000,0,0,0,0,0]}
        fake.productState = {success:true,deviceModel:{available:true,active_document:wrong}}
        verify(!page.currentHasCandidate())
        fake.productState = {success:true,deviceModel:{available:true,
            active_document:Object.assign({},wrong,{coupling_bias_ns:[0.1,0.2,0.3,0.4]})}}
        verify(page.currentHasCandidate())
    }
}
