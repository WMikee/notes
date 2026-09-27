#pragma once
#include <QColor>
#include <QPointF>
#include <QRectF>

class QPainter;

namespace notes {

enum class ShapeKind { Rectangle, Triangle, Ellipse };

struct ShapeItem {
    int id = 0;
    ShapeKind kind = ShapeKind::Rectangle;
    QRectF rect; // coordenadas de mundo (top-left + tamaño)
    QColor color = QColor(0x14, 0x14, 0x1a);
    double penWidth = 3.0; // unidades de mundo

    QRectF bounds() const { return rect.normalized(); }
};

bool shapeHitTest(const ShapeItem& sh, const QPointF& world, double tol);

void drawShapeItem(QPainter& p, const ShapeItem& sh, float zoom, const QPointF& offset);

}