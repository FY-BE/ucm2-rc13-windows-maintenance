#include "trend_curve.h"

#include <algorithm>
#include <cmath>

QVector<QPointF> smoothTrendRun(const QVector<QPointF> &run)
{
    if (run.size() < 2) return run;

    QVector<qreal> slopes(run.size() - 1);
    for (qsizetype index = 0; index + 1 < run.size(); ++index) {
        const qreal distance = run[index + 1].x() - run[index].x();
        if (distance <= 0 || !std::isfinite(distance)) return run;
        slopes[index] = (run[index + 1].y() - run[index].y()) / distance;
    }

    QVector<qreal> tangents(run.size());
    tangents.front() = slopes.front();
    tangents.back() = slopes.back();
    for (qsizetype index = 1; index + 1 < run.size(); ++index) {
        const qreal before = slopes[index - 1];
        const qreal after = slopes[index];
        if (before * after <= 0) {
            tangents[index] = 0;
        } else {
            const qreal previousDistance = run[index].x() - run[index - 1].x();
            const qreal nextDistance = run[index + 1].x() - run[index].x();
            const qreal firstWeight = 2 * nextDistance + previousDistance;
            const qreal secondWeight = nextDistance + 2 * previousDistance;
            tangents[index] = (firstWeight + secondWeight)
                / (firstWeight / before + secondWeight / after);
        }
    }

    QVector<QPointF> result;
    result.reserve(run.size() * 5);
    result.push_back(run.front());
    for (qsizetype index = 0; index + 1 < run.size(); ++index) {
        const QPointF first = run[index];
        const QPointF second = run[index + 1];
        const qreal distance = second.x() - first.x();
        const int steps = std::clamp(static_cast<int>(std::ceil(distance / 4.0)),
                                     2, 8);
        for (int step = 1; step <= steps; ++step) {
            const qreal t = static_cast<qreal>(step) / steps;
            const qreal t2 = t * t;
            const qreal t3 = t2 * t;
            qreal y = (2 * t3 - 3 * t2 + 1) * first.y()
                + (t3 - 2 * t2 + t) * distance * tangents[index]
                + (-2 * t3 + 3 * t2) * second.y()
                + (t3 - t2) * distance * tangents[index + 1];
            y = std::clamp(y, std::min(first.y(), second.y()),
                           std::max(first.y(), second.y()));
            result.push_back({first.x() + t * distance, y});
        }
    }
    return result;
}
