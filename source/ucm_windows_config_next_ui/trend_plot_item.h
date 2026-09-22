#pragma once

#include "trend_buffer.h"

#include <QQuickItem>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlintegration.h>

#include <deque>

class TrendPlotItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QVariantList samples READ samples WRITE setSamples NOTIFY samplesChanged)
    Q_PROPERTY(qreal cursorRatio READ cursorRatio WRITE setCursorRatio NOTIFY cursorRatioChanged)
    Q_PROPERTY(bool cursorVisible READ cursorVisible WRITE setCursorVisible NOTIFY cursorVisibleChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit TrendPlotItem(QQuickItem *parent = nullptr);

    QObject *source() const { return m_source; }
    void setSource(QObject *source);
    QVariantList samples() const { return m_samples; }
    void setSamples(const QVariantList &samples);
    qreal cursorRatio() const { return m_cursorRatio; }
    void setCursorRatio(qreal ratio);
    bool cursorVisible() const { return m_cursorVisible; }
    void setCursorVisible(bool visible);
    int revision() const { return m_revision; }

    Q_INVOKABLE QVariantMap sampleAtRatio(qreal ratio) const;

signals:
    void sourceChanged();
    void samplesChanged();
    void cursorRatioChanged();
    void cursorVisibleChanged();
    void revisionChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode,
                             UpdatePaintNodeData *) override;

private slots:
    void sourceDataChanged();

private:
    const std::deque<TrendPoint> &activePoints() const;
    static std::deque<TrendPoint> decodeSamples(const QVariantList &samples);
    void bumpRevision();

    QObject *m_source = nullptr;
    TrendBuffer *m_buffer = nullptr;
    QVariantList m_samples;
    std::deque<TrendPoint> m_fallbackPoints;
    qreal m_cursorRatio = 0.5;
    bool m_cursorVisible = false;
    int m_revision = 0;
};
