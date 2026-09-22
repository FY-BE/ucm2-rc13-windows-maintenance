#pragma once

#include <QString>
#include <QtGlobal>

namespace ucm {

QString forceRodReasonText(quint32 code);
QString gateReasonText(quint32 code);
QString diagnosticReasonText(quint32 code);
QString eventLifecycleText(int code);
QString severityText(int code);
QString stageText(int code);
QString scopeText(int code);
QString reasonFamilyText(int code);
QString normalizedProcessStateText(int code);
QString agcLifecycleText(int code);

} // namespace ucm
