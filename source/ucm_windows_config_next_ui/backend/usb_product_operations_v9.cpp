#include "usb_product_operations_v9.h"
#include "usb_extended_wire_v2.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QtEndian>
#include <utility>

namespace ucm::productv9 {
namespace {
void put32(QByteArray &b,int p,quint32 v) { qToLittleEndian(v,reinterpret_cast<uchar *>(b.data()+p)); }
void put64(QByteArray &b,int p,quint64 v) { qToLittleEndian(v,reinterpret_cast<uchar *>(b.data()+p)); }
void put16(QByteArray &b,int p,quint16 v) { qToLittleEndian(v,reinterpret_cast<uchar *>(b.data()+p)); }
quint32 u32(const QByteArray &b,int p) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData()+p)); }
quint64 u64(const QByteArray &b,int p) { return qFromLittleEndian<quint64>(reinterpret_cast<const uchar *>(b.constData()+p)); }
bool zero(const QByteArray &b) { for(char c:b) if(c) return false; return true; }
bool digest(const QByteArray &b) { return b.size()==32 && !zero(b); }
bool fail(QString *e,const char *message) { if(e) *e=QString::fromUtf8(message); return false; }
QByteArray upgradeHeader(int bytes) {
    QByteArray b(bytes,0); put32(b,0,0x32475555); put16(b,4,2); put16(b,6,bytes); return b;
}
bool active(const UsbExtendedCapabilitiesV2 &c,quint64 bit) {
    return c.protocolRevision==9 && (c.supportedFeatureMask&bit)==bit
        && (c.activeFeatureMask&bit)==bit;
}
}
QByteArray encodeConfigWrite(const ConfigWriteRequest &r,QString *error) {
    const int maximum=r.domain==Domain::DeviceModel?16384:4096;
    bool valid=(r.domain==Domain::DeviceModel || r.domain==Domain::InputPolicy) && r.operation>=1 && r.operation<=3 && r.bootId && r.authorityGeneration
        && r.requestId && r.entryId && r.sessionId && r.createdMonotonicNs
        && r.expiresMonotonicNs>r.createdMonotonicNs && digest(r.payloadDigest)
        && digest(r.resultId) && digest(r.systemSha256) && r.expectedCurrentId.size()==32;
    if(r.operation==2) valid=valid && r.json.isEmpty() && r.expectedCurrentId==r.resultId;
    else valid=valid && !r.json.isEmpty() && r.json.size()<=maximum
        && QCryptographicHash::hash(r.json,QCryptographicHash::Sha256)==r.payloadDigest;
    if (valid && r.operation != 2) {
        QByteArray identity;
        const auto object = QJsonDocument::fromJson(r.json).object();
        const auto key = r.domain == Domain::DeviceModel ? "device_model_config_id_sha256" : "system_input_policy_id_sha256";
        valid = computeDocumentIdentity(r.domain, r.json, &identity, error)
            && identity == r.resultId
            && object[key].toString().toLatin1() == identity.toHex()
            && object["system_package_id_sha256"].toString().toLatin1() == r.systemSha256.toHex()
            && serializeDocument(r.domain, object, error) == r.json;
    }
    if(!valid) { fail(error,"配置事务身份、ARM时间、摘要或JSON形状无效。"); return {}; }
    QByteArray b(256,0);
    put32(b,0,r.domain==Domain::DeviceModel?0x32434455:0x31504955);
    put32(b,4,r.domain==Domain::DeviceModel?2:1); put32(b,8,256);
    put32(b,12,256+r.json.size()); put32(b,16,r.operation);
    put64(b,24,r.bootId); put64(b,32,r.authorityGeneration); put64(b,40,r.requestId);
    put64(b,48,r.entryId); put64(b,56,r.sessionId);
    put64(b,64,r.createdMonotonicNs); put64(b,72,r.expiresMonotonicNs);
    b.replace(80,32,r.payloadDigest); b.replace(112,32,r.expectedCurrentId);
    b.replace(144,32,r.resultId); b.replace(176,32,r.systemSha256);
    put32(b,208,r.json.size()); const quint32 crc=crc32(b);
    if(!crc) { fail(error,"配置事务头CRC为零。"); return {}; }
    put32(b,212,crc); b.append(r.json); return b;
}
ConfigOperationClient::ConfigOperationClient(Transact t,WriteGate g)
    :transact_(std::move(t)),gate_(std::move(g)) {}
bool ConfigOperationClient::start(const ConfigWriteRequest &r,const UsbExtendedCapabilitiesV2 &c,
    bool authorized,bool commissioned,QString *error) {
    if(outcome_==OperationOutcome::Pending || outcome_==OperationOutcome::Unresolved)
        return fail(error,"上一配置事务尚未完成对账。");
    const quint64 bit=r.domain==Domain::DeviceModel?kUsbExtendedFeatureDeviceModelControlV2
        :kUsbExtendedFeatureSystemInputPolicyControlV1;
    const quint32 ops=r.domain==Domain::DeviceModel?c.deviceModelOperationMask:c.systemInputPolicyOperationMask;
    if(!authorized || !commissioned || !transact_ || !gate_ || !active(c,bit)
        || r.operation<1 || r.operation>3 || !(ops&(1U<<(r.operation-1))))
        return fail(error,"配置写入未授权、连接未就绪或设备能力未激活。");
    const QByteArray payload=encodeConfigWrite(r,error); if(payload.isEmpty()) return false;
    QByteArray key=QByteArray::number(r.bootId)+":"+QByteArray::number(r.authorityGeneration)+":"+QByteArray::number(r.requestId);
    QByteArray entry=QByteArray::number(r.bootId)+":"+QByteArray::number(r.authorityGeneration)+":"+QByteArray::number(r.entryId);
    if(usedRequests_.contains(key) || usedEntries_.contains(entry))
        return fail(error,"配置事务ID或entry已使用，须查询原事务而不是重发或改写负载。");
    if(usedRequests_.size()>=64) return fail(error,"本地配置事务账本已满，须完成验收交接后开启新会话。");
    if(!gate_(error)) return false;
    usedRequests_.insert(key,QCryptographicHash::hash(payload,QCryptographicHash::Sha256));
    usedEntries_.insert(entry,r.requestId);
    request_=r; lastTransitionSequence_=0; outcome_=OperationOutcome::Pending;
    const quint16 type=static_cast<quint16>((r.domain==Domain::DeviceModel?27:42)
        +(r.operation==3?0:r.operation==1?1:2));
    QByteArray response;
    if(!transact_(type,payload,&response,error)) { disconnected(); return false; }
    return accept(response,r.requestId,error);
}
bool ConfigOperationClient::accept(const QByteArray &b,quint64 responseId,QString *error) {
    State s;
    if(!decodeState(b,request_.domain,&s,error) || s.responseRequestId!=responseId
        || s.bootId!=request_.bootId || s.authorityGeneration!=request_.authorityGeneration
        || s.sessionId!=request_.sessionId || u64(b,72)<lastTransitionSequence_
        || s.systemSha256!=request_.systemSha256 || u64(b,64)!=request_.entryId) {
        outcome_=OperationOutcome::Unresolved; return fail(error,"配置回执身份或事务entry不匹配，结果待对账。");
    }
    state_=s; lastTransitionSequence_=u64(b,72);
    const quint32 tx=u32(b,24);
    if(tx==2 || tx==3) { outcome_=OperationOutcome::Pending; return true; }
    if(tx==4 && u32(b,16)==0 && u32(b,28)==0) {
        const int identityOffset=request_.operation==3?152:request_.operation==1?216:280;
        const quint32 flag=request_.operation==3?1:request_.operation==1?2:4;
        const quint32 kind=u32(b,12);
        const bool kindMatches=request_.operation==3?kind==3:request_.operation==1?(kind==4 || kind==5):kind==5;
        if(!kindMatches || (u32(b,20)&flag)==0 || b.mid(identityOffset,32)!=request_.resultId
            || b.mid(identityOffset+32,32)!=request_.payloadDigest
            || ((request_.operation==2 || (request_.operation==1 && kind==5)) &&
                (!s.startupCommitSequence || !s.startupGeneration || !s.activeValid || !s.startupValid ||
                 s.activeId!=request_.resultId || s.startupId!=request_.resultId ||
                 s.activeDigest!=request_.payloadDigest || s.startupDigest!=request_.payloadDigest ||
                 s.activeGeneration!=s.startupGeneration))) {
            outcome_=OperationOutcome::Unresolved; return fail(error,"终态未确认目标配置身份或持久化提交。");
        }
        outcome_=OperationOutcome::Succeeded; return true;
    }
    outcome_=tx==1 || tx==5 || tx==6 || tx==8?OperationOutcome::Failed:OperationOutcome::Unresolved;
    return true;
}
bool ConfigOperationClient::poll(quint64 id,QString *error) {
    if((outcome_!=OperationOutcome::Pending && outcome_!=OperationOutcome::Unresolved) || !id)
        return fail(error,"没有待对账配置事务或查询ID无效。");
    QByteArray response; const auto payload=encodeQuery(request_.domain,4,id,error);
    if(payload.isEmpty() || !transact_(request_.domain==Domain::DeviceModel?24:39,payload,&response,error)) {
        disconnected(); return false;
    }
    return accept(response,id,error);
}
void ConfigOperationClient::disconnected() {
    state_={}; if(outcome_==OperationOutcome::Pending) outcome_=OperationOutcome::Unresolved;
}
bool decodeUpgradeStatus(const QByteArray &b,UpgradeStatus *out,QString *error) {
    if(out) *out={};
    if(!out || b.size()!=128 || u32(b,0)!=0x32475555 || u32(b,4)!=0x00800002
        || u32(b,8)!=1 || u32(b,12)>7 || u32(b,40)>12 || u32(b,44)!=0
        || !zero(b.mid(116,12))) return fail(error,"升级状态头、目标、保留区无效。");
    UpgradeStatus s; s.state=u32(b,12); s.transactionId=u64(b,16);
    s.receivedBytes=u64(b,24); s.totalBytes=u64(b,32); s.result=u32(b,40);
    s.expectedSha256=b.mid(48,32); s.actualSha256=b.mid(80,32); s.packageVersion=u32(b,112);
    bool valid=false;
    if(!s.state) valid=!s.transactionId && !s.receivedBytes && !s.totalBytes && !s.result
        && zero(s.expectedSha256) && zero(s.actualSha256) && !s.packageVersion;
    else if(s.transactionId && s.totalBytes && s.receivedBytes<=s.totalBytes
        && s.packageVersion && digest(s.expectedSha256)) {
        if(s.state==1 || s.state==2) valid=zero(s.actualSha256) && s.result==11
            && (s.state==1 || s.receivedBytes==s.totalBytes);
        else if(s.state>=3 && s.state<=5) valid=s.receivedBytes==s.totalBytes
            && s.actualSha256==s.expectedSha256 && s.result==(s.state==4?11U:0U);
        else valid=s.result!=0 && s.result!=11;
    }
    if(!valid) return fail(error,"升级状态进度、摘要或终态结果不一致。");
    *out=s; return true;
}
UpgradeClient::UpgradeClient(Transact t,WriteGate g):transact_(std::move(t)),gate_(std::move(g)) {}
bool UpgradeClient::begin(const UpgradePackage &p,const UsbExtendedCapabilitiesV2 &c,
    bool authorized,bool commissioned,QString *error) {
    if(outcome_==OperationOutcome::Pending || outcome_==OperationOutcome::Unresolved
        || !authorized || !commissioned || !transact_ || !gate_
        || !active(c,kUsbExtendedFeatureUpgradeStagingV2) || c.activeUpgradeTargetMask!=1)
        return fail(error,"升级未授权、未联调、能力未激活或上一事务未对账。");
    if(!p.transactionId || !p.totalBytes || p.totalBytes>16ULL*1024*1024
        || !p.chunkBytes || p.chunkBytes>65536 || p.chunkBytes%512 || !p.packageVersion
        || !digest(p.sha256) || p.deviceModel.isEmpty() || p.deviceModel.size()>=32
        || p.deviceModel.contains(char(0)) || p.buildId.isEmpty() || p.buildId.size()>=24
        || p.buildId.contains(char(0))) return fail(error,"升级包身份、尺寸、摘要或分块无效。");
    if(usedTransactions_.contains(p.transactionId))
        return fail(error,"升级事务ID已使用，须查询原包状态，禁止用同一事务改写包。");
    if(usedTransactions_.size()>=64) return fail(error,"本地升级事务账本已满。");
    if(!gate_(error)) return false;
    usedTransactions_.insert(p.transactionId,p.sha256);
    package_=p; status_={}; lastConfirmedStatus_={}; activationAttempted_=false; commissioned_=true;
    auto b=upgradeHeader(128); put32(b,8,1); put64(b,16,p.transactionId);
    put64(b,24,p.totalBytes); put32(b,32,p.chunkBytes); put32(b,36,p.packageVersion);
    b.replace(40,32,p.sha256); b.replace(72,p.deviceModel.size(),p.deviceModel);
    b.replace(104,p.buildId.size(),p.buildId); return exchange(32,b,error);
}
bool UpgradeClient::exchange(quint16 type,const QByteArray &b,QString *error) {
    QByteArray response; outcome_=OperationOutcome::Pending;
    if(!transact_(type,b,&response,error)) { disconnected(); return false; }
    UpgradeStatus s;
    if(!decodeUpgradeStatus(response,&s,error) || s.transactionId!=package_.transactionId
        || s.totalBytes!=package_.totalBytes || s.expectedSha256!=package_.sha256
        || s.packageVersion!=package_.packageVersion) {
        outcome_=OperationOutcome::Unresolved; return fail(error,"升级回执与当前包事务不匹配。");
    }
    const quint64 expectedChunkEnd=type==33?u64(b,16)+u32(b,24):0;
    if((type==32 && (s.state!=1 || s.receivedBytes!=0))
        || (type==33 && s.state!=1 && s.state!=6 && s.state!=7)
        || (type==33 && s.state==1 && s.receivedBytes!=expectedChunkEnd)
        || (lastConfirmedStatus_.transactionId && (s.receivedBytes<lastConfirmedStatus_.receivedBytes || s.state<lastConfirmedStatus_.state))
        || (type==34 && s.state!=2 && s.state!=3 && s.state!=6 && s.state!=7)
        || (type==36 && s.state!=7)
        || (type==46 && s.state!=4 && s.state!=5 && s.state!=6 && s.state!=7)) {
        outcome_=OperationOutcome::Unresolved; return fail(error,"升级回执状态或分块确认进度不符合操作。");
    }
    status_=s; lastConfirmedStatus_=s;
    outcome_=s.state==5?OperationOutcome::Succeeded:s.state>=6?OperationOutcome::Failed:OperationOutcome::Pending;
    // A staged reply after a lost activate ACK does not prove the activation
    // was never admitted. Keep querying; never automatically resend or abort.
    if(activationAttempted_ && s.state<4) outcome_=OperationOutcome::Unresolved;
    return true;
}
bool UpgradeClient::chunk(const QByteArray &data,QString *error) {
    if(outcome_!=OperationOutcome::Pending || status_.state!=1 || data.isEmpty()
        || data.size()>package_.chunkBytes || status_.receivedBytes%512
        || quint64(data.size())>package_.totalBytes-status_.receivedBytes
        || (status_.receivedBytes+quint64(data.size())!=package_.totalBytes && data.size()%512))
        return fail(error,"升级分块状态、长度或对齐无效。");
    if(!gate_(error)) return false;
    auto b=upgradeHeader(32); put64(b,8,package_.transactionId); put64(b,16,status_.receivedBytes);
    put32(b,24,data.size()); b.append(data); return exchange(33,b,error);
}
bool UpgradeClient::finalize(QString *error) {
    if(outcome_!=OperationOutcome::Pending || status_.state!=1 || status_.receivedBytes!=package_.totalBytes)
        return fail(error,"升级文件尚未完整确认接收。");
    if(!gate_(error)) return false;
    auto b=upgradeHeader(64); put64(b,8,package_.transactionId); put64(b,16,package_.totalBytes);
    b.replace(24,32,package_.sha256); return exchange(34,b,error);
}
bool UpgradeClient::poll(QString *error) {
    if(!package_.transactionId || !transact_) return fail(error,"没有待查询升级事务。");
    QByteArray b(16,0); put64(b,0,package_.transactionId); put32(b,8,1); return exchange(35,b,error);
}
bool UpgradeClient::activate(const UsbExtendedCapabilitiesV2 &c,bool authorized,QString *error) {
    if(!authorized || !commissioned_ || activationAttempted_ || outcome_!=OperationOutcome::Pending || status_.state!=3
        || !active(c,kUsbExtendedFeatureUpgradeStagingV2|kUsbExtendedFeatureUpgradeActivationV2)
        || c.activeUpgradeTargetMask!=1) return fail(error,"激活需要已验证暂存包、设备能力和单独授权。");
    if(!gate_(error)) return false;
    auto b=upgradeHeader(80); put64(b,8,package_.transactionId); b.replace(16,32,package_.sha256);
    put32(b,48,package_.packageVersion); activationAttempted_=true; return exchange(46,b,error);
}
bool UpgradeClient::abort(quint32 reason,bool authorized,QString *error) {
    if(!authorized || !reason || !commissioned_ || activationAttempted_ || !package_.transactionId
        || status_.state==0 || status_.state==4 || status_.state==5
        || outcome_==OperationOutcome::Unresolved)
        return fail(error,"中止需要明确授权和已对账的未激活事务。");
    if(!gate_(error)) return false;
    auto b=upgradeHeader(32); put64(b,8,package_.transactionId); put32(b,16,reason);
    return exchange(36,b,error);
}
void UpgradeClient::disconnected() {
    status_={}; if(outcome_==OperationOutcome::Pending) outcome_=OperationOutcome::Unresolved;
}
}
