import QtQuick
import QtTest
import "../components/CustomerSettings.js" as Geometry
import "../components/EngineerConfig.js" as Engineer

TestCase {
    name:"BodyReferenceUi"
    function candidate() {
        return {schema_version:2,geometry_model:"BODY_REFERENCE_V1",model_name:"TEST-ONLY",mold_reference_mm:1075,body_reference_mm:2000,l_total_mm:2500,l_b_mm:90,l_d_mm:90,l_e_mm:40,phi_b:0.8,phi_d:0.8,rod_diameter_mm:[91,92,93,94],thread_root_diameter_mm:85.6,coupling_bias_ns:[0,0,0,0],abeq_mm2:5000,ac_mm2:5000,adeq_mm2:5000}
    }
    function test_referenceAndMovement() {
        const d=candidate()
        const reference=Geometry.bodyReferenceProjection(d)
        verify(reference.valid);compare(reference.referenceA,280);compare(reference.body,2000);compare(reference.a,280)
        const moved=Geometry.bodyReferenceProjection(d,1175)
        verify(moved.valid);compare(moved.body,2100);compare(moved.a,180)
        verify(!Geometry.bodyReferenceProjection(d,1355).valid)
        d.body_reference_mm=2280
        verify(!Geometry.bodyReferenceProjection(d).valid)
    }
    function test_mixedOrLegacyContractsBlocked() {
        const old=candidate();old.schema_version=1
        verify(!Engineer.knownContract(0,old));verify(Geometry.bodyReferenceContractError(old).length>0)
        const keys=["mold_to_a_end_scale","mold_to_a_end_offset_mm","mold_to_a_end_mapping_id","a_segment_offset_mm","verified"]
        keys.forEach(function(key){const d=candidate();d[key]=0;verify(!Engineer.knownContract(0,d));verify(!Geometry.bodyReferenceProjection(d).valid)})
        verify(Engineer.knownContract(1,{schema_version:1}))
        verify(!Engineer.knownContract(1,{schema_version:2}))
    }
    function test_explicitReferenceDefaultsPreserveRootAndDoNotQualify() {
        const before=candidate()
        const result=Engineer.referenceDefaults(JSON.stringify(before))
        verify(result.ok)
        const after=JSON.parse(result.text)
        compare(after.phi_b,0.46);compare(after.phi_d,0.46)
        const expectedArea=Math.PI*90*90/4
        compare(after.ac_mm2,expectedArea);compare(after.abeq_mm2,expectedArea);compare(after.adeq_mm2,expectedArea)
        compare(JSON.stringify(after.rod_diameter_mm),"[90,90,90,90]");compare(after.thread_root_diameter_mm,85.6)
        compare(before.ac_mm2,5000);compare(before.rod_diameter_mm[0],91);compare(before.phi_b,0.8)
        verify(after.verified===undefined);verify(after.qualification===undefined)
        verify(Engineer.descriptor(0,"rod_diameter_mm")!==null)
        verify(Engineer.descriptor(0,"abeq_mm2")!==null)
        verify(Engineer.descriptor(0,"mold_reference_mm")!==null)
        verify(!Engineer.referenceDefaults(JSON.stringify({schema_version:1})).ok)
    }
    function test_engineerDiameterAndAreaEditsAreIndependent() {
        const before=candidate(), source=JSON.stringify(before)
        const diameter=Engineer.edit(source,0,"rod_diameter_mm","95,96,97,98")
        verify(diameter.ok)
        const after=JSON.parse(diameter.text)
        compare(JSON.stringify(after.rod_diameter_mm),"[95,96,97,98]")
        compare(after.abeq_mm2,5000);compare(after.ac_mm2,5000);compare(after.adeq_mm2,5000)
        compare(after.phi_b,0.8);compare(after.phi_d,0.8);compare(after.thread_root_diameter_mm,85.6)
        verify(after.verified===undefined);verify(after.qualification===undefined)
        const area=Engineer.edit(source,0,"ac_mm2","6100")
        verify(area.ok)
        const areaAfter=JSON.parse(area.text)
        compare(areaAfter.ac_mm2,6100);compare(areaAfter.abeq_mm2,5000);compare(areaAfter.adeq_mm2,5000)
        compare(JSON.stringify(areaAfter.rod_diameter_mm),JSON.stringify(before.rod_diameter_mm))
        const root=Engineer.edit(source,0,"thread_root_diameter_mm","84")
        verify(root.ok);compare(JSON.parse(root.text).thread_root_diameter_mm,84)
        compare(JSON.parse(root.text).ac_mm2,5000)
        verify(!Engineer.edit(source,0,"rod_diameter_mm","0,90,90,90").ok)
        verify(!Engineer.edit(source,0,"rod_diameter_mm","90,-1,90,90").ok)
        verify(!Engineer.edit(source,0,"rod_diameter_mm","90,90,90").ok)
        verify(!Engineer.edit(source,0,"thread_root_diameter_mm","0").ok)
        verify(!Engineer.edit(source,0,"abeq_mm2","-1").ok)
        const compensation=Engineer.edit(source,0,"coupling_bias_ns","-1")
        verify(compensation.ok);compare(JSON.stringify(JSON.parse(compensation.text).coupling_bias_ns),"[-1,-1,-1,-1]")
        verify(Engineer.edit(source,0,"coupling_bias_ns","2,2,2,2").ok)
        verify(Engineer.edit(source,0,"coupling_bias_ns","-1,0,1,-2").ok)
    }
    function test_gw1850rContractFreezesStructureButAllowsCalibration() {
        const gw={schema_version:3,device_model_id:37,model_name:"GW1850R",
            geometry_model:"GW_DRAWING_FE_ENGINEERING_V1",kmat_unified:2.1619923995787173e-15,
            coupling_bias_ns:[0,0,0,0],force_correction_knot_count:0,
            force_correction_input_n:[0,0,0,0,0,0,0,0],
            force_correction_output_n:[0,0,0,0,0,0,0,0],l_total_mm:5230}
        const source=JSON.stringify(gw)
        verify(Engineer.knownContract(0,gw))
        verify(Engineer.edit(source,0,"kmat_unified","2.2e-15").ok)
        verify(Engineer.edit(source,0,"coupling_bias_ns","1,2,3,4").ok)
        verify(Engineer.edit(source,0,"force_correction_knot_count","3").ok)
        verify(Engineer.edit(source,0,"force_correction_input_n","0,10000,20000,0,0,0,0,0").ok)
        verify(!Engineer.edit(source,0,"l_total_mm","5200").ok)
    }
    function test_customerCannotEditReferenceOrDiameterAndChecksReferenceA() {
        const basic=Geometry.customerCandidate("TEST-ONLY",2500,90,90,40,90)
        verify(basic.mold_reference_mm===undefined);verify(basic.body_reference_mm===undefined);verify(basic.rod_diameter_mm===undefined)
        compare(Geometry.validateCustomerReference(basic,candidate()),"")
        basic.l_total_mm=2200
        verify(Geometry.validateCustomerReference(basic,candidate()).length>0)
    }
}
