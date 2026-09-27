#include "scene/select.h"
#include "scene/xform.h"
#include <cmath>
#include <limits>

namespace notes {

namespace {

double cross2(const QPointF& a, const QPointF& b)
{
    return a.x() * b.y() - a.y() * b.x();
}

bool segHitsRect(const QPointF& a, const QPointF& b, const QRectF& r)
{
    if (r.contains(a) || r.contains(b)) return true;
    const QPointF e[4] = {
        QPointF(r.left(), r.top()),
        QPointF(r.right(), r.top()),
        QPointF(r.right(), r.bottom()),
        QPointF(r.left(), r.bottom()),
    };
    for (int i = 0; i < 4; ++i) {
        const QPointF c = e[i], d = e[(i + 1) % 4];
        if (cross2(b - a, c - a) * cross2(b - a, d - a) > 0) continue;
        if (cross2(d - c, a - c) * cross2(d - c, b - c) > 0) continue;
        return true;
    }
    return false;
}

} // namespace

int strokeAt(const std::vector<Stroke>& strokes, const QPointF& world, float tol)
{
    for (auto it = strokes.rbegin(); it != strokes.rend(); ++it) {
        if (distanceTo(*it, float(world.x()), float(world.y())) <= tol)
            return it->id;
    }
    return -1;
}

int textAt(const std::vector<TextBox>& texts, const QPointF& world, float tol)
{
    for (auto it = texts.rbegin(); it != texts.rend(); ++it) {
        if (textHitTest(*it, world, tol))
            return it->id;
    }
    return -1;
}

int imageAt(const std::vector<ImageItem>& images, const QPointF& world, float tol)
{
    for (auto it = images.rbegin(); it != images.rend(); ++it) {
        if (imageHitTest(*it, world, tol))
            return it->id;
    }
    return -1;
}

int shapeAt(const std::vector<ShapeItem>& shapes, const QPointF& world, float tol)
{
    for (auto it = shapes.rbegin(); it != shapes.rend(); ++it) {
        if (shapeHitTest(*it, world, tol))
            return it->id;
    }
    return -1;
}

bool strokeHitsRect(const Stroke& s, const QRectF& r)
{
    if (s.pts.empty()) return false;
    if (r.contains(QPointF(s.pts.front().x, s.pts.front().y))) return true;
    for (size_t i = 1; i < s.pts.size(); ++i) {
        const QPointF a(s.pts[i - 1].x, s.pts[i - 1].y);
        const QPointF b(s.pts[i].x, s.pts[i].y);
        if (segHitsRect(a, b, r)) return true;
    }
    return false;
}

bool textHitsRect(const TextBox& t, const QRectF& r)
{
    return t.rect().intersects(r);
}

bool imageHitsRect(const ImageItem& im, const QRectF& r)
{
    const QRectF rect = im.rect();
    return !rect.isNull() && rect.intersects(r);
}

bool shapeHitsRect(const ShapeItem& sh, const QRectF& r)
{
    const QRectF rect = sh.bounds();
    return !rect.isNull() && rect.intersects(r);
}

QRectF boundsOfPoints(const std::vector<QPointF>& pts)
{
    if (pts.empty()) return QRectF();
    double minX = pts[0].x(), maxX = minX, minY = pts[0].y(), maxY = minY;
    for (const QPointF& p : pts) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

QRectF bboxOf(const std::vector<Stroke>& strokes)
{
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool first = true;
    for (const Stroke& s : strokes) {
        for (const Pt& p : s.pts) {
            if (first) {
                minX = maxX = p.x;
                minY = maxY = p.y;
                first = false;
            } else {
                if (p.x < minX) minX = p.x;
                if (p.x > maxX) maxX = p.x;
                if (p.y < minY) minY = p.y;
                if (p.y > maxY) maxY = p.y;
            }
        }
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

QPointF handlePos(const QRectF& r, Handle h, double rot)
{
    const QRectF n = r.normalized();
    const double x = n.left(), y = n.top();
    const double w = n.width(), ht = n.height();
    QPointF p;
    switch (h) {
    case Handle::N:  p = QPointF(x + w / 2, y); break;
    case Handle::NE: p = QPointF(x + w, y); break;
    case Handle::E:  p = QPointF(x + w, y + ht / 2); break;
    case Handle::SE: p = QPointF(x + w, y + ht); break;
    case Handle::S:  p = QPointF(x + w / 2, y + ht); break;
    case Handle::SW: p = QPointF(x, y + ht); break;
    case Handle::W:  p = QPointF(x, y + ht / 2); break;
    case Handle::NW: p = QPointF(x, y); break;
    default:         p = QPointF(x + w / 2, y + ht / 2); break;
    }
    return rotateAbout(p, n.center(), rot);
}

QPointF handleAnchor(const QRectF& r, Handle h, double rot)
{
    const QRectF n = r.normalized();
    const double x = n.left(), y = n.top();
    const double w = n.width(), ht = n.height();
    QPointF p;
    switch (h) {
    case Handle::N:  p = QPointF(x + w / 2, y + ht); break;
    case Handle::NE: p = QPointF(x, y + ht); break;
    case Handle::E:  p = QPointF(x, y + ht / 2); break;
    case Handle::SE: p = QPointF(x, y); break;
    case Handle::S:  p = QPointF(x + w / 2, y); break;
    case Handle::SW: p = QPointF(x + w, y); break;
    case Handle::W:  p = QPointF(x + w, y + ht / 2); break;
    case Handle::NW: p = QPointF(x + w, y + ht); break;
    default:         p = QPointF(x + w / 2, y + ht / 2); break;
    }
    return rotateAbout(p, n.center(), rot);
}

QPointF rotateHandlePos(const QRectF& r, double rot, double offset)
{
    const QRectF n = r.normalized();
    const QPointF c = n.center();
    QPointF p = handlePos(n, Handle::N);

    p = QPointF(p.x(), p.y() - offset);
    return rotateAbout(p, c, rot);
}

Handle hitHandle(const QRectF& r, const QPointF& world, float tol,
                 double rot, double rotOffset, float rotTol)
{
    if (rotOffset > 0.0) {
        const double rt = rotTol > 0.0 ? rotTol : tol;
        if (std::hypot(rotateHandlePos(r, rot, rotOffset).x() - world.x(),
                       rotateHandlePos(r, rot, rotOffset).y() - world.y()) <= rt)
            return Handle::Rotate;
    }
    const Handle hs[8] = { Handle::N, Handle::NE, Handle::E, Handle::SE,
                           Handle::S, Handle::SW, Handle::W, Handle::NW };
    Handle best = Handle::N;
    double bd = std::numeric_limits<double>::max();
    for (Handle h : hs) {
        const QPointF p = handlePos(r, h, rot);
        const double d = std::hypot(p.x() - world.x(), p.y() - world.y());
        if (d < bd) { bd = d; best = h; }
    }
    return bd <= tol ? best : Handle::None;
}

}
