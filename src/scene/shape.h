#pragma once
#include <QColor>
#include <QPointF>
#include <QRectF>
#include "scene/xform.h"

class QPainter;

namespace notes {

enum class ShapeKind { Rectangle, Triangle, Ellipse };

struct ShapeItem {
    int id = 0;
    ShapeKind kind = ShapeKind::Rectangle;
    QRectF rect;
    QColor color = QColor(0x14, 0x14, 0x1a);
    double penWidth = 3.0;
    double rot = 0.0;

    QRectF localRect() const { return rect.normalized(); }
    QPointF anchor() const { return rect.normalized().topLeft(); }
    QRectF bounds() const { return rotatedBounds(rect, rot); }
};

bool shapeHitTest(const ShapeItem& sh, const QPointF& world, double tol);

void drawShapeItem(QPainter& p, const ShapeItem& sh, float zoom, const QPointF& offset);

}
