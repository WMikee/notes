#include "image.h"
#include <QPainter>

namespace notes {

bool imageHitTest(const ImageItem& im, const QPointF& world, double tol)
{
    const QRectF r = im.rect();
    if (r.isNull()) return false;
    return r.adjusted(-tol, -tol, tol, tol).contains(world);
}

void drawImageItem(QPainter& p, const ImageItem& im, float zoom, const QPointF& offset)
{
    if (im.source.isNull()) return;
    const QRectF r = im.rect();
    if (r.isNull()) return;
    const QRectF dst(r.left() * zoom + offset.x(),
                     r.top() * zoom + offset.y(),
                     r.width() * zoom,
                     r.height() * zoom);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(dst.normalized(), im.source);
}

}