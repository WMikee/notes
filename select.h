#pragma once
#include <QPointF>
#include <QRectF>
#include <vector>
#include "image.h"
#include "shape.h"
#include "stroke.h"
#include "text.h"

namespace notes {

enum class Handle { None, Move, N, NE, E, SE, S, SW, W, NW };

int strokeAt(const std::vector<Stroke>& strokes, const QPointF& world, float tol);
int textAt(const std::vector<TextBox>& texts, const QPointF& world, float tol);
int imageAt(const std::vector<ImageItem>& images, const QPointF& world, float tol);
int shapeAt(const std::vector<ShapeItem>& shapes, const QPointF& world, float tol);

bool strokeHitsRect(const Stroke& s, const QRectF& r);
bool textHitsRect(const TextBox& s, const QRectF& r);
bool imageHitsRect(const ImageItem& im, const QRectF& r);
bool shapeHitsRect(const ShapeItem& sh, const QRectF& r);

QRectF bboxOf(const std::vector<Stroke>& strokes);

QPointF handlePos(const QRectF& r, Handle h);
QPointF handleAnchor(const QRectF& r, Handle h);
Handle hitHandle(const QRectF& r, const QPointF& world, float tol);

}