#include "scene/shape.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QtMath>
#include <algorithm>

namespace notes {

namespace {

double signOf(const QPointF& a, const QPointF& b, const QPointF& p)
{
    return (p.x() - b.x()) * (a.y() - b.y()) - (a.x() - b.x()) * (p.y() - b.y());
}

bool pointInTriangle(const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c)
{
    const double d1 = signOf(a, b, p);
    const double d2 = signOf(b, c, p);
    const double d3 = signOf(c, a, p);
    const bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

bool pointInEllipse(const QPointF& p, const QRectF& r)
{
    const double rx = r.width() * 0.5, ry = r.height() * 0.5;
    if (rx <= 0 || ry <= 0) return false;
    const double dx = (p.x() - r.center().x()) / rx;
    const double dy = (p.y() - r.center().y()) / ry;
    return dx * dx + dy * dy <= 1.0;
}

QPainterPath makePath(const ShapeItem& sh)
{
    QPainterPath path;
    const QRectF r = sh.rect.normalized();
    switch (sh.kind) {
    case ShapeKind::Rectangle:
        path.addRect(r);
        break;
    case ShapeKind::Triangle: {
        QPolygonF poly;
        poly << QPointF(r.x() + r.width() / 2.0, r.y())
             << QPointF(r.x() + r.width(), r.y() + r.height())
             << QPointF(r.x(), r.y() + r.height());
        path.addPolygon(poly);
        path.closeSubpath();
        break;
    }
    case ShapeKind::Ellipse:
        path.addEllipse(r);
        break;
    }
    return path;
}

} // namespace

bool shapeHitTest(const ShapeItem& sh, const QPointF& world, double tol)
{
    const QRectF r = sh.rect.normalized();
    if (r.isNull()) return false;


    const QPointF world2 = toLocal(world, sh.anchor(), sh.rot);
    if (r.adjusted(-tol, -tol, tol, tol).contains(world2)) {
        switch (sh.kind) {
        case ShapeKind::Rectangle:
            return true;
        case ShapeKind::Triangle:
            return pointInTriangle(world2,
                QPointF(r.x() + r.width() / 2.0, r.y()),
                QPointF(r.x() + r.width(), r.y() + r.height()),
                QPointF(r.x(), r.y() + r.height()));
        case ShapeKind::Ellipse:
            return pointInEllipse(world2, r);
        }
    }
    if (tol <= 0) return false;

    const QPainterPath path = makePath(sh);
    const int count = qMax(24, int(std::ceil(path.length() / 6.0)));
    double best = 1e18;
    for (int i = 0; i < count; ++i) {
        const qreal t0 = qreal(i) / count;
        const qreal t1 = qreal(i + 1) / count;
        const QPointF p0 = toWorld(path.pointAtPercent(t0), sh.anchor(), sh.rot);
        const QPointF p1 = toWorld(path.pointAtPercent(t1), sh.anchor(), sh.rot);
        const QPointF d = p1 - p0;
        const qreal len = d.x() * d.x() + d.y() * d.y();
        if (len <= 0) continue;
        const qreal t = qBound(0.0, ((world.x() - p0.x()) * d.x()
                                   + (world.y() - p0.y()) * d.y()) / len, 1.0);
        const QPointF nearest = p0 + d * t;
        const double dist = std::hypot(world.x() - nearest.x(), world.y() - nearest.y());
        if (dist < best) best = dist;
    }
    return best <= tol;
}

void drawShapeItem(QPainter& p, const ShapeItem& sh, float zoom, const QPointF& offset)
{
    const QRectF r = sh.rect.normalized();
    if (r.isNull()) return;
    const QRectF dst(r.left() * zoom + offset.x(),
                     r.top() * zoom + offset.y(),
                     r.width() * zoom,
                     r.height() * zoom);
    if (dst.width() < 0.5 && dst.height() < 0.5) return;

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(sh.color, qMax(1.0, sh.penWidth * zoom), Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));

    QPainterPath path = makePath(sh);
    QTransform m;
    m.translate(offset.x(), offset.y());
    m.scale(zoom, zoom);
    if (sh.rot != 0.0) {
        const QPointF a = sh.anchor();
        m.translate(a.x(), a.y());
        m.rotate(qRadiansToDegrees(sh.rot));
        m.translate(-a.x(), -a.y());
    }
    p.drawPath(m.map(path));
}

}
