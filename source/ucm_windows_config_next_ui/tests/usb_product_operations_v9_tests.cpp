#include "usb_product_operations_v9.h"
#include "usb_extended_wire_v2.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QTextStream>
#include <QtEndian>
using namespace ucm;
using namespace ucm::productv9;
namespace {
int failures=0;
void check(bool v,const char *name) { QTextStream(stdout)<<(v?"PASS ":"FAIL ")<<name<<'\n'; if(!v) ++failures; }
void p32(QByteArray &b,int o,quint32 v) { qToLittleEndian(v,reinterpret_cast<uchar*>(b.data()+o)); }
void p64(QByteArray &b,int o,quint64 v) { qToLittleEndian(v,reinterpret_cast<uchar*>(b.data()+o)); }
quint32 r32(const QByteArray &b,int o) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()+o)); }
ConfigWriteRequest request(Domain domain=Domain::DeviceModel) {
    ConfigWriteRequest r; r.domain=domain; r.operation=3; r.bootId=1; r.authorityGeneration=2;
    r.requestId=3; r.entryId=4; r.sessionId=5; r.createdMonotonicNs=6; r.expiresMonotonicNs=7;
    QFile fixture(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()+"/fixtures/arm_product_v9/"+(domain==Domain::DeviceModel?"device_model.json":"input_policy.json"));
    check(fixture.open(QIODevice::ReadOnly),"load real canonical document fixture");
    auto object=QJsonDocument::fromJson(fixture.readAll()).object();
    r.json=serializeDocument(domain,object);
    r.payloadDigest=QCryptographicHash::hash(r.json,QCryptographicHash::Sha256);
    r.resultId=QByteArray::fromHex(object[domain==Domain::DeviceModel?"device_model_config_id_sha256":"system_input_policy_id_sha256"].toString().toLatin1());
    r.systemSha256=QByteArray::fromHex(object["system_package_id_sha256"].toString().toLatin1()); r.expectedCurrentId=QByteArray(32,0); return r;
}
QByteArray receipt(const ConfigWriteRequest &r,quint64 id,quint32 tx=4) {
    QByteArray b(352,0); p32(b,0,r.domain==Domain::DeviceModel?0x32434455:0x31504955);
    p32(b,4,r.domain==Domain::DeviceModel?2:1); p32(b,8,352);
    p32(b,12,r.operation==3?3:r.operation==1?4:5);
    p32(b,20,r.operation==3?9:r.operation==1?2:6); p32(b,24,tx);
    p64(b,32,r.bootId); p64(b,40,r.authorityGeneration); p64(b,48,r.sessionId);
    p64(b,56,id); p64(b,64,r.entryId); p64(b,72,8);
    if(r.operation!=3) p64(b,80,1);
    if(r.operation==2) { p64(b,88,1); p64(b,96,1); }
    p32(b,104,r.domain==Domain::DeviceModel?2:1); p32(b,108,1); p32(b,112,1); p32(b,116,r.domain==Domain::DeviceModel?1:0);
    b.replace(120,32,r.systemSha256);
    int offset=r.operation==3?152:r.operation==1?216:280;
    b.replace(offset,32,r.resultId); b.replace(offset+32,32,r.payloadDigest);
    if(r.operation==2) { b.replace(216,32,r.resultId); b.replace(248,32,r.payloadDigest); }
    p32(b,344,crc32(b)); return b;
}
UsbExtendedCapabilitiesV2 capabilities() {
    UsbExtendedCapabilitiesV2 c; c.protocolRevision=9; c.supportedFeatureMask=c.activeFeatureMask=0x1FF9;
    c.deviceModelOperationMask=c.systemInputPolicyOperationMask=63;
    c.activeUpgradeTargetMask=c.supportedUpgradeTargetMask=1; return c;
}
UpgradePackage package() {
    UpgradePackage p; p.transactionId=8; p.totalBytes=600; p.chunkBytes=512;
    p.packageVersion=1; p.sha256=QByteArray(32,'h'); p.deviceModel="DE168"; p.buildId="test"; return p;
}
QByteArray upgradeStatus(const UpgradePackage &p,quint32 state,quint64 received) {
    QByteArray b(128,0); p32(b,0,0x32475555); p32(b,4,0x00800002); p32(b,8,1); p32(b,12,state);
    p64(b,16,p.transactionId); p64(b,24,received); p64(b,32,p.totalBytes);
    p32(b,40,state==1||state==2||state==4?11:state>=6?1:0);
    b.replace(48,32,p.sha256); if(state>=3 && state<=5) b.replace(80,32,p.sha256);
    p32(b,112,p.packageVersion); return b;
}
void configTests() {
    auto r=request(); QString e; auto b=encodeConfigWrite(r,&e);
    auto header=b.left(256); p32(header,212,0);
    check(b.size()==256+r.json.size() && r32(b,208)==r.json.size() && r32(b,212)==crc32(header),"config exact header CRC and JSON length");
    auto invalid=r; invalid.json.replace("\"schema_version\":2","\"schema_version\":1"); invalid.payloadDigest=QCryptographicHash::hash(invalid.json,QCryptographicHash::Sha256);
    check(encodeConfigWrite(invalid,&e).isEmpty(),"old schema never reaches write wire");
    invalid=r; invalid.json=QJsonDocument(QJsonDocument::fromJson(r.json).object()).toJson(QJsonDocument::Compact); invalid.payloadDigest=QCryptographicHash::hash(invalid.json,QCryptographicHash::Sha256);
    check(encodeConfigWrite(invalid,&e).isEmpty(),"unordered JSON never reaches ARM decoder");
    r.payloadDigest[0]^=1; check(encodeConfigWrite(r,&e).isEmpty(),"reject wrong JSON SHA256"); r=request();
    r.expiresMonotonicNs=r.createdMonotonicNs; check(encodeConfigWrite(r,&e).isEmpty(),"reject invalid ARM expiry"); r=request();
    r.operation=2; r.expectedCurrentId=r.resultId;
    check(encodeConfigWrite(r,&e).isEmpty(),"save never sends JSON"); r.json.clear();
    check(encodeConfigWrite(r,&e).size()==256,"save keeps explicit current identity and payload digest");
    r=request(); int sends=0,gates=0; bool allow=false; QByteArray response;
    ConfigOperationClient client([&](quint16 type,const QByteArray &,QByteArray *out,QString*) {
        ++sends; check(type==27||type==24,"config type follows validate or state query"); *out=response; return true;
    },[&](QString*) { ++gates; return allow; });
    check(!client.start(r,capabilities(),false,true,&e)&&sends==0,"authorization gate prevents send");
    check(!client.start(r,capabilities(),true,false,&e)&&sends==0,"commissioning gate prevents send");
    check(!client.start(r,capabilities(),true,true,&e)&&sends==0&&gates==1,"live authority gate prevents send");
    allow=true; response=receipt(r,r.requestId,2);
    check(client.start(r,capabilities(),true,true,&e)&&client.outcome()==OperationOutcome::Pending,"accepted is pending, not success");
    check(!client.start(r,capabilities(),true,true,&e),"single outstanding config operation");
    client.disconnected(); check(client.outcome()==OperationOutcome::Unresolved&&!client.state().bootId,"disconnect clears state and retains unresolved outcome");
    response=receipt(r,99); check(client.poll(99,&e)&&client.outcome()==OperationOutcome::Succeeded,"query reconciles entry and terminal identity");
    auto changed=r; changed.json="{\"changed\":1}";
    changed.payloadDigest=QCryptographicHash::hash(changed.json,QCryptographicHash::Sha256);
    const int sendsBeforeReuse=sends;
    check(!client.start(changed,capabilities(),true,true,&e)&&sends==sendsBeforeReuse,
        "same config request identity cannot change payload after completion");
    ++r.requestId; ++r.entryId;
    response=receipt(r,r.requestId); p64(response,64,44); p32(response,344,0); p32(response,344,crc32(response));
    check(!client.start(r,capabilities(),true,true,&e)&&client.outcome()==OperationOutcome::Unresolved,"wrong entry never reports success");
    response=receipt(r,100); response[152]^=1; p32(response,344,0); p32(response,344,crc32(response));
    check(!client.poll(100,&e)&&client.outcome()==OperationOutcome::Unresolved,"terminal wrong config identity remains unresolved");
    for(quint32 operation:{1U,2U,3U}) {
        r=request(); r.operation=operation;
        if(operation==2) { r.json.clear(); r.expectedCurrentId=r.resultId; }
        ConfigOperationClient stale([&](quint16,const QByteArray &,QByteArray *out,QString*) {
            *out=receipt(r,r.requestId); p64(*out,48,r.sessionId+1);
            p32(*out,344,0); p32(*out,344,crc32(*out)); return true;
        },[](QString*) { return true; });
        check(!stale.start(r,capabilities(),true,true,&e)&&stale.outcome()==OperationOutcome::Unresolved,
            "same boot/authority with changed session never confirms transaction");
    }
    for(quint32 tx:{6U,7U,8U}) {
        r=request();
        ConfigOperationClient recovery([&](quint16,const QByteArray &,QByteArray *out,QString*) {
            *out=receipt(r,r.requestId,tx); p32(*out,12,tx==8?7:6); p32(*out,16,tx==8?11:9);
            p32(*out,344,0); p32(*out,344,crc32(*out)); return true;
        },[](QString*) { return true; });
        check(recovery.start(r,capabilities(),true,true,&e)
            && recovery.outcome()==(tx==7?OperationOutcome::Unresolved:OperationOutcome::Failed),
            "restored/safe-wait/unresolved transaction is never success");
    }
    for(auto domain:{Domain::DeviceModel,Domain::InputPolicy}) for(quint32 op:{1U,2U,3U}) {
        r=request(domain); r.operation=op;
        if(op==2) { r.json.clear(); r.expectedCurrentId=r.resultId; }
        ConfigOperationClient c([&](quint16 type,const QByteArray &,QByteArray *out,QString*) {
            const quint16 expected=(domain==Domain::DeviceModel?27:42)+(op==3?0:op==1?1:2);
            check(type==expected,"exact config operation message mapping"); *out=receipt(r,r.requestId); return true;
        },[](QString*) { return true; });
        check(c.start(r,capabilities(),true,true,&e)&&c.outcome()==OperationOutcome::Succeeded,"config operation reconciles only matching terminal reply");
    }
    for(auto domain:{Domain::DeviceModel,Domain::InputPolicy}) {
        r=request(domain); r.operation=1;
        ConfigOperationClient saved([&](quint16,const QByteArray &,QByteArray *out,QString*) {
            *out=receipt(r,r.requestId); p32(*out,12,5);p32(*out,20,6);
            p64(*out,88,1);p64(*out,96,9);out->replace(280,32,r.resultId);out->replace(312,32,r.payloadDigest);
            p32(*out,344,0);p32(*out,344,crc32(*out));return true;
        },[](QString*){return true;});
        check(saved.start(r,capabilities(),true,true,&e)&&saved.outcome()==OperationOutcome::Succeeded,
            "apply identical to startup accepts ARM APPLIED_SAVED terminal");
        auto missing=capabilities();missing.activeFeatureMask&=~(domain==Domain::DeviceModel?kUsbExtendedFeatureDeviceModelControlV2:kUsbExtendedFeatureSystemInputPolicyControlV1);
        int writes=0;
        ConfigOperationClient inactive([&](quint16,const QByteArray &,QByteArray*,QString*){++writes;return false;},[](QString*){return true;});
        check(!inactive.start(r,missing,true,true,&e)&&writes==0,"inactive domain cannot write despite other active capabilities");
        r.operation=2;r.json.clear();r.expectedCurrentId=r.resultId;
        ConfigOperationClient badSave([&](quint16,const QByteArray &,QByteArray *out,QString*) {
            *out=receipt(r,r.requestId);(*out)[216]^=1;p32(*out,344,0);p32(*out,344,crc32(*out));return true;
        },[](QString*){return true;});
        check(!badSave.start(r,capabilities(),true,true,&e)&&badSave.outcome()==OperationOutcome::Unresolved,
            "save startup cannot confirm a different active identity");
    }
}
void upgradeTests() {
    auto p=package(); QString e; UpgradeStatus decoded;
    auto b=upgradeStatus(p,3,p.totalBytes);
    check(decodeUpgradeStatus(b,&decoded,&e),"verified staged SHA match accepted");
    b[80]^=1; check(!decodeUpgradeStatus(b,&decoded,&e),"verified SHA mismatch rejected");
    b=upgradeStatus(p,1,0); p32(b,116,1); check(!decodeUpgradeStatus(b,&decoded,&e),"upgrade reserved field rejected");
    b=upgradeStatus(p,1,p.totalBytes+1); check(!decodeUpgradeStatus(b,&decoded,&e),"upgrade progress overflow rejected");
    int sends=0,gates=0; quint32 remoteState=1; quint64 received=0;
    UpgradeClient client([&](quint16 type,const QByteArray &payload,QByteArray *out,QString*) {
        ++sends;
        if(type==32) { check(payload.size()==128 && r32(payload,32)==512,"begin exact ABI"); remoteState=1;received=0; }
        if(type==33) { check(r32(payload,24)==quint32(payload.size()-32),"chunk exact data count"); received+=r32(payload,24); }
        if(type==34) { check(payload.size()==64,"finalize exact ABI"); remoteState=3; }
        if(type==46) { check(payload.size()==80 && r32(payload,48)==1,"activation exact ABI"); remoteState=5; }
        *out=upgradeStatus(p,remoteState,received); return true;
    },[&](QString*) { ++gates; return true; });
    check(!client.begin(p,capabilities(),false,true,&e)&&sends==0,"upgrade requires authorization");
    check(client.begin(p,capabilities(),true,true,&e)&&client.outcome()==OperationOutcome::Pending,"upgrade begin only receiving");
    check(!client.finalize(&e),"cannot finalize incomplete transfer");
    check(!client.chunk(QByteArray(511,'x'),&e),"nonfinal chunk requires alignment");
    check(client.chunk(QByteArray(512,'x'),&e),"confirmed first chunk advances progress");
    check(client.chunk(QByteArray(88,'y'),&e),"last chunk permits short tail");
    check(client.finalize(&e)&&client.status().state==3&&client.outcome()==OperationOutcome::Pending,"finalize stages without activating or claiming installed");
    check(!client.activate(capabilities(),false,&e),"activation requires separate authorization");
    auto c=capabilities(); c.activeFeatureMask&=~kUsbExtendedFeatureUpgradeActivationV2;
    check(!client.activate(c,true,&e),"inactive activation capability prevents send");
    check(client.activate(capabilities(),true,&e)&&client.outcome()==OperationOutcome::Succeeded&&gates==5,"every mutating upgrade call checks authority");
    check(!client.abort(1,true,&e),"cannot abort an activated package");
    auto altered=p; altered.sha256=QByteArray(32,'z');
    const int sendsBeforeReuse=sends;
    check(!client.begin(altered,capabilities(),true,true,&e)&&sends==sendsBeforeReuse,
        "same upgrade transaction cannot change package after completion");
    bool aborted=false;
    UpgradeClient abortable([&](quint16 type,const QByteArray &payload,QByteArray *out,QString*) {
        if(type==36) { check(payload.size()==32 && r32(payload,16)==7,"abort exact ABI and reason"); aborted=true; }
        *out=upgradeStatus(p,aborted?7:1,0); return true;
    },[](QString*) { return true; });
    check(abortable.begin(p,capabilities(),true,true,&e),"begin abortable package");
    check(!abortable.abort(0,true,&e)&&!abortable.abort(7,false,&e),"abort requires nonzero reason and explicit approval");
    check(abortable.abort(7,true,&e)&&abortable.status().state==7&&abortable.outcome()==OperationOutcome::Failed,
        "abort receipt records aborted terminal without claiming activation");
    UpgradeClient lost([](quint16,const QByteArray &,QByteArray *,QString*) { return false; },[](QString*) { return true; });
    check(!lost.begin(p,capabilities(),true,true,&e)&&lost.outcome()==OperationOutcome::Unresolved,"lost begin response is unresolved");
    check(!lost.chunk(QByteArray(512,'x'),&e),"lost response prevents further chunks until reconciliation");
    int activationSends=0, recoverySends=0; quint32 queriedState=3; int corruption=0;
    UpgradeClient recovering([&](quint16 type,const QByteArray &payload,QByteArray *out,QString*) {
        ++recoverySends;
        if(type==46) { ++activationSends; return false; } // ARM may have received activation; ACK lost.
        if(type==35) {
            check(payload.size()==16 && qFromLittleEndian<quint64>(payload.constData())==p.transactionId && r32(payload,8)==1,
                "recovery queries exact original transaction and ARM target");
            *out=upgradeStatus(p,queriedState,p.totalBytes);
            if(corruption==1)p64(*out,16,p.transactionId+1);
            if(corruption==2)(*out)[80]^=1;
            if(corruption==3)p32(*out,112,p.packageVersion+1);
            return true;
        }
        *out=upgradeStatus(p,type==32?1:3,type==32?0:p.totalBytes);return true;
    },[](QString*){return true;});
    check(recovering.begin(p,capabilities(),true,true,&e),"prepare activation recovery transaction");
    check(recovering.poll(&e)&&recovering.status().state==3,"same transaction query confirms verified staging");
    const int beforeActivation=recoverySends;
    check(!recovering.activate(capabilities(),false,&e)&&recoverySends==beforeActivation,"unauthorized activation issues zero packets");
    check(!recovering.activate(capabilities(),true,&e)&&recovering.outcome()==OperationOutcome::Unresolved,
        "lost activation ACK remains unresolved rather than installed");
    check(recovering.poll(&e)&&recovering.outcome()==OperationOutcome::Unresolved,"staged query after lost activation never authorizes a retry");
    const int afterStaged=recoverySends;
    check(!recovering.activate(capabilities(),true,&e)&&!recovering.abort(1,true,&e)&&recoverySends==afterStaged&&activationSends==1,
        "activation attempt latches query-only recovery without repeated activate or abort");
    queriedState=4;check(recovering.poll(&e)&&recovering.outcome()==OperationOutcome::Pending,"ARM activating remains pending");
    recovering.disconnected();queriedState=3;
    check(!recovering.poll(&e)&&recovering.outcome()==OperationOutcome::Unresolved,"disconnect preserves last confirmed state watermark");
    queriedState=5;
    for(int bad:{1,2,3}) {
        corruption=bad;
        check(!recovering.poll(&e)&&recovering.outcome()==OperationOutcome::Unresolved,"active response with wrong transaction SHA or version cannot confirm upgrade");
    }
    corruption=0;
    check(recovering.poll(&e)&&recovering.outcome()==OperationOutcome::Succeeded&&recovering.status().actualSha256==p.sha256,
        "matching ARM ACTIVE same-transaction query confirms activation after disconnect");
    check(activationSends==1,"recovery never resends activation");
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); configTests();upgradeTests();
    if(argc==3 && QString::fromLocal8Bit(argv[1])=="--emit-crosscheck") {
        const QDir output(QString::fromLocal8Bit(argv[2]));
        check(QDir().mkpath(output.absolutePath()),"create crosscheck evidence directory");
        for(int variant=0;variant<3;++variant) {
            auto r=request(); auto object=QJsonDocument::fromJson(r.json).object();
            if(variant) {
                object["mold_reference_mm"]=1075; object["body_reference_mm"]=2000;
                object["fixed_mold_thickness_mm"]=1075; object["phi_b"]=0.46; object["phi_d"]=0.46;
                object["configuration_generation"]=qint64(variant==2 ? 9007199254740993LL : 8);
            }
            r.json=serializeDocument(r.domain,object);
            check(computeDocumentIdentity(r.domain,r.json,&r.resultId),"Windows DCI2 computation");
            object["device_model_config_id_sha256"]=QString::fromLatin1(r.resultId.toHex());
            r.json=serializeDocument(r.domain,object);
            r.payloadDigest=QCryptographicHash::hash(r.json,QCryptographicHash::Sha256);
            const auto payload=encodeConfigWrite(r);
            check(!payload.isEmpty(),"Windows BODY_REFERENCE request encoding");
            QFile file(output.filePath(QString("model-%1.request").arg(variant)));
            check(file.open(QIODevice::WriteOnly) && file.write(payload)==payload.size(),"write crosscheck request");
        }
    }
    return failures?1:0;
}
