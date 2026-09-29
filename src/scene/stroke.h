#pragma once
#include <QColor>
#include <QPointF>
#include <vector>

namespace notes {

struct Pt { float x, y, p; };

constexpr float kDefaultStrokeSize = 12.0f;

inline constexpr int kOutlineCapSegments = 64;
inline constexpr int kOutlineCornerSegments = 32;

struct Stroke {
    int id = 0;
    std::vector<Pt> pts;
    std::vector<float> verts;
    QColor color;
    float size = kDefaultStrokeSize;
    bool complete = false;
    bool stabilized = true;
};

struct StrokeOutline {
    std::vector<QPointF> body;
    std::vector<QPointF> startCap;
    std::vector<QPointF> endCap;
    bool empty() const { return body.size() < 2; }
};

void tessellate(const std::vector<Pt>& pts, const QColor& color, std::vector<float>& out);
void retessellate(Stroke& s);

void smoothStrokePoints(std::vector<Pt>& pts);

StrokeOutline strokeOutline(const Stroke& s, int capSegments = kOutlineCapSegments,
                            int cornerSegments = kOutlineCornerSegments);

float distanceTo(const Stroke& s, float x, float y);

bool hitTest(const Stroke& s, float x, float y, float threshold);

}
