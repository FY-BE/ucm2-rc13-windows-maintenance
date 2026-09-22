#include "trend_buffer.h"
#include "trend_plot_item.h"
#include "trend_curve.h"

#include <QGuiApplication>
#include <QSGGeometryNode>

#include <array>
#include <algorithm>

namespace {

class InspectableTrendPlotItem : public TrendPlotItem {
public:
    using TrendPlotItem::updatePaintNode;
};

bool expect(bool condition, const char *message)
{
    if (condition) return true;
    qCritical("FAIL: %s", message);
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    TrendBuffer buffer;
    buffer.append(1000, 0x0fU, {100.0, 110.0, 120.0, 130.0});
    buffer.append(2000, 0x0fU, {101.0, 111.0, 121.0, 131.0});
    buffer.append(3000, 0x0fU, {102.0, 112.0, 122.0, 132.0});

    TrendPlotItem plot;
    plot.setSource(&buffer);
    const QVariantMap middle = plot.sampleAtRatio(0.5);
    const QVariantList values = middle.value(QStringLiteral("values")).toList();
    bool passed = true;
    passed &= expect(middle.value(QStringLiteral("timestampMs")).toLongLong()
                         == 2000,
                     "shared cursor selects the nearest timestamp");
    passed &= expect(middle.value(QStringLiteral("validMask")).toInt() == 15,
                     "all four rods remain valid at the shared cursor");
    passed &= expect(values.size() == 4
                         && values.at(0).toDouble() == 101.0
                         && values.at(3).toDouble() == 131.0,
                     "shared cursor exposes all four rod values");

    const QVector<QPointF> rawCurve {
        {0.0, 10.0}, {10.0, 0.0}, {20.0, 10.0}, {30.0, 8.0}
    };
    const QVector<QPointF> drawnCurve = smoothTrendRun(rawCurve);
    passed &= expect(drawnCurve.size() > rawCurve.size()
                         && drawnCurve.front() == rawCurve.front()
                         && drawnCurve.back() == rawCurve.back(),
                     "display curve retains the original endpoints");
    for (const QPointF &point : drawnCurve) {
        int interval = 0;
        while (interval + 2 < rawCurve.size()
               && point.x() > rawCurve[interval + 1].x())
            ++interval;
        const qreal low = std::min(rawCurve[interval].y(),
                                   rawCurve[interval + 1].y());
        const qreal high = std::max(rawCurve[interval].y(),
                                    rawCurve[interval + 1].y());
        passed &= expect(point.y() >= low - 0.001
                             && point.y() <= high + 0.001,
                         "display curve does not overshoot measured values");
    }
    const QVector<QPointF> duplicateTime {{0.0, 1.0}, {0.0, 2.0}};
    passed &= expect(smoothTrendRun(duplicateTime) == duplicateTime,
                     "duplicate timestamps are not interpolated");
    const int dataRevision = plot.revision();
    plot.setCursorRatio(0.75);
    plot.setCursorVisible(true);
    passed &= expect(plot.revision() == dataRevision,
                     "moving the cursor does not rebuild the data curve");

    InspectableTrendPlotItem gapPlot;
    gapPlot.setWidth(400);
    gapPlot.setHeight(200);
    QVariantList gapSamples;
    for (int index = 0; index < 5; ++index) {
        gapSamples.push_back(QVariantMap {
            {QStringLiteral("timestampMs"), 1000 + index * 1000},
            {QStringLiteral("validMask"), index == 2 ? 0 : 1},
            {QStringLiteral("values"), QVariantList {index, 0, 0, 0}}
        });
    }
    gapPlot.setSamples(gapSamples);
    QSGNode *scene = gapPlot.updatePaintNode(nullptr, nullptr);
    const QSGNode *rodNode = scene->firstChild()
        ? scene->firstChild()->nextSibling() : nullptr;
    passed &= expect(rodNode != nullptr,
                     "valid runs produce a line geometry");
    if (rodNode != nullptr) {
        const auto *geometry = static_cast<const QSGGeometryNode *>(rodNode)
                                   ->geometry();
        const auto *vertices = geometry->vertexDataAsPoint2D();
        for (int index = 0; index < geometry->vertexCount(); ++index) {
            passed &= expect(vertices[index].x <= 100.001f
                                 || vertices[index].x >= 299.999f,
                             "invalid frame leaves a visible gap in the curve");
        }
    }
    delete scene;

    for (int index = 0; index < 700; ++index) {
        const double value = 200.0 + index;
        buffer.append(4000 + index * 100, 0x0fU,
                      {value, value + 1.0, value + 2.0, value + 3.0});
    }
    passed &= expect(buffer.count() <= 600,
                     "C++ trend buffer is bounded to 600 latest samples");
    const QVariantMap newest = plot.sampleAtRatio(1.0);
    passed &= expect(newest.value(QStringLiteral("timestampMs")).toLongLong()
                         == 4000 + 699 * 100,
                     "latest-only ring buffer retains the newest sample");
    return passed ? 0 : 1;
}
