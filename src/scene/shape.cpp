#include "scene/shape.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QtMath>
#include <algorithm>
#include <cmath>

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
    case ShapeKind::Curve:
        curvePath(sh, path);
        break;
    }
    return path;
}

} // namespace

void curvePath(const ShapeItem& sh, QPainterPath& path)
{
    const int n = int(sh.nodes.size());
    if (n < 2) return;
    const QPointF a = sh.anchor();
    path.moveTo(a + sh.nodes[0].pos);
    for (int i = 0; i + 1 < n; ++i) {
        const QPointF c1 = a + sh.nodes[i].pos + sh.nodes[i].out;
        const QPointF c2 = a + sh.nodes[i + 1].pos + sh.nodes[i + 1].in;
        const QPointF p2 = a + sh.nodes[i + 1].pos;
        path.cubicTo(c1, c2, p2);
    }
}

QPointF curveNodeWorld(const ShapeItem& sh, int index)
{
    if (index < 0 || index >= int(sh.nodes.size())) return {};
    const QPointF a = sh.anchor();
    return toWorld(a + sh.nodes[index].pos, a, sh.rot);
}

QPointF curveHandleWorld(const ShapeItem& sh, int index, bool out)
{
    if (index < 0 || index >= int(sh.nodes.size())) return {};
    const QPointF a = sh.anchor();
    const QPointF base = a + sh.nodes[index].pos;
    return toWorld(base + (out ? sh.nodes[index].out : sh.nodes[index].in), a, sh.rot);
}

CurveHit curveHit(const ShapeItem& sh, const QPointF& world, double tol)
{
    CurveHit best;
    double bestD = tol;
    for (int i = 0; i < int(sh.nodes.size()); ++i) {
        const QPointF p = curveNodeWorld(sh, i);
        const double d = std::hypot(p.x() - world.x(), p.y() - world.y());
        if (d <= bestD) {
            bestD = d;
            best = CurveHit{CurvePart::Node, i};
        }
    }
    if (best.part != CurvePart::None) return best;

    for (int i = 0; i < int(sh.nodes.size()); ++i) {
        const QPointF p = curveNodeWorld(sh, i);
        for (bool out : {false, true}) {
            const QPointF h = curveHandleWorld(sh, i, out);
            if (h == p) continue;
            if (std::hypot(h.x() - world.x(), h.y() - world.y()) <= tol)
                return CurveHit{out ? CurvePart::HandleOut : CurvePart::HandleIn, i};
        }
    }
    return {};
}

void curveNormalize(ShapeItem& sh)
{
    if (sh.nodes.empty()) return;
    const QPointF base = sh.rect.normalized().topLeft();
    bool first = true;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (const CurveNode& n : sh.nodes) {
        const QPointF abs = base + n.pos;
        const QPointF pts[3] = { abs, abs + n.in, abs + n.out };
        for (const QPointF& p : pts) {
            if (first) {
                minX = maxX = p.x();
                minY = maxY = p.y();
                first = false;
            } else {
                minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
                minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
            }
        }
    }
    const QRectF bounds(QPointF(minX, minY), QPointF(maxX, maxY));
    for (CurveNode& n : sh.nodes)
        n.pos = (base + n.pos) - bounds.topLeft();
    sh.rect = bounds;
}

void curveBuildFromPoints(const std::vector<QPointF>& pts, ShapeItem& out)
{
    out.kind = ShapeKind::Curve;
    out.nodes.assign(pts.size(), CurveNode{});
    const int n = int(pts.size());
    for (int i = 0; i < n; ++i)
        out.nodes[i].pos = pts[i];

    if (n >= 2) {
        out.nodes[0].out = (pts[1] - pts[0]) / 3.0;
        out.nodes[n - 1].in = (pts[n - 2] - pts[n - 1]) / 3.0;
    }
    for (int i = 1; i + 1 < n; ++i) {
        const QPointF t = (pts[i + 1] - pts[i - 1]) / 6.0;
        out.nodes[i].out = t;
        out.nodes[i].in = -t;
    }

    out.rect = QRectF(QPointF(0, 0), QPointF(0, 0));
    curveNormalize(out);
}

void curveMoveNode(ShapeItem& sh, int index, const QPointF& world)
{
    if (index < 0 || index >= int(sh.nodes.size())) return;
    const QPointF a = sh.anchor();
    sh.nodes[index].pos = toLocal(world, a, sh.rot) - a;
    curveNormalize(sh);
}

void curveMoveHandle(ShapeItem& sh, int index, bool out, const QPointF& world)
{
    if (index < 0 || index >= int(sh.nodes.size())) return;
    const QPointF a = sh.anchor();
    const QPointF nodeAbs = a + sh.nodes[index].pos;
    const QPointF offset = toLocal(world, a, sh.rot) - nodeAbs;
    if (out) sh.nodes[index].out = offset;
    else sh.nodes[index].in = offset;
    curveNormalize(sh);
}

bool shapeHitTest(const ShapeItem& sh, const QPointF& world, double tol)
{
    if (sh.kind == ShapeKind::Curve && sh.nodes.size() < 2)
        return false;

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
        case ShapeKind::Curve:
            break;
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
