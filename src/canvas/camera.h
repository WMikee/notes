#pragma once
#include <QRectF>
#include <QPointF>
#include <QSizeF>
#include <QVector2D>

namespace notes {

class Camera
{
public:
    QPointF toWorld(const QPointF& screen) const;

    void panBy(const QPointF& delta);
    void zoomAt(const QPointF& screenAnchor, const QPointF& worldAnchor, double factor);
    void setScale(const QPointF& screenAnchor, const QPointF& worldAnchor, double newScale);

    void setWorldBounds(const QRectF& bounds) { bounds_ = bounds; }
    void setViewSize(const QSizeF& size) { viewSize_ = size; clampOffset(); }
    void centerOnPage();

    QVector2D offset;
    float zoom = 1.0f;

    static constexpr float kMinZoom = 0.1f;
    static constexpr float kMaxZoom = 10.0f;

private:
    static constexpr double kPanMargin = 96.0;

    void clampOffset();

    QRectF bounds_;
    QSizeF viewSize_ = QSizeF(1000, 700);
};

}
