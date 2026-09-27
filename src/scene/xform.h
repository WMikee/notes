#pragma once
#include <QPointF>
#include <QRectF>
#include <algorithm>
#include <cmath>

namespace notes {


inline QPointF rotateAbout(const QPointF& p, const QPointF& c, double rot)
{
    if (rot == 0.0) return p;
    const double s = std::sin(rot), co = std::cos(rot);
    const double dx = p.x() - c.x(), dy = p.y() - c.y();
    return QPointF(c.x() + dx * co - dy * s, c.y() + dx * s + dy * co);
}


inline QPointF toWorld(const QPointF& local, const QPointF& anchor, double rot)
{
    return rotateAbout(local, anchor, rot);
}


inline QPointF toLocal(const QPointF& world, const QPointF& anchor, double rot)
{
    return rotateAbout(world, anchor, -rot);
}


inline QRectF rotatedBounds(const QRectF& r, double rot)
{
    const QRectF n = r.normalized();
    if (rot == 0.0) return n;
    const QPointF o = n.topLeft();
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool first = true;
    const QPointF corners[4] = { n.topLeft(), n.topRight(), n.bottomRight(), n.bottomLeft() };
    for (const QPointF& c : corners) {
        const QPointF w = rotateAbout(c, o, rot);
        if (first) {
            minX = maxX = w.x();
            minY = maxY = w.y();
            first = false;
        } else {
            minX = std::min(minX, w.x());
            minY = std::min(minY, w.y());
            maxX = std::max(maxX, w.x());
            maxY = std::max(maxY, w.y());
        }
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

}
