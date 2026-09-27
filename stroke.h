#pragma once
#include <QColor>
#include <vector>

namespace notes {

struct Pt { float x, y, p; };

constexpr float kDefaultStrokeSize = 12.0f;

struct Stroke {
    int id = 0;
    std::vector<Pt> pts;
    std::vector<float> verts;
    QColor color;
    float size = kDefaultStrokeSize;
    bool complete = false;
};

void tessellate(const std::vector<Pt>& pts, const QColor& color, std::vector<float>& out);
void retessellate(Stroke& s);

float distanceTo(const Stroke& s, float x, float y);

bool hitTest(const Stroke& s, float x, float y, float threshold);

}