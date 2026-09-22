#pragma once

#include "usb_extended_wire_v2.h"
#include "usb_parameter_config_v9.h"

#include <QString>
#include <QVector>

namespace ucm {

struct UsbRuntimeParameterEditV9 {
    quint32 fieldId = 0;
    int elementIndex = -1;
    qint64 value = 0;
};

bool usbRuntimeParameterValueV9(
    const UsbRuntimeConfigObjectV9 &object, quint32 fieldId,
    int elementIndex, qint64 *value, QString *error = nullptr);

bool applyUsbRuntimeParameterEditV9(
    UsbRuntimeConfigObjectV9 *object,
    const UsbParameterDescriptorV2 &descriptor,
    quint64 activeConfigGroupMask,
    const UsbRuntimeParameterEditV9 &edit,
    QString *error = nullptr);

bool verifyUsbRuntimeParameterReadbackV9(
    const UsbRuntimeConfigReceiptV9 &receipt,
    const QVector<UsbRuntimeParameterEditV9> &edits,
    QString *error = nullptr);

bool usbRuntimeConfigurationValuesEqualV9(
    const UsbRuntimeConfigObjectV9 &left,
    const UsbRuntimeConfigObjectV9 &right);

} // namespace ucm
