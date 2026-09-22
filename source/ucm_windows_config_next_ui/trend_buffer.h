#pragma once

#include <QObject>

#include <array>
#include <deque>

struct TrendPoint {
    qint64 timestampMs = 0;
    quint32 validMask = 0;
    std::array<double, 4> values {};
};

class TrendBuffer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    explicit TrendBuffer(QObject *parent = nullptr);

    int count() const { return static_cast<int>(m_points.size()); }
    const std::deque<TrendPoint> &points() const { return m_points; }
    void append(qint64 timestampMs, quint32 validMask,
                const std::array<double, 4> &values);
    void clear();

signals:
    void changed();

private:
    static constexpr std::size_t maximumPoints = 600;
    static constexpr qint64 maximumAgeMs = 60000;
    std::deque<TrendPoint> m_points;
};
