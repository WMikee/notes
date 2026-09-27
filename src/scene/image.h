#pragma once
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include "scene/xform.h"

class QPainter;

namespace notes {

struct ImageItem {
    int id = 0;
    QImage source;
    QPointF pos;
    double width = 0.0;
    double height = 0.0;
    double rot = 0.0;

    QRectF localRect() const
    {
        if (width <= 0.0 || height <= 0.0)
            return QRectF(pos, QSizeF(source.width(), source.height()));
        return QRectF(pos, QSizeF(width, height));
    }

    QPointF anchor() const { return pos; }
    QRectF rect() const { return rotatedBounds(localRect(), rot); }
};

bool imageHitTest(const ImageItem& im, const QPointF& world, double tol);

void drawImageItem(QPainter& p, const ImageItem& im, float zoom, const QPointF& offset);

}
