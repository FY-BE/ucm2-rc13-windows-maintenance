#include "usb_functionfs_transport.h"
#include <QtEndian>

namespace ucm {
namespace {
QString outcomeText(productv9::OperationOutcome o) {
    switch(o) {
    case productv9::OperationOutcome::Idle:return QStringLiteral("idle");
    case productv9::OperationOutcome::Pending:return QStringLiteral("pending");
    case productv9::OperationOutcome::Succeeded:return QStringLiteral("succeeded");
    case productv9::OperationOutcome::Failed:return QStringLiteral("failed");
    case productv9::OperationOutcome::Unresolved:return QStringLiteral("unresolved");
    }
    return QStringLiteral("unresolved");
}
ProductOperationResult configResult(bool ok,const productv9::ConfigOperationClient &c,const QString &error) {
    return {ok,outcomeText(c.outcome()),error,c.state().fields};
}
ProductOperationResult upgradeResult(bool ok,const productv9::UpgradeClient &c,const QString &error) {
    const auto &s=c.status();
    QJsonObject fields{{"state",qint64(s.state)},{"result",qint64(s.result)},
        {"transactionId",QString::number(s.transactionId)}, {"receivedBytes",QString::number(s.receivedBytes)},
        {"totalBytes",QString::number(s.totalBytes)}, {"packageVersion",qint64(s.packageVersion)},
        {"expectedSha256",QString::fromLatin1(s.expectedSha256.toHex())},
        {"actualSha256",QString::fromLatin1(s.actualSha256.toHex())}};
    return {ok,outcomeText(c.outcome()),error,fields};
}
}
bool UsbFunctionfsTransport::productOperationsCommissioned() const {
    const auto &c=m_extendedDiscovery.capabilities;
    const quint64 authority=kUsbExtendedFeatureControlAuthorityV2;
    const quint64 operations=kUsbExtendedFeatureDeviceModelControlV2 |
        kUsbExtendedFeatureSystemInputPolicyControlV1 | kUsbExtendedFeatureUpgradeStagingV2;
    return m_initialized && m_backend && m_backend->isOpen() && m_extendedDiscovery.available
        && c.protocolRevision==9 && (c.supportedFeatureMask&c.activeFeatureMask&authority)==authority
        && (c.supportedFeatureMask&c.activeFeatureMask&operations)!=0;
}
bool UsbFunctionfsTransport::productWriteGate(QString *error) {
    if(!m_initialized || !m_backend || !m_backend->isOpen()
        || m_extendedDiscovery.capabilities.protocolRevision!=9) {
        if(error) *error=QStringLiteral("尚未建立 revision 9 产品连接。"); return false;
    }
    // Capability readiness is discovered from ARM, never a permanent local W2 switch.
    discoverExtendedV2();
    if(!productOperationsCommissioned()) {
        if(error) *error=QStringLiteral("ARM实时产品能力未激活，当前操作不可用。");
        return false;
    }
    const auto result=readControlAuthorityState();
    if(!result.success) { if(error) *error=result.message; return false; }
    const auto &s=result.state;
    if(!usbHostManagedStoppedV2(s)) { if(error) *error=QStringLiteral("ARM尚未确认主机托管且硬件安全停止。"); return false; }
    return true;
}
void UsbFunctionfsTransport::ensureProductOperationClients() {
    if(m_productConfigClient && m_productUpgradeClient) return;
    auto send=[this](quint16 type,const QByteArray &payload,QByteArray *out,QString *error) {
        const bool query=type==24 || type==39 || type==35;
        if(!m_initialized || m_extendedDiscovery.capabilities.protocolRevision!=9) {
            if(error) *error=QStringLiteral("产品连接已失效。"); return false;
        }
        // Defense in depth: even a direct callback invocation checks live authority.
        if(!query && !productWriteGate(error)) return false;
        const bool configWrite=(type>=27 && type<=29)||(type>=42 && type<=44);
        if(configWrite) {
            const bool model=type<=29;
            const auto &c=m_extendedDiscovery.capabilities;
            const quint64 feature=model?kUsbExtendedFeatureDeviceModelControlV2:kUsbExtendedFeatureSystemInputPolicyControlV1;
            const quint32 mask=model?c.deviceModelOperationMask:c.systemInputPolicyOperationMask;
            const int first=model?27:42;
            const quint32 operation=type==first?3:type==first+1?1:2;
            if((c.supportedFeatureMask&c.activeFeatureMask&feature)!=feature || !(mask&(1U<<(operation-1)))
                || payload.size()<256 || qFromLittleEndian<quint64>(payload.constData()+32)!=m_controlAuthority.generation
                || qFromLittleEndian<quint64>(payload.constData()+64)>m_controlAuthority.publishedMonotonicNs
                || qFromLittleEndian<quint64>(payload.constData()+72)<=m_controlAuthority.publishedMonotonicNs) {
                if(error) *error=QStringLiteral("ARM配置能力、控制权代次或请求期限已变化；未发送写入。"); return false;
            }
        }
        int offset=(type>=24 && type<=29)||(type>=39 && type<=44)?40:type==32?16:type==35?0:8;
        if(payload.size()<offset+8) { if(error) *error=QStringLiteral("产品操作载荷缺少事务身份。"); return false; }
        const auto id=qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(payload.constData()+offset));
        // Read-only state queries identify the package inside their payload;
        // ARM revision 9 requires a zero transaction in the outer frame.
        const quint64 frameTransaction=query?0:id;
        UsbFrameV1 response;
        if(!transact(static_cast<UsbMessageTypeV1>(type),frameTransaction,payload,&response,error,5000U)) return false;
        if(response.flags!=(query?9U:1U)) {
            if(error) *error=QStringLiteral("产品操作回执标志不符合协议。"); return false;
        }
        *out=response.payload; return true;
    };
    auto gate=[this](QString *e) { return productWriteGate(e); };
    auto upgradeGate=[this](QString *e) { return productWriteGate(e); };
    if(!m_productConfigClient) m_productConfigClient=std::make_unique<productv9::ConfigOperationClient>(send,gate);
    if(!m_productUpgradeClient) m_productUpgradeClient=std::make_unique<productv9::UpgradeClient>(send,upgradeGate);
}
void UsbFunctionfsTransport::invalidateProductOperations() {
    if(m_productConfigClient) m_productConfigClient->disconnected();
    if(m_productUpgradeClient) m_productUpgradeClient->disconnected();
}
ProductOperationResult UsbFunctionfsTransport::submitProductConfiguration(
    const productv9::ConfigWriteRequest &request,bool authorized) {
    ensureProductOperationClients(); QString error;
    if(!authorized) return {false,QStringLiteral("denied"),QStringLiteral("配置操作尚未获得明确授权。"),{}};
    if(productv9::encodeConfigWrite(request,&error).isEmpty()) return {false,QStringLiteral("denied"),error,{}};
    if(!productWriteGate(&error)) return {false,QStringLiteral("denied"),error,{}};
    const bool ok=m_productConfigClient->start(request,m_extendedDiscovery.capabilities,
        authorized,productOperationsCommissioned(),&error);
    return configResult(ok,*m_productConfigClient,error);
}
ProductOperationResult UsbFunctionfsTransport::pollProductConfiguration(quint64 requestId) {
    ensureProductOperationClients(); QString error;
    const bool ok=m_productConfigClient->poll(requestId,&error);
    return configResult(ok,*m_productConfigClient,error);
}
ProductOperationResult UsbFunctionfsTransport::beginProductUpgrade(const productv9::UpgradePackage &package,bool authorized) {
    ensureProductOperationClients(); QString error;
    if(!authorized) return {false,QStringLiteral("denied"),QStringLiteral("升级暂存尚未获得明确授权。"),{}};
    if(!productWriteGate(&error)) return {false,QStringLiteral("denied"),error,{}};
    const bool ok=m_productUpgradeClient->begin(package,m_extendedDiscovery.capabilities,
        authorized,productOperationsCommissioned(),&error);
    return upgradeResult(ok,*m_productUpgradeClient,error);
}
ProductOperationResult UsbFunctionfsTransport::writeProductUpgradeChunk(const QByteArray &data) {
    ensureProductOperationClients(); QString error;
    const bool ok=m_productUpgradeClient->chunk(data,&error);
    return upgradeResult(ok,*m_productUpgradeClient,error);
}
ProductOperationResult UsbFunctionfsTransport::finalizeProductUpgrade() {
    ensureProductOperationClients(); QString error;
    const bool ok=m_productUpgradeClient->finalize(&error);
    return upgradeResult(ok,*m_productUpgradeClient,error);
}
ProductOperationResult UsbFunctionfsTransport::abortProductUpgrade(quint32 reason,bool authorized) {
    ensureProductOperationClients(); QString error;
    const bool ok=m_productUpgradeClient->abort(reason,authorized,&error);
    return upgradeResult(ok,*m_productUpgradeClient,error);
}
ProductOperationResult UsbFunctionfsTransport::pollProductUpgrade() {
    ensureProductOperationClients(); QString error;
    const bool ok=m_productUpgradeClient->poll(&error);
    return upgradeResult(ok,*m_productUpgradeClient,error);
}
ProductOperationResult UsbFunctionfsTransport::activateProductUpgrade(bool authorized) {
    ensureProductOperationClients(); QString error;
    const bool ok=m_productUpgradeClient->activate(m_extendedDiscovery.capabilities,authorized,&error);
    return upgradeResult(ok,*m_productUpgradeClient,error);
}
}
