#pragma once
#include <QPointF>
#include <QRectF>
#include <utility>
#include <vector>
#include "scene/image.h"
#include "scene/shape.h"
#include "scene/stroke.h"
#include "scene/text.h"

namespace notes {

class Document
{
public:
    const std::vector<Stroke>& strokes() const { return strokes_; }
    Stroke* strokeById(int id);
    const Stroke* strokeById(int id) const;

    const std::vector<TextBox>& texts() const { return texts_; }
    TextBox* textById(int id);
    const TextBox* textById(int id) const;

    const std::vector<ImageItem>& images() const { return images_; }
    ImageItem* imageById(int id);
    const ImageItem* imageById(int id) const;

    const std::vector<ShapeItem>& shapes() const { return shapes_; }
    ShapeItem* shapeById(int id);
    const ShapeItem* shapeById(int id) const;

    int nextId() { return nextId_++; }
    bool undoAvailable() const { return !undo_.empty(); }
    bool redoAvailable() const { return !redo_.empty(); }

    void add(const Stroke& s);
    void addText(const TextBox& t);
    void addImage(const ImageItem& im);
    void addShape(const ShapeItem& sh);
    void undo();
    void redo();

    void replaceAll(std::vector<Stroke>&& strokes, std::vector<TextBox>&& texts,
                    std::vector<ImageItem>&& images, std::vector<ShapeItem>&& shapes, int nextId);
    void setDirty() { dirty_ = true; }
    bool isDirty() const { return dirty_; }
    void markSaved() { dirty_ = false; }
    int nextIdValue() const { return nextId_; }

    struct EraseResult {
        std::vector<Stroke> strokes;
        std::vector<TextBox> texts;
        std::vector<ImageItem> images;
        std::vector<Stroke> added;
        std::vector<ShapeItem> shapes;
    };
    EraseResult eraseNear(const QPointF& world, float radius);
    EraseResult erasePartial(const QPointF& world, float radius);

    void commitErase(EraseResult&& res);
    void commitAdd(std::vector<Stroke>&& added, std::vector<TextBox>&& addedTexts,
                   std::vector<ImageItem>&& addedImages, std::vector<ShapeItem>&& addedShapes);
    void commitRemove(const std::vector<int>& ids);
    void discardText(int id, bool dirtyBefore);

    std::vector<Stroke> snapshot(const std::vector<int>& ids) const;
    std::vector<TextBox> textSnapshot(const std::vector<int>& ids) const;
    std::vector<ImageItem> imageSnapshot(const std::vector<int>& ids) const;
    std::vector<ShapeItem> shapeSnapshot(const std::vector<int>& ids) const;
    void commitTransform(const std::vector<Stroke>& before, const std::vector<Stroke>& after);
    void commitTextTransform(const std::vector<TextBox>& before, const std::vector<TextBox>& after);
    void commitImageTransform(const std::vector<ImageItem>& before, const std::vector<ImageItem>& after);
    void commitShapeTransform(const std::vector<ShapeItem>& before, const std::vector<ShapeItem>& after);
    void commitTextEdit(const std::vector<TextBox>& before, const std::vector<TextBox>& after);

private:
    struct Action {
        enum class Kind { Add, Remove, Edit };
        Kind kind;
        std::vector<Stroke> added;
        std::vector<Stroke> removed;
        std::vector<TextBox> addedTexts;
        std::vector<TextBox> removedTexts;
        std::vector<ImageItem> addedImages;
        std::vector<ImageItem> removedImages;
        std::vector<ShapeItem> addedShapes;
        std::vector<ShapeItem> removedShapes;
    };
    void apply(const std::vector<Stroke>& list);
    void applyText(const std::vector<TextBox>& list);
    void applyImage(const std::vector<ImageItem>& list);
    void applyShape(const std::vector<ShapeItem>& list);
    void removeById(int id);
    void removeTextById(int id);
    void removeImageById(int id);
    void removeShapeById(int id);

    std::vector<Stroke> strokes_;
    std::vector<TextBox> texts_;
    std::vector<ImageItem> images_;
    std::vector<ShapeItem> shapes_;
    std::vector<Action> undo_, redo_;
    int nextId_ = 1;
    bool dirty_ = false;
};

}
