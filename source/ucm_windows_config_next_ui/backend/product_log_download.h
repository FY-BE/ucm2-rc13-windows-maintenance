#pragma once
#include "config_transport.h"
#include <QCryptographicHash>

namespace ucm {
// One file at a time, one USB chunk per scheduler turn. Any failure destroys
// partial bytes; callers must refresh the catalog before beginning again.
class ProductLogDownload {
public:
    bool begin(const DeviceLogSource &source);
    bool accept(const DeviceLogChunkResult &chunk);
    void cancel(const QString &reason);
    quint64 offset() const { return quint64(m_data.size()); }
    quint32 nextBytes() const;
    bool complete() const { return m_complete; }
    bool active() const { return m_active; }
    const QString &error() const { return m_error; }
    const DeviceLogSource &source() const { return m_source; }
    QByteArray data() const { return m_complete ? m_data : QByteArray(); }
    QByteArray localSha256() const;
private:
    DeviceLogSource m_source;
    QByteArray m_data;
    QString m_error;
    bool m_active = false, m_complete = false;
};
}
