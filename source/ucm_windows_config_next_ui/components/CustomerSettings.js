.pragma library

var basicKeys = ["model_name", "l_total_mm", "l_b_mm", "l_d_mm", "l_e_mm"]

function customerCandidate(modelName, totalLength, bLength, dLength, eLength) {
    return {model_name: String(modelName).trim(), l_total_mm: Number(totalLength),
        l_b_mm: Number(bLength), l_d_mm: Number(dLength), l_e_mm: Number(eLength)}
}

function validationError(candidate) {
    if (!candidate.model_name) return "请输入设备型号"
    for (let index = 1; index < basicKeys.length; ++index) {
        const value = candidate[basicKeys[index]]
        if (!isFinite(value) || value <= 0) return "请完整填写总长、B/D/E段长度，数值必须大于零"
    }
    if (candidate.l_total_mm <= candidate.l_b_mm + candidate.l_d_mm + candidate.l_e_mm)
        return "拉杆总长度必须大于B、D、E段长度之和"
    return ""
}

function basicDocument(document) {
    const result = {}
    if (!document) return result
    for (let index = 0; index < basicKeys.length; ++index) {
        const key = basicKeys[index]
        if (document[key] !== undefined) result[key] = document[key]
    }
    const diameter = result.rod_diameter_mm
    if (Array.isArray(diameter) && diameter.length === 4
            && diameter.every(function(value) { return value === diameter[0] }))
        result.rod_diameter_mm = diameter[0]
    return result
}

function fieldLabel(key) {
    const labels = {model_name: "设备型号", l_total_mm: "总长度 (mm)", l_b_mm: "B段长度 (mm)",
        l_d_mm: "D段长度 (mm)", l_e_mm: "E段长度 (mm)", rod_diameter_mm: "拉杆直径 (mm)"}
    return labels[key] || key
}

function displayValue(value) {
    if (value === undefined) return "--（缺失）"
    return typeof value === "object" ? JSON.stringify(value) : String(value)
}

function fieldDifferences(before, after, keys) {
    before = before || {}
    after = after || {}
    const fields = keys || Array.from(new Set(Object.keys(before).concat(Object.keys(after)))).sort()
    const differences = []
    for (let index = 0; index < fields.length; ++index) {
        const key = fields[index]
        if (JSON.stringify(before[key]) !== JSON.stringify(after[key]))
            differences.push({field: key, label: fieldLabel(key), before: displayValue(before[key]), after: displayValue(after[key])})
    }
    return differences
}


function bodyReferenceContractError(document) {
    if(!document || (document.schema_version!==2 && document.schema_version!==3) || document.geometry_model!=="BODY_REFERENCE_V1")
        return "设备型号必须使用schema 2/3 / BODY_REFERENCE_V1；旧型号文档不可应用"
    const forbidden=["a_segment_offset_mm","mold_to_a_end_mapping_id","mold_to_a_end_scale","mold_to_a_end_offset_mm","tiebar_a_end_distance_mm","verified","force_verified","qualification"]
    for(let i=0;i<forbidden.length;i++) if(Object.prototype.hasOwnProperty.call(document,forbidden[i]))
        return "新文档不能混入旧映射或自授标定字段："+forbidden[i]
    return ""
}

function bodyReferenceProjection(document, moldMm) {
    const error=bodyReferenceContractError(document)
    if(error) return {valid:false,error:error}
    const keys=["mold_reference_mm","body_reference_mm","l_total_mm","l_b_mm","l_d_mm","l_e_mm"]
    for(let i=0;i<keys.length;i++) if(typeof document[keys[i]]!=="number" || !isFinite(document[keys[i]]) || document[keys[i]]<=0)
        return {valid:false,error:"参考坐标和各段长度必须为有限正数："+keys[i]}
    const mold=moldMm===undefined ? document.mold_reference_mm : Number(moldMm)
    if(!isFinite(mold) || mold<=0) return {valid:false,error:"试算模厚必须为有限正数"}
    const rest=document.l_total_mm-document.l_b_mm-document.l_d_mm-document.l_e_mm
    const referenceA=rest-document.body_reference_mm
    const body=document.body_reference_mm+(mold-document.mold_reference_mm)
    const a=rest-body
    if(!isFinite(rest)||!isFinite(referenceA)||!isFinite(body)||!isFinite(a)||rest<=0||referenceA<=0||body<=0||a<=0)
        return {valid:false,error:"参考或试算几何无效：A_ref、C和A均须大于零"}
    return {valid:true,error:"",referenceA:referenceA,body:body,a:a,mold:mold}
}

function validateCustomerReference(candidate, currentDocument) {
    const error=validationError(candidate)
    if(error || !currentDocument) return error
    const merged=Object.assign({},currentDocument,candidate)
    return bodyReferenceProjection(merged).error
}
