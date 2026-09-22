#include "usb_runtime_progress_v9.h"

#include "usb_wire_v1.h"

#include <QtEndian>

namespace ucm {
namespace {
constexpr quint32 kProgressToken = 0x31525055U;
constexpr quint32 kActionToken = 0x31434155U;
constexpr quint32 kActionReceiptToken = 0x31524155U;

quint16 u16(const QByteArray &b, int o) { return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(b.constData()+o)); }
quint32 u32(const QByteArray &b, int o) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData()+o)); }
quint64 u64(const QByteArray &b, int o) { return qFromLittleEndian<quint64>(reinterpret_cast<const uchar *>(b.constData()+o)); }
void put16(QByteArray &b, int o, quint16 v) { qToLittleEndian(v, reinterpret_cast<uchar *>(b.data()+o)); }
void put32(QByteArray &b, int o, quint32 v) { qToLittleEndian(v, reinterpret_cast<uchar *>(b.data()+o)); }
void put64(QByteArray &b, int o, quint64 v) { qToLittleEndian(v, reinterpret_cast<uchar *>(b.data()+o)); }

bool validObject(const QByteArray &payload, int bytes, quint32 token, QString *error)
{
    if (payload.size()!=bytes || u32(payload,0)!=token || u16(payload,4)!=1U || u16(payload,6)!=bytes) {
        if (error) *error=QStringLiteral("运行对象身份或长度不匹配。"); return false;
    }
    QByteArray copy=payload; const quint32 expected=u32(copy,8); put32(copy,8,0U);
    if (expected==0U || usbCrc32V1(copy)!=expected) {
        if (error) *error=QStringLiteral("运行对象CRC不匹配。"); return false;
    }
    return true;
}
}

bool decodeUsbRuntimeProgressV9(const QByteArray &payload, UsbRuntimeProgressV9 *p, QString *error)
{
    if (!p || !validObject(payload,kUsbRuntimeProgressBytesV9,kProgressToken,error)) return false;
    UsbRuntimeProgressV9 v; v.available=true;
    v.generation=u64(payload,16); v.updatedMonotonicNs=u64(payload,24);
    v.stageElapsedMs=u64(payload,32); v.stageLimitMs=u64(payload,40);
    v.estimatedRemainingMs=u64(payload,48); v.lastActionTransactionId=u64(payload,56);
    v.stage=u32(payload,64); v.stageItem=u32(payload,68); v.stageItemCount=u32(payload,72);
    v.overallPermille=u32(payload,76); v.waitReason=u32(payload,80); v.canMeasure=u32(payload,84)!=0;
    v.qualityLevel=u32(payload,88); v.plcState=u32(payload,92); v.plcFresh=u32(payload,96)!=0;
    v.templateValidMask=u32(payload,100); v.tareState=u32(payload,104); v.tareGeneration=u32(payload,108);
    v.lastAction=u32(payload,112); v.lastActionResult=static_cast<qint32>(u32(payload,116)); v.faultCode=u32(payload,120);
    for(int i=0;i<4;++i){v.current.lnaDb[i]=u32(payload,124+i*4);v.best.lnaDb[i]=u32(payload,160+i*4);}
    v.current.pgaDb=u32(payload,140); v.current.vcntlCode=u32(payload,144); v.current.nominalHvV=u32(payload,148); v.current.burstCycles=u32(payload,152);
    v.best.pgaDb=u32(payload,176); v.best.vcntlCode=u32(payload,180); v.best.nominalHvV=u32(payload,184); v.best.burstCycles=u32(payload,188);
    v.activeDeviceModelId=u32(payload,192); v.startupDeviceModelId=u32(payload,196);
    v.pendingDeviceModelId=u32(payload,200); v.modelSwitchState=u32(payload,204);
    v.modelSwitchResult=static_cast<qint32>(u32(payload,208));
    v.modelProfileOrigin=u32(payload,212); v.modelSwitchTransactionId=u64(payload,216);
    if (v.stage>6U || v.overallPermille>1000U || v.qualityLevel>3U || v.plcState>4U
        || v.templateValidMask>15U || v.current.burstCycles>8U || v.best.burstCycles>8U
        || (v.activeDeviceModelId!=0U && v.activeDeviceModelId!=1U && v.activeDeviceModelId!=168U && v.activeDeviceModelId!=37U)
        || (v.startupDeviceModelId!=0U && v.startupDeviceModelId!=1U && v.startupDeviceModelId!=168U && v.startupDeviceModelId!=37U)
        || (v.pendingDeviceModelId!=0U && v.pendingDeviceModelId!=1U && v.pendingDeviceModelId!=168U && v.pendingDeviceModelId!=37U)
        || v.modelSwitchState>2U || v.modelProfileOrigin>2U) {
        if(error)*error=QStringLiteral("运行进度字段超出合同范围。"); return false;
    }
    *p=v; return true;
}

QByteArray encodeUsbRuntimeActionV9(quint32 action, quint64 tx, quint64 generation, QString *error)
{
    /* Action 2 used to clear a force-domain tare.  Runtime results are
     * already relative to the unloaded delay reference, so that second zero
     * layer is retired.  Keep its wire number unused. */
    if((action!=1U&&action!=3U)||tx==0U){if(error)*error=QStringLiteral("运行动作参数无效。");return {};}
    QByteArray b(kUsbRuntimeActionBytesV9,0); put32(b,0,kActionToken);put16(b,4,1U);put16(b,6,kUsbRuntimeActionBytesV9);
    put32(b,12,action);put64(b,24,tx);put64(b,32,generation);put32(b,8,usbCrc32V1(b));return b;
}

bool decodeUsbRuntimeActionReceiptV9(const QByteArray &payload, UsbRuntimeActionResultV9 *r, QString *error)
{
    if(!r||!validObject(payload,kUsbRuntimeActionReceiptBytesV9,kActionReceiptToken,error))return false;
    UsbRuntimeActionResultV9 v;v.action=u32(payload,12);v.result=static_cast<qint32>(u32(payload,16));v.transactionId=u64(payload,24);
    v.generation=u64(payload,32);v.stage=u32(payload,40);v.tareState=u32(payload,44);v.tareGeneration=u32(payload,48);v.faultCode=u32(payload,52);
    v.success=v.result==0;v.raw=payload;v.message=v.success?QStringLiteral("ARM运行动作已完成。"):QStringLiteral("ARM拒绝运行动作（%1）。").arg(v.result);
    *r=v;return true;
}

QString usbRuntimeStageTextV9(quint32 s)
{
    static const char *const names[]={"启动","训练","等待卸载","AGC扫描","建立模板","正式测量","故障"};
    return s<7U?QString::fromUtf8(names[s]):QStringLiteral("未知阶段");
}
QString usbRuntimeQualityTextV9(quint32 q)
{
    switch(q){case 0:return QStringLiteral("红色");case 1:return QStringLiteral("黄色");case 2:return QStringLiteral("绿色");default:return QStringLiteral("未评定");}
}
} // namespace ucm
