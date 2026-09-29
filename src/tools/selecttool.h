#pragma once
#include <QObject>
#include <QRectF>
#include <vector>
#include "scene/image.h"
#include "scene/select.h"
#include "scene/shape.h"
#include "scene/stroke.h"
#include "scene/text.h"
#include "tools/tool.h"

namespace notes {


class SelectTool : public QObject, public Tool
{
    Q_OBJECT
public:
    explicit SelectTool(QObject* parent = nullptr);

    void onPress(const InputPoint& p, ToolContext& ctx) override;
    void onMove(const InputPoint& p, ToolContext& ctx) override;
    void onRelease(const InputPoint& p, ToolContext& ctx) override;
    void finish(ToolContext& ctx) override;

    const std::vector<int>& selection() const { return selection_; }
    void setSelection(std::vector<int> ids, const Document& doc);
    void clear(ToolContext& ctx);
    void forget(int id);
    void deleteSelected(ToolContext& ctx);

    QRectF gizmoBox(ToolContext& ctx);
    double gizmoRot() const { return boxRot_; }
    double rotateOffset() const;
    void invalidateBox() { boxValid_ = false; }

    bool dragging() const { return drag_; }
    bool marqueeActive() const { return marqueeActive_; }
    const QRectF& marqueeRect() const { return marqueeRect_; }

signals:
    void gizmoAppeared();
    void gizmoCleared();

private:
    void beginDrag(const QPointF& screen, ToolContext& ctx);
    void translate(const QPointF& deltaWorld, ToolContext& ctx);
    void scale(const QPointF& world, bool shift, ToolContext& ctx);
    void rotate(const QPointF& world, bool snap, ToolContext& ctx);
    void endMarquee(ToolContext& ctx);
    void commitDrag(ToolContext& ctx);
    void collectSnapshots(Document& doc);
    void forgetSnapshots();
    static void collect(const Document& doc, const QRectF& world, std::vector<int>& out);

    std::vector<int> selection_;
    QRectF boxWorld_;
    double boxRot_ = 0.0;
    bool boxValid_ = false;
    bool drag_ = false;
    bool marqueeActive_ = false;
    bool marqueeAdd_ = false;
    QPointF marqueeAnchorWorld_;
    QRectF marqueeRect_;
    Handle handle_ = Handle::Move;
    QPointF dragLastScreen_;
    QRectF dragStartWorld_;
    QPointF dragAnchorWorld_;
    double dragStartRot_ = 0.0;
    double dragStartAngle_ = 0.0;
    std::vector<Stroke> beforeStrokes_;
    std::vector<TextBox> beforeTexts_;
    std::vector<ImageItem> beforeImages_;
    std::vector<ShapeItem> beforeShapes_;
    std::vector<Stroke*> dragStrokes_;
    std::vector<TextBox*> dragTexts_;
    std::vector<ImageItem*> dragImages_;
    std::vector<ShapeItem*> dragShapes_;
    std::vector<QPointF> scratchPts_;
    double appliedRot_ = 0.0;
    bool moved_ = false;
};

}
