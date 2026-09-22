#include "trend_buffer.h"

TrendBuffer::TrendBuffer(QObject *parent)
    : QObject(parent)
{
}

void TrendBuffer::append(qint64 timestampMs, quint32 validMask,
                         const std::array<double, 4> &values)
{
    m_points.push_back({timestampMs, validMask & 0x0fU, values});
    while (m_points.size() > maximumPoints)
        m_points.pop_front();
    while (!m_points.empty()
           && m_points.front().timestampMs < timestampMs - maximumAgeMs) {
        m_points.pop_front();
    }
    emit changed();
}

void TrendBuffer::clear()
{
    if (m_points.empty()) return;
    m_points.clear();
    emit changed();
}
