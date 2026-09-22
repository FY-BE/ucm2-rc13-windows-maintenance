#include "product_log_download.h"
#include <QRegularExpression>
#include <algorithm>

namespace ucm {
bool ProductLogDownload::begin(const DeviceLogSource &source)
{
    cancel({});
    const QRegularExpression legacy(QStringLiteral("^result-[0-9]{8}-[0-9]{3}\\.urs[12]$"));
    const QRegularExpression native(QStringLiteral("^result-[0-9]{8}-[0-9]{4}\\.url3$"));
    const bool nameValid = (source.kind == 1 && legacy.match(source.name).hasMatch())
        || (source.kind == 2 && native.match(source.name).hasMatch());
    const quint64 recordBytes = source.kind == 2 ? 256u :
        (source.name.endsWith(QStringLiteral(".urs1")) ? 320u : 680u);
    if (!source.available || !source.sourceId || !source.snapshotId
        || !source.fileIdentityCrc32 || source.totalBytes > 16777216U
        || !nameValid || (source.totalBytes && source.totalBytes % recordBytes != 0u)) {
        m_error = QStringLiteral("日志目录项无效或超过16MiB。");
        return false;
    }
    m_source = source;
    // Even an empty file needs a terminal read to revalidate the identity.
    m_active = true;
    return true;
}
quint32 ProductLogDownload::nextBytes() const
{
    if (!m_active) return 0;
    return quint32(std::max<quint64>(1, std::min<quint64>(65536, m_source.totalBytes - offset())));
}
void ProductLogDownload::cancel(const QString &reason)
{
    m_data.clear(); m_source = {}; m_active = false; m_complete = false; m_error = reason;
}
bool ProductLogDownload::accept(const DeviceLogChunkResult &chunk)
{
    if (!m_active) return false;
    if (!chunk.success || chunk.sourceId != m_source.sourceId
        || chunk.snapshotId != m_source.snapshotId
        || chunk.fileIdentityCrc32 != m_source.fileIdentityCrc32
        || chunk.totalBytes != m_source.totalBytes || chunk.offset != offset()
        || quint64(chunk.data.size()) > m_source.totalBytes - offset()
        || chunk.data.size() > nextBytes()
        || chunk.more != (offset() + quint64(chunk.data.size()) < m_source.totalBytes)
        || (chunk.data.isEmpty() && chunk.more)) {
        cancel(chunk.message.isEmpty() ? QStringLiteral("日志分块身份变化，已丢弃本次下载。") : chunk.message);
        return false;
    }
    m_data.append(chunk.data);
    m_complete = !chunk.more;
    m_active = !m_complete;
    return true;
}
QByteArray ProductLogDownload::localSha256() const
{
    return m_complete ? QCryptographicHash::hash(m_data, QCryptographicHash::Sha256) : QByteArray();
}
}
