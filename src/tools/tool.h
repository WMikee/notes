#pragma once
#include <QPointF>
#include <functional>
#include "canvas/camera.h"
#include "scene/document.h"

namespace notes {

struct InputPoint {
    QPointF pos;
    float pressure = 0.5f;
    bool shift = false;
};

struct ToolContext {
    ToolContext(Document& document, Camera& camera,
                std::function<void()> invalidateStrokes,
                std::function<void()> requestRepaint)
        : doc(document), cam(camera),
          invalidate(std::move(invalidateStrokes)),
          repaint(std::move(requestRepaint)) {}

    Document& doc;
    Camera& cam;
    std::function<void()> invalidate;
    std::function<void()> repaint;
};

class Tool {
public:
    virtual ~Tool() = default;

    virtual void onPress(const InputPoint& p, ToolContext& ctx) = 0;
    virtual void onMove(const InputPoint& p, ToolContext& ctx) = 0;
    virtual void onRelease(const InputPoint& p, ToolContext& ctx) = 0;
    virtual void finish(ToolContext& ctx) { (void)ctx; }
};

}
