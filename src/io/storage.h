#pragma once
#include <QByteArray>
#include <vector>
#include "canvas/camera.h"
#include "scene/image.h"
#include "scene/shape.h"
#include "scene/stroke.h"
#include "scene/text.h"

namespace notes {

struct DocumentData {
    std::vector<Stroke> strokes;
    std::vector<TextBox> texts;
    std::vector<ImageItem> images;
    std::vector<ShapeItem> shapes;
    Camera camera;
    int nextId = 1;
};

QByteArray serializeDocument(const DocumentData& data);
bool deserializeDocument(const QByteArray& bytes, DocumentData& out);

}
