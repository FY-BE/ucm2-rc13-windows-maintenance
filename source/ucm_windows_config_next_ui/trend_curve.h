#pragma once

#include <QPointF>
#include <QVector>

// Build a display-only curve through every point in one contiguous valid run.
// The curve never overshoots either endpoint of an interval.
QVector<QPointF> smoothTrendRun(const QVector<QPointF> &run);
