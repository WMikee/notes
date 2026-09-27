#pragma once
#include <QByteArray>
#include <vector>
#include "scene/image.h"
#include "scene/shape.h"
#include "scene/stroke.h"
#include "scene/text.h"

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
