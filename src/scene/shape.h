#pragma once
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <vector>
#include "scene/xform.h"

class QPainter;
class QPainterPath;

namespace notes {

enum class ShapeKind { Rectangle, Triangle, Ellipse, Curve };

struct CurveNode {
    QPointF pos;
    QPointF in;
    QPointF out;
};

struct ShapeItem {
    int id = 0;
    ShapeKind kind = ShapeKind::Rectangle;
    QRectF rect;
    QColor color = QColor(0x14, 0x14, 0x1a);
    double penWidth = 3.0;
    double rot = 0.0;
    std::vector<CurveNode> nodes;

    QRectF localRect() const { return rect.normalized(); }
    QPointF anchor() const { return rect.normalized().topLeft(); }
    QRectF bounds() const { return rotatedBounds(rect, rot); }
};

enum class CurvePart { None, Node, HandleIn, HandleOut };

struct CurveHit {
    CurvePart part = CurvePart::None;
    int index = -1;
};

QPointF curveNodeWorld(const ShapeItem& sh, int index);
QPointF curveHandleWorld(const ShapeItem& sh, int index, bool out);
CurveHit curveHit(const ShapeItem& sh, const QPointF& world, double tol);

void curveNormalize(ShapeItem& sh);
void curveBuildFromPoints(const std::vector<QPointF>& pts, ShapeItem& out);
void curveMoveNode(ShapeItem& sh, int index, const QPointF& world);
void curveMoveHandle(ShapeItem& sh, int index, bool out, const QPointF& world);

void curvePath(const ShapeItem& sh, QPainterPath& path);

bool shapeHitTest(const ShapeItem& sh, const QPointF& world, double tol);

void drawShapeItem(QPainter& p, const ShapeItem& sh, float zoom, const QPointF& offset);

}
