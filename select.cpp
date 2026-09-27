#include "select.h"
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
    const QRectF rect = sh.rect.normalized();
    return !rect.isNull() && rect.intersects(r);
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

QPointF handlePos(const QRectF& r, Handle h)
{
    const double x = r.left(), y = r.top();
    const double w = r.width(), ht = r.height();
    switch (h) {
    case Handle::N:  return QPointF(x + w / 2, y);
    case Handle::NE: return QPointF(x + w, y);
    case Handle::E:  return QPointF(x + w, y + ht / 2);
    case Handle::SE: return QPointF(x + w, y + ht);
    case Handle::S:  return QPointF(x + w / 2, y + ht);
    case Handle::SW: return QPointF(x, y + ht);
    case Handle::W:  return QPointF(x, y + ht / 2);
    case Handle::NW: return QPointF(x, y);
    default:         return QPointF(x + w / 2, y + ht / 2);
    }
}

QPointF handleAnchor(const QRectF& r, Handle h)
{
    const double x = r.left(), y = r.top();
    const double w = r.width(), ht = r.height();
    switch (h) {
    case Handle::N:  return QPointF(x + w / 2, y + ht);
    case Handle::NE: return QPointF(x, y + ht);
    case Handle::E:  return QPointF(x, y + ht / 2);
    case Handle::SE: return QPointF(x, y);
    case Handle::S:  return QPointF(x + w / 2, y);
    case Handle::SW: return QPointF(x + w, y);
    case Handle::W:  return QPointF(x + w, y + ht / 2);
    case Handle::NW: return QPointF(x + w, y + ht);
    default:         return QPointF(x + w / 2, y + ht / 2);
    }
}

Handle hitHandle(const QRectF& r, const QPointF& world, float tol)
{
    const Handle hs[8] = { Handle::N, Handle::NE, Handle::E, Handle::SE,
                           Handle::S, Handle::SW, Handle::W, Handle::NW };
    Handle best = Handle::N;
    double bd = std::numeric_limits<double>::max();
    for (Handle h : hs) {
        const QPointF p = handlePos(r, h);
        const double d = std::hypot(p.x() - world.x(), p.y() - world.y());
        if (d < bd) { bd = d; best = h; }
    }
    return bd <= tol ? best : Handle::None;
}

}