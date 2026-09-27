#pragma once
#include <QByteArray>
#include <vector>
#include "image.h"
#include "shape.h"
#include "stroke.h"
#include "text.h"

namespace notes {

struct ClipboardData {
    std::vector<Stroke> strokes;
    std::vector<TextBox> texts;
    std::vector<ImageItem> images;
    std::vector<ShapeItem> shapes;
};

QByteArray serializeClipboard(const ClipboardData& data);
bool deserializeClipboard(const QByteArray& bytes, ClipboardData& out);

}