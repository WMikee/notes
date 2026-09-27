#include "scene/image.h"
#include <QPainter>
#include <QTransform>
#include <QtMath>

namespace notes {

bool imageHitTest(const ImageItem& im, const QPointF& world, double tol)
{
    const QRectF r = im.localRect();
    if (r.isNull()) return false;
    return r.adjusted(-tol, -tol, tol, tol).contains(toLocal(world, im.anchor(), im.rot));
}

void drawImageItem(QPainter& p, const ImageItem& im, float zoom, const QPointF& offset)
{
    if (im.source.isNull()) return;
    const QRectF r = im.localRect();
    if (r.isNull()) return;
    const QRectF dst(r.left() * zoom + offset.x(),
                     r.top() * zoom + offset.y(),
                     r.width() * zoom,
                     r.height() * zoom);
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (im.rot != 0.0) {
        const QPointF a = im.anchor();
        p.translate(a.x() * zoom + offset.x(), a.y() * zoom + offset.y());
        p.rotate(qRadiansToDegrees(im.rot));
        p.translate(-(a.x() * zoom + offset.x()), -(a.y() * zoom + offset.y()));
    }
    p.drawImage(dst.normalized(), im.source);
    p.restore();
}

}
