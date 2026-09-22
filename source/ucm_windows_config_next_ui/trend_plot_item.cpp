#include "trend_plot_item.h"
#include "trend_curve.h"

#include <QColor>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

void addLines(QSGNode *root, const QVector<QPointF> &points,
              const QColor &color, unsigned int mode, float width)
{
    if (points.isEmpty()) return;
    auto *geometry = new QSGGeometry(
        QSGGeometry::defaultAttributes_Point2D(), points.size());
    geometry->setDrawingMode(mode);
    geometry->setLineWidth(width);
    auto *vertices = geometry->vertexDataAsPoint2D();
    for (qsizetype index = 0; index < points.size(); ++index) {
        vertices[index].set(static_cast<float>(points[index].x()),
                            static_cast<float>(points[index].y()));
    }
    auto *material = new QSGFlatColorMaterial;
    material->setColor(color);
    auto *node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setFlag(QSGNode::OwnsMaterial);
    root->appendChildNode(node);
}

QVariantMap pointToVariant(const TrendPoint &point)
{
    QVariantList values;
    for (double value : point.values) values.push_back(value);
    return {
        {QStringLiteral("timestampMs"), point.timestampMs},
        {QStringLiteral("validMask"), static_cast<int>(point.validMask)},
        {QStringLiteral("values"), values}
    };
}

} // namespace

TrendPlotItem::TrendPlotItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void TrendPlotItem::setSource(QObject *source)
{
    if (m_source == source) return;
    if (m_buffer != nullptr)
        disconnect(m_buffer, nullptr, this, nullptr);
    m_source = source;
    m_buffer = qobject_cast<TrendBuffer *>(source);
    if (m_buffer != nullptr) {
        connect(m_buffer, &TrendBuffer::changed,
                this, &TrendPlotItem::sourceDataChanged);
    }
    emit sourceChanged();
    bumpRevision();
}

void TrendPlotItem::setSamples(const QVariantList &samples)
{
    if (m_samples == samples) return;
    m_samples = samples;
    m_fallbackPoints = decodeSamples(samples);
    emit samplesChanged();
    if (m_buffer == nullptr) bumpRevision();
}

void TrendPlotItem::setCursorRatio(qreal ratio)
{
    ratio = std::clamp(ratio, 0.0, 1.0);
    if (qFuzzyCompare(m_cursorRatio, ratio)) return;
    m_cursorRatio = ratio;
    emit cursorRatioChanged();
}

void TrendPlotItem::setCursorVisible(bool visible)
{
    if (m_cursorVisible == visible) return;
    m_cursorVisible = visible;
    emit cursorVisibleChanged();
}

QVariantMap TrendPlotItem::sampleAtRatio(qreal ratio) const
{
    const auto &points = activePoints();
    if (points.empty()) return {};
    ratio = std::clamp(ratio, 0.0, 1.0);
    const qint64 oldest = points.front().timestampMs;
    const qint64 newest = points.back().timestampMs;
    const double target = oldest + ratio * std::max<qint64>(1, newest - oldest);
    const TrendPoint *best = &points.front();
    double distance = std::abs(best->timestampMs - target);
    for (const TrendPoint &point : points) {
        const double candidate = std::abs(point.timestampMs - target);
        if (candidate < distance) {
            best = &point;
            distance = candidate;
        }
    }
    return pointToVariant(*best);
}

QSGNode *TrendPlotItem::updatePaintNode(
    QSGNode *oldNode, UpdatePaintNodeData *)
{
    delete oldNode;
    auto *root = new QSGNode;
    const qreal width = boundingRect().width();
    const qreal height = boundingRect().height();
    if (width <= 1.0 || height <= 1.0) return root;

    QVector<QPointF> grid;
    grid.reserve(24);
    for (int column = 0; column <= 6; ++column) {
        const qreal x = column * width / 6.0;
        grid << QPointF(x, 0) << QPointF(x, height);
    }
    for (int row = 0; row <= 4; ++row) {
        const qreal y = row * height / 4.0;
        grid << QPointF(0, y) << QPointF(width, y);
    }
    addLines(root, grid, QColor(QStringLiteral("#DDE5F0")),
             QSGGeometry::DrawLines, 1.0f);

    const auto &points = activePoints();
    if (points.empty()) return root;
    const qint64 oldest = points.front().timestampMs;
    const qint64 newest = points.back().timestampMs;
    const double timeRange = std::max<qint64>(1, newest - oldest);
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const TrendPoint &point : points) {
        for (int rod = 0; rod < 4; ++rod) {
            if ((point.validMask & (1U << rod)) == 0U) continue;
            const double value = point.values[rod];
            if (!std::isfinite(value)) continue;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) return root;
    if (std::abs(maximum - minimum) < 0.000001) {
        const double margin = std::max(1.0, std::abs(minimum) * 0.05);
        minimum -= margin;
        maximum += margin;
    } else {
        const double margin = (maximum - minimum) * 0.08;
        minimum -= margin;
        maximum += margin;
    }
    const auto pointX = [=](const TrendPoint &point) {
        return (point.timestampMs - oldest) / timeRange * width;
    };
    const auto pointY = [=](double value) {
        return height - (value - minimum) / (maximum - minimum) * height;
    };
    const QColor colors[4] = {
        QColor(QStringLiteral("#E96A4A")), QColor(QStringLiteral("#3974C9")),
        QColor(QStringLiteral("#1B9D93")), QColor(QStringLiteral("#8657B8"))
    };
    for (int rod = 0; rod < 4; ++rod) {
        QVector<QPointF> segments;
        QVector<QPointF> run;
        const auto appendRun = [&] {
            const QVector<QPointF> curve = smoothTrendRun(run);
            for (qsizetype index = 1; index < curve.size(); ++index)
                segments << curve[index - 1] << curve[index];
            run.clear();
        };
        for (const TrendPoint &point : points) {
            const bool valid = (point.validMask & (1U << rod)) != 0U
                && std::isfinite(point.values[rod]);
            if (!valid) {
                appendRun();
                continue;
            }
            const QPointF mapped(pointX(point), pointY(point.values[rod]));
            if (!run.isEmpty() && mapped.x() <= run.back().x())
                appendRun();
            run.push_back(mapped);
        }
        appendRun();
        addLines(root, segments, colors[rod], QSGGeometry::DrawLines, 2.2f);
    }
    return root;
}

void TrendPlotItem::sourceDataChanged()
{
    bumpRevision();
}

const std::deque<TrendPoint> &TrendPlotItem::activePoints() const
{
    return m_buffer != nullptr ? m_buffer->points() : m_fallbackPoints;
}

std::deque<TrendPoint> TrendPlotItem::decodeSamples(
    const QVariantList &samples)
{
    std::deque<TrendPoint> decoded;
    for (const QVariant &entry : samples) {
        const QVariantMap map = entry.toMap();
        const QVariantList values = map.value(QStringLiteral("values")).toList();
        if (values.size() != 4) continue;
        TrendPoint point;
        point.timestampMs = map.value(QStringLiteral("timestampMs")).toLongLong();
        point.validMask = static_cast<quint32>(
            map.value(QStringLiteral("validMask")).toUInt()) & 0x0fU;
        for (int rod = 0; rod < 4; ++rod)
            point.values[rod] = values.at(rod).toDouble();
        decoded.push_back(point);
    }
    return decoded;
}

void TrendPlotItem::bumpRevision()
{
    ++m_revision;
    emit revisionChanged();
    update();
}
