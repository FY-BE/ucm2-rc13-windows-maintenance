.pragma library

function parse(text) {
    try { const value = JSON.parse(text); return value && typeof value === "object" && !Array.isArray(value) ? value : null }
    catch (error) { return null }
}
function field(key, label, kind, unit, hint, options) {
    return {key:key, label:label, kind:kind || "text", unit:unit || "", hint:hint || "", options:options || []}
}
function groups(domain) {
    if (domain === 1) return [{title:"PLC输入策略", note:"输入含义与是否参与联锁分别设置；实际采用状态由ARM回报。", fields:[
        field("plc_interlock_policy","联锁策略","enum","","",[
            {value:"ARM_ONLY",label:"仅ARM自主判断"}, {value:"PLC_OPTIONAL_INTERLOCK",label:"PLC可选联锁"}, {value:"PLC_REQUIRED_INTERLOCK",label:"PLC必需联锁"}]),
        field("plc_state_encoding","PLC状态编码","enum","","",[
            {value:"PROTOCOL_A_3_STATE",label:"协议A三态"}, {value:"OPEN_PERMIT_BOOLEAN",label:"开模许可布尔量"}])]}]
    return [
        {title:"型号与适用范围", note:"只编辑ARM文档中已经存在的字段。配置身份及代数由事务层处理。", fields:[
            field("model_name","型号名称"), field("pack_id","参数包标识"), field("applicability_name","适用范围名称")]},
        {title:"BODY_REFERENCE参考几何", note:"M_ref与C_ref描述同一物理状态；几何编辑不授予标定资格。杆径、根径和等效面积独立配置；修改直径不会自动换算或覆盖面积。", fields:[
            field("mold_reference_mm","参考模厚 M_ref","number","mm"), field("body_reference_mm","参考本体长度 C_ref","number","mm"),
            field("l_total_mm","拉杆总长度","number","mm"), field("l_b_mm","B段长度","number","mm"),
            field("l_d_mm","D段长度","number","mm"), field("l_e_mm","E段长度","number","mm")]},
        {title:"杆径与等效面积", note:"等效面积单位为mm²，是独立工程参数，不自动从杆径计算。当前ARM回读值作为编辑起点。", fields:[
            field("rod_diameter_mm","四杆直径","array","mm","按杆1至4填写4个正数，用逗号分隔；不联动等效面积"),
            field("thread_root_diameter_mm","螺纹根径","number","mm","保持ARM回读值，候选默认不改此项"),
            field("abeq_mm2","B段等效面积","number","mm²"), field("ac_mm2","C段面积","number","mm²"), field("adeq_mm2","D段等效面积","number","mm²")]},
        {title:"材料与补偿参数", note:"GW1850R只允许修改Kmat、四路残余偏置和零锚定单调PWL；结构字段只读。", fields:[
            field("kmat_unified","统一材料系数","number"), field("coupling_bias_ns","四路耦合偏置","array","ns","单位ns；四路独立偏置在ARM换算前各扣一次，不能填入诊断拟合截距b"),
            field("force_correction_knot_count","PWL节点数","integer","点","允许0或2–8；0表示不启用分段修正"),
            field("force_correction_input_n","PWL输入节点","array8","N","固定8个槽；未使用槽填0，首点必须为0"),
            field("force_correction_output_n","PWL输出节点","array8","N","固定8个槽；未使用槽填0，首点必须为0"),
            field("phi_b","B段系数 phi_B","number","","暂定建议0.46；以当前回读值为编辑起点"), field("phi_d","D段系数 phi_D","number","","暂定建议0.46；以当前回读值为编辑起点")]},
        {title:"模厚输入", note:"模厚变化按C=C_ref+(M-M_ref)投影；来源选择与参考坐标分别设置。", fields:[
            field("mold_source_mode","模厚来源","enum","","",[
                {value:"AUTO",label:"自动（当前兼容值）"}, {value:"AUTO_RS485_FIRST",label:"自动，RS485优先"}, {value:"RS485_ONLY",label:"仅RS485"},
                {value:"ETHERCAT_ONLY",label:"仅EtherCAT"}, {value:"MODEL_DEFAULT_ONLY",label:"仅型号默认值"}, {value:"AUTO_ETHERCAT_FIRST",label:"自动，EtherCAT优先"}]),
            field("fixed_mold_thickness_mm","型号默认模厚","number","mm")]},
        {title:"测量质量门", note:"本地表单检查数值格式；最终允许范围和生效结果由ARM校验。", fields:[
            field("min_ncc_peak","最低相关峰","number"), field("open_stable_confirm_frames","开模稳定确认帧数","integer","帧")]}
    ]
}
function descriptor(domain,key) {
    const sections=groups(domain)
    for (let i=0;i<sections.length;i++) for(let j=0;j<sections[i].fields.length;j++)
        if(sections[i].fields[j].key===key) return sections[i].fields[j]
    return null
}
function edit(text,domain,key,input) {
    const value=parse(text), description=descriptor(domain,key)
    if(!knownContract(domain,value) || !description || value[key]===undefined)
        return {ok:false,error:"请先载入可编辑的ARM当前文档；此字段没有已接入合同。"}
    if(domain===0 && value.device_model_id===37
       && ["kmat_unified","coupling_bias_ns","force_correction_knot_count",
           "force_correction_input_n","force_correction_output_n"].indexOf(key)<0)
        return {ok:false,error:"GW1850R结构参数已由权威图纸/有限元模型冻结；这里只允许修改Kmat、四路偏置和PWL。"}
    let next=input
    if(description.kind==="number" || description.kind==="integer") {
        if(String(input).trim()==="") return {ok:false,error:description.label+"不能为空"}
        next=Number(input)
        if(!isFinite(next) || (description.kind==="integer" && (!Number.isSafeInteger(next) ||
             (key==="force_correction_knot_count" ? (next<0 || next>8 || next===1) : next<=0))))
            return {ok:false,error:description.label+"的数值格式无效"}
        if(["mold_reference_mm","body_reference_mm","l_total_mm","l_b_mm","l_d_mm","l_e_mm","thread_root_diameter_mm","abeq_mm2","ac_mm2","adeq_mm2"].indexOf(key)>=0 && next<=0)
            return {ok:false,error:description.label+"必须大于零"}
    } else if(description.kind==="array" || description.kind==="array8") {
        next=String(input).replace(/，/g,",").split(",").map(function(part){return part.trim()})
        if(description.kind==="array8") {
            if(next.length!==8 || next.some(function(part){return part==="" || !isFinite(Number(part))}))
                return {ok:false,error:description.label+"必须填写8个有限数值"}
            next=next.map(Number)
        } else if(key==="coupling_bias_ns") {
            if((next.length!==1 && next.length!==4) || next.some(function(part){return part==="" || !isFinite(Number(part))}))
                return {ok:false,error:"耦合偏置须填写1个有限数或4个独立的有限数"}
            next=next.map(Number)
            if(next.length===1) next=[next[0],next[0],next[0],next[0]]
        } else {
            if(next.length!==4 || next.some(function(part){return part==="" || !isFinite(Number(part))}))
                return {ok:false,error:description.label+"必须填写4个有限数值"}
            next=next.map(Number)
        }
        if(key==="rod_diameter_mm" && next.some(function(part){return part<=0}))
            return {ok:false,error:"四杆直径均必须大于零"}
    } else if(description.kind==="enum") {
        if(!description.options.some(function(option){return option.value===input})) return {ok:false,error:"未识别的策略选项"}
    } else { next=String(input).trim(); if(!next) return {ok:false,error:description.label+"不能为空"} }
    value[key]=next
    return {ok:true,text:JSON.stringify(value,null,2),error:""}
}
function display(value) {
    if(value===undefined || value===null) return "未提供"
    return Array.isArray(value) ? value.join(", ") : typeof value==="object" ? JSON.stringify(value) : String(value)
}
function label(domain,key) {
    const item=descriptor(domain,key)
    const common={configuration_generation:"配置代数",schema_version:"文档版本",system_package_id_sha256:"系统包身份",device_model_config_id_sha256:"型号配置身份",system_input_policy_id_sha256:"输入策略身份"}
    return item ? item.label : common[key] || key
}
function diff(domain,before,after) {
    if(!before || !after) return []
    const keys=Array.from(new Set(Object.keys(before).concat(Object.keys(after)))).sort(), rows=[]
    keys.forEach(function(key){if(JSON.stringify(before[key])!==JSON.stringify(after[key])) rows.push({field:key,label:label(domain,key),before:display(before[key]),after:display(after[key])})})
    return rows
}
function upgradeState(state) {
    return ["尚未开始","正在传输","正在校验","已验证并暂存","正在激活","已激活","升级失败","已中止"][Number(state)] || "状态未知"
}


function knownContract(domain,value) {
    if(!value) return false
    if(domain===1) return value.schema_version===1
    if(domain!==0 || (value.schema_version!==2 && value.schema_version!==3)) return false
    const gw=value.device_model_id===37 && value.model_name==="GW1850R"
             && value.schema_version===3 && value.geometry_model==="GW_DRAWING_FE_ENGINEERING_V1"
    if(!gw && value.geometry_model!=="BODY_REFERENCE_V1") return false
    const forbidden=["a_segment_offset_mm","mold_to_a_end_mapping_id","mold_to_a_end_scale","mold_to_a_end_offset_mm","tiebar_a_end_distance_mm","verified","force_verified","qualification"]
    return !forbidden.some(function(key){return Object.prototype.hasOwnProperty.call(value,key)})
}
function referenceDefaults(text) {
    const value=parse(text)
    if(!knownContract(0,value)) return {ok:false,error:"请先载入schema 2/3当前文档；不自动迁移旧schema 1"}
    if(value.device_model_id===37) return {ok:false,error:"GW1850R结构参数已冻结，不能套用A001/DE168候选几何。"}
    const candidateArea=Math.PI*90*90/4
    const defaults={rod_diameter_mm:[90,90,90,90],abeq_mm2:candidateArea,ac_mm2:candidateArea,adeq_mm2:candidateArea,mold_reference_mm:1075,body_reference_mm:2000,l_total_mm:2500,l_b_mm:90,l_d_mm:90,l_e_mm:40,phi_b:0.46,phi_d:0.46}
    for(const key in defaults) {
        if(value[key]===undefined) return {ok:false,error:"当前文档缺少必需字段："+key}
        value[key]=defaults[key]
    }
    return {ok:true,text:JSON.stringify(value,null,2),error:""}
}

function writeStatus(offline,logged,connected,capabilities,operation,allowed) {
    if(offline) return "离线预览：只允许本地编辑，不向设备写入"
    if(!logged) return "尚未登录工程师；请先完成身份验证"
    if(!connected) return "设备未连接；等待连接恢复"
    if(operation.outcome==="unresolved") return "上次事务未对账，禁止新写入"
    if(operation.busy) return "设备事务处理中，请等待当前结果"
    if(capabilities.blockReason) return String(capabilities.blockReason)
    if(!capabilities.commissioned) return "当前连接未获产品操作授权"
    return allowed ? "按当前能力开放；提交仍需设备实时校验" : "当前设备能力或控制状态不允许写入"
}
function monitoringStatus(offline,connected,ready) {
    if(offline) return "离线预览 · 未采集设备数据"
    if(!connected) return "连接未建立 · 等待连接恢复，历史曲线保留断点"
    return ready ? "监测目标 50 Hz（20 ms）· 显示ARM真实帧" : "等待有效ARM帧 · 无效区间保留断点"
}
function upgradeSteps(operation,state,hasFile) {
    const phases=["checking","transferring","verifying","staged","activating","reboot_query"]
    const titles=["1  检查升级包","2  传输","3  校验","4  暂存","5  独立激活","6  激活后查询"]
    const phase=operation.phase || ""
    const current=phases.indexOf(phase)
    return titles.map(function(title,index){
        let note="等待前序步骤"
        if(index===0) note=hasFile ? "已选文件，等待检查" : "等待选择文件"
        if(current===index) note="正在进行"
        if(current>index && index<4) note="已完成此步骤"
        if(!phase && state===index && state>=1 && state<=4) note=upgradeState(state)
        if(index===3 && state===3) note="已校验并暂存，尚未激活"
        if(index===4 && state===5) note="设备报告已激活"
        if(index===5) note=phase==="complete" ? "激活后查询已确认" : phase==="reboot_query" ? "正在查询激活终态；若断连，重连后继续" : "尚未确认激活终态"
        if((phase==="failed" || phase==="unresolved") && current<0 && index===5) note="结果未确认；保留事务供查询"
        return {title:title,note:note}
    })
}
