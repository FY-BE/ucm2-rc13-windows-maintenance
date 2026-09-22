#include "usb_functionfs_transport.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QtEndian>
#include <functional>
using namespace ucm;
namespace {
int failures=0;
void expect(bool ok,const char *name) { if(!ok) { ++failures; QTextStream(stderr)<<"FAIL "<<name<<'\n'; } }
void p32(QByteArray &b,int o,quint32 v) { qToLittleEndian(v,b.data()+o); }
void p64(QByteArray &b,int o,quint64 v) { qToLittleEndian(v,b.data()+o); }
void seal(QByteArray &b,int o) { p32(b,o,0);p32(b,o,usbCrc32V1(b)); }
QByteArray fixture(const QString &name) {
    QFile f(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()+"/fixtures/arm_product_v9/"+name+".bin");
    if(!f.open(QIODevice::ReadOnly)) { expect(false,"fixture open"); return {}; }
    return f.readAll();
}
QByteArray parameterCatalogFixture() {
    QFile f(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()
            + "/fixtures/arm_product_v9_parameter_config/parameter_catalog_descriptors.bin");
    if(!f.open(QIODevice::ReadOnly)) { expect(false,"parameter catalog fixture open"); return {}; }
    return f.readAll();
}
QByteArray basic() {
    QByteArray b(128,0);p32(b,0,0x31504355);qToLittleEndian<quint16>(1,b.data()+4);qToLittleEndian<quint16>(128,b.data()+6);
    p32(b,8,1);p32(b,12,262144);p32(b,40,1);p32(b,48,1);p32(b,52,0x1d6b0105);p32(b,56,1);p32(b,60,0x81);
    p32(b,64,usbCrc32V1("UCM.CONFIG.USB.FUNCTIONFS.V1/1D6B:0105/OUT01/IN81"));
    b.replace(68,22,"UCM-PRODUCT-CONTROL-V2\0");b.replace(96,7,"fixture");return b;
}
class Fake final : public LibusbBackend {
public:
    bool opened=false, failRead=false, malformedError=false, badHeader=false;
    bool nonzeroReadTransaction=false;
    bool waveformAvailable=false;
    bool parameterCatalogAvailable=false;
    bool configPendingOnce=false;
    bool authorityAvailable=false;
    quint32 authorityMode=1, authorityPhase=3, authorityOwner=2, authorityHardware=1;
    quint64 authorityGeneration=40;
    quint64 authorityLastTransaction=0;
    quint64 upgradeTransaction=0, upgradeTotal=0;
    quint32 upgradeVersion=0;
    QByteArray upgradeSha;
    QByteArray authorityPayload() const {
        QByteArray b(128,0);p32(b,0,0x32534155);p32(b,4,0x00800002);
        p32(b,12,authorityMode);p32(b,16,authorityMode);p32(b,20,authorityPhase);
        p32(b,24,authorityOwner);p32(b,32,authorityHardware);p64(b,48,authorityGeneration);
        p64(b,64,authorityLastTransaction);p64(b,72,authorityPhase==7?2000:0);p64(b,80,1000);seal(b,8);return b;
    }
    int mismatchOffset=-1;
    int writeDelayMs=0;
    QVector<unsigned> readBudgets;
    QByteArray requests, replies;
    QByteArray lastConfigRequest;
    QByteArray lastWaveformRequest;
    QVector<int> messages;
    std::function<void(int,QByteArray &)> mutate;
    UsbOpenResult open() override { opened=true; return {true,"mock",{}}; }
    void close() override { opened=false; requests.clear(); replies.clear(); }
    bool isOpen() const override { return opened; }
    QJsonObject evidence() const override { return {{"simulation",true}}; }
    UsbTransferResult writeAll(const QByteArray &bytes,unsigned) override {
        if(writeDelayMs) QThread::msleep(writeDelayMs);
        requests+=bytes;
        if(requests.size()>=40 && requests.size()==40+qFromLittleEndian<quint32>(requests.constData()+12)) {
            const auto type=qFromLittleEndian<quint16>(requests.constData()+8);
            const auto sequence=qFromLittleEndian<quint64>(requests.constData()+16);
            const auto transaction=qFromLittleEndian<quint64>(requests.constData()+24);
            if(((type>=24 && type<=26)||(type>=39 && type<=41)||type==35) && transaction!=0)
                nonzeroReadTransaction=true;
            messages.append(type);
            QByteArray payload; quint16 flags=9, replyType=type;
            switch(type) {
            case 1:payload=basic();break;
            case 16:
                payload=fixture("capabilities");
                if(parameterCatalogAvailable) {
                    const QByteArray descriptors=parameterCatalogFixture();
                    p32(payload,36,65U);
                    p64(payload,72,qFromLittleEndian<quint64>(payload.constData()+72)|2U);
                    p64(payload,80,qFromLittleEndian<quint64>(payload.constData()+80)|2U);
                    p32(payload,92,usbCrc32V1(descriptors));
                }
                break;
            case 17: {
                if(!parameterCatalogAvailable) {
                    replyType=255;flags=3;payload=QByteArray(32,0);
                    p32(payload,0,2);p64(payload,8,sequence);p64(payload,16,transaction);
                    break;
                }
                const QByteArray descriptors=parameterCatalogFixture();
                const quint32 start=qFromLittleEndian<quint32>(requests.constData()+40);
                const quint32 maximum=qFromLittleEndian<quint32>(requests.constData()+44);
                const quint32 expectedCrc=qFromLittleEndian<quint32>(requests.constData()+48);
                const quint32 crc=usbCrc32V1(descriptors);
                if(start>=65U || maximum==0U || maximum>64U || expectedCrc!=crc) {
                    replyType=255;flags=3;payload=QByteArray(32,0);
                    p32(payload,0,1);p64(payload,8,sequence);p64(payload,16,transaction);
                    break;
                }
                const quint32 count=qMin(maximum,65U-start);
                const bool more=start+count<65U;
                payload=QByteArray(32,0);
                p32(payload,0,kUsbParameterCatalogTokenV2);
                qToLittleEndian<quint16>(kUsbExtendedSchemaV2,payload.data()+4);
                qToLittleEndian<quint16>(kUsbParameterCatalogHeaderBytesV2,payload.data()+6);
                p32(payload,8,crc);p32(payload,12,65U);p32(payload,16,start);
                p32(payload,20,count);p32(payload,24,more?1U:0U);
                payload.append(descriptors.mid(static_cast<int>(start*48U),
                                               static_cast<int>(count*48U)));
                flags=static_cast<quint16>(9U|(more?4U:0U));
                break;
            }
            case 20:
                if(authorityAvailable) {
                    payload=authorityPayload();break;
                }
                replyType=255;flags=3;payload=QByteArray(32,0);p32(payload,0,7);p64(payload,8,sequence);p64(payload,16,transaction);
                if(malformedError)p64(payload,8,sequence+1);break;
            case 21: {
                const auto action=qFromLittleEndian<quint32>(requests.constData()+40+12);
                const auto requestedMode=qFromLittleEndian<quint32>(requests.constData()+40+16);
                const auto expectedGeneration=authorityGeneration;
                if(action==1) {
                    authorityMode=requestedMode;
                    authorityPhase=requestedMode==2?7:3;
                    authorityOwner=requestedMode==2?3:2;
                    authorityHardware=requestedMode==2?0:1;
                    ++authorityGeneration;
                }
                authorityLastTransaction=transaction;
                payload=QByteArray(192,0);p32(payload,0,0x32414155);p32(payload,4,0x00c00002);
                p32(payload,12,2);p32(payload,16,0);p64(payload,24,transaction);p64(payload,32,expectedGeneration);
                payload.replace(40,128,authorityPayload());seal(payload,8);flags=1;
                break;
            }
            case 27:case 42:
                lastConfigRequest=requests.mid(40);
                payload=fixture(type==27?"device_model_state":"input_policy_state");flags=1;
                p32(payload,12,3);p32(payload,20,7);p32(payload,24,4);p64(payload,56,transaction);
                p64(payload,32,qFromLittleEndian<quint64>(lastConfigRequest.constData()+24));
                p64(payload,40,qFromLittleEndian<quint64>(requests.constData()+40+32));
                p64(payload,48,qFromLittleEndian<quint64>(lastConfigRequest.constData()+56));
                p64(payload,64,qFromLittleEndian<quint64>(requests.constData()+40+48));
                payload.replace(120,32,lastConfigRequest.mid(176,32));
                payload.replace(152,32,requests.mid(40+144,32));payload.replace(184,32,requests.mid(40+80,32));
                if(configPendingOnce) { p32(payload,12,2);p32(payload,20,6);p32(payload,24,2);
                    payload.replace(152,64,QByteArray(64,0));configPendingOnce=false; }
                seal(payload,344);break;
            case 32:
                upgradeTransaction=transaction;
                upgradeTotal=qFromLittleEndian<quint64>(requests.constData()+40+24);
                upgradeVersion=qFromLittleEndian<quint32>(requests.constData()+40+36);
                upgradeSha=requests.mid(40+40,32);
                [[fallthrough]];
            case 35:
                if(type==35 && transaction!=0) {
                    replyType=255;flags=3;payload=QByteArray(32,0);
                    p32(payload,0,1);p64(payload,8,sequence);p64(payload,16,transaction);
                    break;
                }
                payload=QByteArray(128,0);p32(payload,0,0x32475555);p32(payload,4,0x00800002);
                p32(payload,8,1);p32(payload,12,1);p64(payload,16,upgradeTransaction);
                p64(payload,32,upgradeTotal);p32(payload,40,11);
                payload.replace(48,32,upgradeSha);p32(payload,112,upgradeVersion);
                flags=type==35?9:1;
                break;
            case 8:payload=requests.mid(40);flags=1;break;
            case 7: {
                lastWaveformRequest=requests.mid(40);
                if(!waveformAvailable) {
                    replyType=255;flags=3;payload=QByteArray(32,0);
                    p32(payload,0,7);p64(payload,8,sequence);p64(payload,16,transaction);
                    break;
                }
                const quint32 slice=qFromLittleEndian<quint32>(
                    lastWaveformRequest.constData()+12);
                payload=QByteArray(64+2048*4*2,0);
                p64(payload,0,81);p64(payload,8,82);p64(payload,16,83);p64(payload,24,84);
                p32(payload,32,50000000);p32(payload,36,9781+slice);
                p32(payload,40,2048);p32(payload,44,4);p32(payload,48,2);
                p32(payload,52,usbCrc32V1(payload.mid(64)));p32(payload,56,1);
                p32(payload,60,slice);
                break;
            }
            case 30:payload=fixture("publication_valid");break;
            case 45:payload=fixture("input_status");
                if(mismatchOffset>=0) { p64(payload,mismatchOffset,qFromLittleEndian<quint64>(payload.constData()+mismatchOffset)+1);seal(payload,8); }break;
            case 24:case 25:case 26:case 39:case 40:case 41: {
                const int index=type<=26?type-24:type-39;
                const QString domain=type<=26?"device_model_":"input_policy_";
                const auto queryRequestId=qFromLittleEndian<quint64>(requests.constData()+40+40);
                payload=fixture(domain+QStringList{"state","active","startup"}[index]);
                p64(payload,index==0?56:48,queryRequestId);
                if(index==0 && !lastConfigRequest.isEmpty() &&
                   queryRequestId==qFromLittleEndian<quint64>(lastConfigRequest.constData()+40)) {
                    p32(payload,12,3);p32(payload,20,7);p32(payload,24,4);
                    p64(payload,32,qFromLittleEndian<quint64>(lastConfigRequest.constData()+24));
                    p64(payload,40,qFromLittleEndian<quint64>(lastConfigRequest.constData()+32));
                    p64(payload,48,qFromLittleEndian<quint64>(lastConfigRequest.constData()+56));
                    p64(payload,64,qFromLittleEndian<quint64>(lastConfigRequest.constData()+48));
                    payload.replace(120,32,lastConfigRequest.mid(176,32));
                    payload.replace(152,32,lastConfigRequest.mid(144,32));
                    payload.replace(184,32,lastConfigRequest.mid(80,32));
                }
                seal(payload,index==0?344:188);break;
            }
            case 37:payload=fixture("log_catalog");break;
            case 38:payload=fixture("log_chunk");break;
            default:
                replyType=255;flags=3;payload=QByteArray(32,0);p32(payload,0,7);p64(payload,8,sequence);p64(payload,16,transaction);
                if(malformedError) p64(payload,8,sequence+1);
            }
            if(mutate) mutate(type,payload);
            QString error;
            replies=encodeUsbFrameV1(static_cast<UsbMessageTypeV1>(replyType),flags,sequence,transaction,payload,&error);
            if(badHeader) replies[36]^=1;
            requests.clear();
        }
        return {UsbTransferStatus::Success,bytes.size(),{}, {}};
    }
    UsbTransferResult readExact(int count,unsigned budget) override {
        readBudgets.push_back(budget);
        if(failRead || count>replies.size())return {UsbTransferStatus::Disconnected,0,{},"simulated disconnect"};
        const auto result=replies.left(count);replies.remove(0,count);return {UsbTransferStatus::Success,count,result,{}};
    }
};
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    {
        auto catalogBackend=std::make_unique<Fake>();auto *catalog=catalogBackend.get();
        catalog->parameterCatalogAvailable=true;
        UsbFunctionfsTransport catalogTransport(std::move(catalogBackend));
        const auto discovery=catalogTransport.usbExtendedDiscovery();
        expect(catalogTransport.info().armReceiverContacted
            && discovery.catalogReady && discovery.parameters.size()==65
            && discovery.parameters.last().fieldId==99U,
            "65-entry parameter catalog closes across a MORE page");
        expect(catalog->messages==QVector<int>({1,16,17,17,20}),
            "catalog discovery requests both pages before authority state");
        const auto object=catalogTransport.readUsbParameterCatalogObject();
        expect(object.success && object.data==parameterCatalogFixture(),
            "catalog evidence stores the merged 65 descriptor bytes");
    }
    {
        auto limitedBackend=std::make_unique<Fake>();auto *limited=limitedBackend.get();
        limited->mutate=[](int type,QByteArray &b) {
            if(type==16) {
                p64(b,72,0x261);p64(b,80,0x261);
                b.replace(184,24,QByteArray(24,0));
                b.replace(216,40,QByteArray(40,0));
            }
        };
        UsbFunctionfsTransport limitedTransport(std::move(limitedBackend));
        const int before=limited->messages.size();
        const auto unsupported=limitedTransport.readProductState();
        expect(limitedTransport.info().armReceiverContacted
            && !unsupported.supported && !unsupported.success,
            "unadvertised product domains are unavailable without disconnect");
        expect(limited->messages.size()==before,
            "unadvertised product domains issue no USB requests");
        const int beforeLogs=limited->messages.size();
        const auto logs=limitedTransport.readLogSources();
        expect(!logs.success && logs.message.contains(QStringLiteral("日志目录")),
            "inactive log catalog has an explicit unavailable result");
        expect(limited->messages.size()==beforeLogs,
            "inactive log catalog issues no USB request");
    }
    auto backend=std::make_unique<Fake>();auto *fake=backend.get();
    UsbFunctionfsTransport transport(std::move(backend));
    expect(transport.info().armReceiverContacted,"read-only initializes without CFG2");
    expect(fake->messages==QVector<int>({1,16,20}),"only capabilities and authority during handshake");
    expect(transport.ping({}).success,"empty ping supported");
    auto result=transport.readTelemetrySnapshot();
    expect(result.success && result.formalForceValid && result.formalTotalN==400 && !result.diagnosticFieldsAvailable,"paired ARM formal result");
    const auto product=transport.readProductState();
    expect(product.success && product.deviceModel["active_document"].toObject()["model_name"].toString()=="DE168","both config domains/documents");
    expect(!fake->nonzeroReadTransaction,"read-only product queries use transaction id zero");
    for (int offset : {32, 40, 48, 120}) {
        fake->mutate=[offset](int type,QByteArray &b) {
            if(type>=39 && type<=41) {
                const int at=type==39?offset:(offset==120?88:offset-8);
                b[at]^=1;seal(b,type==39?344:188);
            }
        };
        expect(!transport.readProductState().success,"cross-domain identity change rejects config group");
    }
    fake->mutate={};
    auto list=transport.readLogSources();expect(list.success && list.sources.size()==1,"dynamic catalog");
    if(list.sources.size()==1) expect(transport.readLogChunk(1,0,65536,list.sources[0].snapshotId).success,"identity-bound log chunk");
    const auto count=fake->messages.size();
    expect(!transport.stageRam({}).success,"inactive configuration denied");
    expect(!transport.readWaveformSnapshot().success,"missing mock waveform rejected");
    expect(!transport.submitProductConfiguration({},true).ok,"malformed direct configuration request denied");
    expect(fake->messages.size()==count+1 && fake->messages.last()==7,
           "disabled writes issue zero USB requests while active waveform is queried");
    expect(transport.productOperationsCommissioned(),"live revision9 capabilities enable product path without local permanent switch");
    {
        auto waveformBackend=std::make_unique<Fake>();auto *f=waveformBackend.get();
        f->waveformAvailable=true;
        UsbFunctionfsTransport waveformTransport(std::move(waveformBackend));
        WaveformViewportRequest request;request.expectedGeneration=81;
        request.sliceOffset=2048;
        const auto viewport=waveformTransport.readWaveformViewport(request);
        expect(viewport.success && viewport.supported
            && viewport.waveform.windowStart==9781+2048,
            "message 7 reads a 2048-point slice from the retained 8192-point frame");
        expect(f->lastWaveformRequest.size()==32
            && qFromLittleEndian<quint64>(f->lastWaveformRequest.constData())==81U
            && qFromLittleEndian<quint32>(f->lastWaveformRequest.constData()+12)==2048U
            && f->lastWaveformRequest.mid(16)==QByteArray(16,'\0'),
            "message 7 encodes the slice at offset 12 and preserves its reserved tail");
        f->mutate=[](int type,QByteArray &payload) {
            if(type==7) p64(payload,0,82U);
        };
        expect(!waveformTransport.readWaveformViewport(request).success,
            "message 7 rejects a slice from a different publisher generation");
        f->mutate={};
        request.sliceOffset=6145;
        expect(!waveformTransport.readWaveformViewport(request).success,
            "message 7 rejects a slice outside 0..6144 before USB");
    }
    {
        auto writeBackend=std::make_unique<Fake>();auto *f=writeBackend.get();f->authorityAvailable=true;
        UsbFunctionfsTransport writer(std::move(writeBackend));
        expect(writer.switchControlMode(kUsbControlModeHostManagedV2).success && f->messages.contains(21),"revision9 host takeover consumes one synchronous terminal receipt");
        auto doc=fixture("device_model_active");productv9::ConfigWriteRequest request;
        request.domain=productv9::Domain::DeviceModel;request.operation=3;request.bootId=32;request.authorityGeneration=41;
        request.sessionId=48;request.requestId=100;request.entryId=101;request.createdMonotonicNs=900;request.expiresMonotonicNs=2000;
        request.json=productv9::serializeDocument(request.domain,QJsonDocument::fromJson(doc.mid(256)).object());
        request.resultId=doc.mid(120,32);request.payloadDigest=QCryptographicHash::hash(request.json,QCryptographicHash::Sha256);
        request.expectedCurrentId=QByteArray(32,0);request.systemSha256=doc.mid(88,32);
        const int before=f->messages.size();
        expect(!writer.submitProductConfiguration(request,false).ok && f->messages.size()==before,"authorization denied before USB");
        f->configPendingOnce=true;
        expect(writer.submitProductConfiguration(request,true).ok && f->messages.contains(27),"active capability and fresh host safe wait permit validated configuration");
        expect(writer.pollProductConfiguration(request.requestId).ok && !f->nonzeroReadTransaction,
               "configuration reconciliation query uses read-only frame transaction zero");
        ++request.requestId;++request.entryId;f->authorityGeneration=42;
        const int writes=f->messages.count(27);
        expect(!writer.submitProductConfiguration(request,true).ok && f->messages.count(27)==writes,"changed ARM authority generation never sends stale config");
        f->authorityGeneration=41;f->authorityMode=1;f->authorityPhase=1;
        expect(!writer.submitProductConfiguration(request,true).ok && f->messages.count(27)==writes,"autonomous safe stopped is not host write authority");
        f->failRead=true;expect(!writer.submitProductConfiguration(request,true).ok && !writer.productOperationsCommissioned(),"lost connection revokes capability readiness");
    }
    {
        auto upgradeBackend=std::make_unique<Fake>();auto *f=upgradeBackend.get();f->authorityAvailable=true;
        f->mutate=[](int type,QByteArray &payload) {
            if(type==16) { p64(payload,64,1);p64(payload,80,0x1f39); }
        };
        UsbFunctionfsTransport writer(std::move(upgradeBackend));
        expect(writer.switchControlMode(kUsbControlModeHostManagedV2).success,
               "upgrade mock enters ARM host-managed stopped state");
        productv9::UpgradePackage package;
        package.transactionId=701;package.totalBytes=600;package.chunkBytes=512;
        package.packageVersion=2;package.sha256=QByteArray(32,'u');
        package.deviceModel="A001";package.buildId="UCM2-R2S-156";
        expect(writer.beginProductUpgrade(package,true).ok,
               "upgrade begin uses host-managed stopped transport");
        expect(writer.pollProductUpgrade().ok && !f->nonzeroReadTransaction
                   && f->messages.contains(35),
               "upgrade state query uses read-only frame transaction zero and package identity in payload");
    }
    for(int offset:{16,24,32,40,48,56}) {
        fake->mismatchOffset=offset; const int before=fake->messages.size();
        expect(!transport.readTelemetrySnapshot().success,"each identity mismatch rejected");
        expect(fake->messages.size()-before<=6,"pair retries bounded");
    }
    fake->mismatchOffset=-1;
    fake->mutate=[](int type,QByteArray &b){if(type==30 || type==45){p64(b,40,qFromLittleEndian<quint64>(b.constData()+40)+1);seal(b,8);}};
    expect(!transport.readTelemetrySnapshot().success,"same publication time cannot identify a different paired frame");
    fake->mutate=[](int type,QByteArray &b){if(type==30)b=fixture("publication_invalid");};
    result=transport.readTelemetrySnapshot();
    expect(result.success && !result.formalForceValid && !result.forceAvailableMask,"paired invalid result never becomes formal force");
    fake->mutate=[](int type,QByteArray &b){if(type==30){p32(b,12,6);p32(b,76,9);seal(b,8);}};
    result=transport.readTelemetrySnapshot();
    expect(result.success && !result.formalForceValid && !result.forceAvailableMask,"invalid flag denies all formal values despite force-available bits");
    fake->mutate=[](int type,QByteArray &b){if(type==30)b=fixture("publication_maintenance");};
    const auto maintenanceBefore=fake->messages.size();
    result=transport.readTelemetrySnapshot();
    expect(!result.success && result.primaryReasonCode==10 && fake->messages.size()==maintenanceBefore+1,"maintenance reason retained without invented pair");
    fake->mutate={};
    QThread::msleep(510);
    expect(!transport.readTelemetrySnapshot().success,"frozen repeated result expires locally");
    fake->failRead=true;expect(!transport.readTelemetrySnapshot().success && !transport.info().realUsbOpened,"disconnect closes transport");
    expect(!transport.usbExtendedDiscovery().available && !transport.readProductState().success,"disconnect clears capability and product state");
    fake->failRead=false;expect(transport.reconnect().success,"fresh handshake reconnect");
    fake->writeDelayMs=60;fake->readBudgets.clear();
    expect(transport.readTelemetrySnapshot().success,"delayed transfer still pairs within total budget");
    expect(fake->readBudgets.size()==4 && fake->readBudgets[0]<=250 && fake->readBudgets[2]<=250,"write time deducted from read deadline");
    fake->writeDelayMs=0;
    fake->mutate=[](int type,QByteArray &b){if(type==16)p32(b,12,10);};
    expect(!transport.reconnect().success && !transport.info().realUsbOpened,"unknown revision rejected without fallback");
    fake->mutate={};fake->malformedError=true;
    expect(!transport.reconnect().success,"error rejected sequence mismatch tears down session");
    fake->malformedError=false;expect(transport.reconnect().success,"recover after malformed error");
    fake->badHeader=true;expect(!transport.ping("test").success && !transport.info().realUsbOpened,"header CRC failure invalidates session");
    return failures?1:0;
}
