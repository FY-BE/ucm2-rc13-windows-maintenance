#include "usb_runtime_progress_v9.h"
#include "usb_wire_v1.h"

#include <QtEndian>
#include <cstdio>

namespace {
void p16(QByteArray &b,int o,quint16 v){qToLittleEndian(v,reinterpret_cast<uchar*>(b.data()+o));}
void p32(QByteArray &b,int o,quint32 v){qToLittleEndian(v,reinterpret_cast<uchar*>(b.data()+o));}
void p64(QByteArray &b,int o,quint64 v){qToLittleEndian(v,reinterpret_cast<uchar*>(b.data()+o));}
bool check(bool value,const char *message){if(!value)std::fprintf(stderr,"FAIL: %s\n",message);return value;}
}

int main()
{
    bool ok=true;
    ok&=check(ucm::usbRuntimeQualityTextV9(0)==QStringLiteral("红色"),"quality 0 is red");
    ok&=check(ucm::usbRuntimeQualityTextV9(1)==QStringLiteral("黄色"),"quality 1 is yellow");
    ok&=check(ucm::usbRuntimeQualityTextV9(2)==QStringLiteral("绿色"),"quality 2 is green");
    QByteArray p(ucm::kUsbRuntimeProgressBytesV9,0);
    p32(p,0,0x31525055U);p16(p,4,1);p16(p,6,p.size());
    p64(p,16,9);p64(p,24,1000);p64(p,32,1200);p64(p,40,60000);p64(p,48,8000);
    p32(p,64,3);p32(p,68,7);p32(p,72,12);p32(p,76,583);p32(p,84,1);p32(p,88,2);
    p32(p,92,4);p32(p,96,1);p32(p,100,15);p32(p,104,2);p32(p,108,6);
    for(int i=0;i<4;++i){p32(p,124+i*4,12+i*6);p32(p,160+i*4,24);}
    p32(p,140,30);p32(p,144,12000);p32(p,148,75);p32(p,152,3);
    p32(p,176,24);p32(p,180,9000);p32(p,184,80);p32(p,188,4);
    p32(p,192,1);p32(p,196,1);p32(p,200,168);p32(p,204,1);
    p32(p,208,0);p32(p,212,2);p64(p,216,77);
    p32(p,8,ucm::usbCrc32V1(p));
    ucm::UsbRuntimeProgressV9 decoded;QString error;
    ok&=check(ucm::decodeUsbRuntimeProgressV9(p,&decoded,&error),"valid progress decodes");
    ok&=check(decoded.stage==3&&decoded.overallPermille==583&&decoded.current.pgaDb==30
              &&decoded.current.lnaDb[3]==30&&decoded.best.burstCycles==4
              &&decoded.activeDeviceModelId==1&&decoded.pendingDeviceModelId==168
              &&decoded.modelSwitchState==1&&decoded.modelProfileOrigin==2
              &&decoded.modelSwitchTransactionId==77,"progress offsets match ARM");
    for (int offset : {192, 196, 200}) {
        QByteArray gw=p; p32(gw,offset,37); p32(gw,8,0); p32(gw,8,ucm::usbCrc32V1(gw));
        ok&=check(ucm::decodeUsbRuntimeProgressV9(gw,&decoded,&error),"GW1850R progress model accepted");
        p32(gw,offset,38); p32(gw,8,0); p32(gw,8,ucm::usbCrc32V1(gw));
        ok&=check(!ucm::decodeUsbRuntimeProgressV9(gw,&decoded,&error),"unknown progress model rejected");
    }
    QByteArray broken=p;broken[180]^=1;
    ok&=check(!ucm::decodeUsbRuntimeProgressV9(broken,&decoded,&error),"CRC damage rejected");
    const QByteArray action=ucm::encodeUsbRuntimeActionV9(1,55,9,&error);
    ok&=check(action.size()==64,"action is fixed 64 bytes");
    QByteArray copy=action;const quint32 crc=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(copy.constData()+8));p32(copy,8,0);
    ok&=check(crc==ucm::usbCrc32V1(copy),"action CRC covers zeroed CRC field");
    ok&=check(ucm::encodeUsbRuntimeActionV9(2,55,9,&error).isEmpty(),"retired force-domain clear action rejected");
    ok&=check(ucm::encodeUsbRuntimeActionV9(3,55,9,&error).size()==64,"AGC restart remains available");
    ok&=check(ucm::encodeUsbRuntimeActionV9(4,55,9,&error).isEmpty(),"unknown action rejected");
    return ok?0:1;
}
