#pragma once
#include <QPointF>
#include <QRectF>
#include <vector>
#include "scene/image.h"
#include "scene/shape.h"
#include "scene/stroke.h"
#include "scene/text.h"

namespace notes {

enum class Handle { None, Move, Rotate, N, NE, E, SE, S, SW, W, NW };


constexpr double kRotateOffsetPx = 26.0;

int strokeAt(const std::vector<Stroke>& strokes, const QPointF& world, float tol);
int textAt(const std::vector<TextBox>& texts, const QPointF& world, float tol);
int imageAt(const std::vector<ImageItem>& images, const QPointF& world, float tol);
int shapeAt(const std::vector<ShapeItem>& shapes, const QPointF& world, float tol);

bool strokeHitsRect(const Stroke& s, const QRectF& r);
bool textHitsRect(const TextBox& s, const QRectF& r);
bool imageHitsRect(const ImageItem& im, const QRectF& r);
bool shapeHitsRect(const ShapeItem& sh, const QRectF& r);

QRectF bboxOf(const std::vector<Stroke>& strokes);


QRectF boundsOfPoints(const std::vector<QPointF>& pts);


QPointF handlePos(const QRectF& r, Handle h, double rot = 0.0);
QPointF handleAnchor(const QRectF& r, Handle h, double rot = 0.0);
QPointF rotateHandlePos(const QRectF& r, double rot, double offset);
Handle hitHandle(const QRectF& r, const QPointF& world, float tol,
                 double rot = 0.0, double rotOffset = 0.0, float rotTol = 0.0);

}
