#include "usb_functionfs_transport.h"

namespace ucm {

BinaryObjectResult UsbFunctionfsTransport::readTelemetryObject()
{
    QString error;
    const QByteArray payload = readTelemetry(&error);
    return payload.isEmpty()
        ? BinaryObjectResult{false, error, {}}
        : BinaryObjectResult{true, QStringLiteral("256 B ARM正式结果对象读取成功（已与输入状态配对）。"),
                             payload};
}

BinaryObjectResult UsbFunctionfsTransport::readWaveformObject()
{
    QString error;
    const QByteArray payload = readWaveform(0, 0, &error);
    return payload.isEmpty()
        ? BinaryObjectResult{false, error, {}}
        : BinaryObjectResult{true, QStringLiteral("16448 B波形对象读取成功。"),
                             payload};
}

BinaryObjectResult UsbFunctionfsTransport::readConfigReceiptObject()
{
    return {false, QStringLiteral(
        "产品配置使用revision 9独立型号／输入策略状态；旧CFG2回执路径已移除。"), {}};
}

} // namespace ucm
