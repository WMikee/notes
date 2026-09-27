#pragma once
#include <QByteArray>
#include <vector>
#include "camera.h"
#include "image.h"
#include "shape.h"
#include "stroke.h"
#include "text.h"

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