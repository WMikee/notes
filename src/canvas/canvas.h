#pragma once
#include <QColor>
#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QRectF>
#include <vector>
#include "canvas/camera.h"
#include "canvas/glrenderer.h"
#include "tools/selecttool.h"
#include "tools/tool.h"
#include "scene/document.h"
#include "scene/select.h"
#include "scene/text.h"

class QImage;
class QKeyEvent;
class QMouseEvent;
class QMimeData;
class QTabletEvent;
class QTimer;
class QVariantAnimation;
class QWheelEvent;

namespace notes {

class Canvas : public QOpenGLWidget
{
    Q_OBJECT
public:
    enum class ToolId { Pencil, Highlighter, Eraser, Select, Text, Shape };
    enum class EraserMode { Full, Partial };

    explicit Canvas(QWidget* parent = nullptr);

    void setTool(ToolId t);
    void setEraserMode(EraserMode m);
    void setShapeKind(ShapeKind k);
    void setColor(const QColor& c);
    void setPressureEnabled(bool on);
    void setStrokeSize(float size);
    void setEraserRadius(float radius);
    void setFontSize(float px);
    void setShapePenWidth(float width);
    QColor color() const { return color_; }
    float strokeSize() const { return size_; }
    void undo();
    void redo();
    void copy();
    void cut();
    void paste();
    void insertImage(const QImage& image);
    void insertImageAt(const QImage& image, const QPointF& worldPos, double width = 0, double height = 0);
    QByteArray serializeState() const;
    bool loadState(const QByteArray& bytes);
    void newDocument();
    bool isDirty() const { return doc_.isDirty(); }
    void markSaved() { doc_.markSaved(); }

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void tabletEvent(QTabletEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void inputMethodEvent(QInputMethodEvent* e) override;

private:
    enum class Gesture { None, Draw, Pan, Zoom, Select };

    float pressureValue(float p) const;
    void updateCursor();
    void beginZoom(const QPointF& p);
    void panStylus(const QPointF& p);
    void zoomStylus(const QPointF& p);

    void beginStroke(const QPointF& p, float pr);
    void addPoint(const QPointF& p, float pr);
    void endStroke();
    void beginErase(const QPointF& p);
    void eraseAt(const QPointF& p);
    void endErase();
    void beginShape(const QPointF& p);
    void updateShape(const QPointF& p);
    void endShape();
    void finishInput();
    void imageMimeDataPaste(const QMimeData& mime);

    void deleteSelection();
    void animateGizmoIn();
    void resetGizmo();
    ToolContext& ctx() { return toolCtx_; }

    void textBegin(const QPointF& p);
    void textKeyPress(QKeyEvent* e);
    void textInsert(const QString& s);
    void textRemovePreedit();
    void finishEditText();

    GLRenderer renderer_;
    SelectTool* selTool_ = nullptr;
    Camera cam_;
    Document doc_;
    ToolContext toolCtx_;

    Stroke cur_;
    std::vector<Stroke> eraseBatch_;
    std::vector<TextBox> textEraseBatch_;
    std::vector<ImageItem> imageEraseBatch_;
    std::vector<ShapeItem> shapeEraseBatch_;
    std::vector<Stroke> partialAddBatch_;
    std::vector<EraserPt> eraserTrail_;
    QElapsedTimer eraseClock_;
    QTimer* eraseTimer_ = nullptr;
    QElapsedTimer strokeTessClock_;

    int editingText_ = -1;
    int textCursor_ = 0;
    bool textEditJustStarted_ = false;
    bool textDirtyBefore_ = false;
    TextBox textEditBefore_;
    int preeditStart_ = -1;
    int preeditLen_ = 0;

    ToolId tool_ = ToolId::Select;
    EraserMode eraserMode_ = EraserMode::Full;
    ShapeKind shapeKind_ = ShapeKind::Rectangle;
    QColor color_ = QColor(0.08f, 0.08f, 0.10f);
    bool pressureEnabled_ = true;
    float size_ = kDefaultStrokeSize;
    float fontSize_ = 16.0f;
    float shapePenWidth_ = 3.0f;
    QPointF panLast_;
    bool drawing_ = false;
    bool erasing_ = false;
    bool panning_ = false;
    float eraseRadius_ = 3.5f;
    bool shapeDragging_ = false;
    QPointF shapeAnchor_;
    ShapeItem dragShape_;

    bool penDown_ = false;
    bool spaceDown_ = false;
    bool zDown_ = false;
    bool shiftDown_ = false;
    QPointF penLast_;
    QPointF zoomAnchorPos_;
    QPointF zoomAnchorWorld_;
    double zoomStartScale_ = 1.0;
    Gesture stylusMode_ = Gesture::None;

    float gizmoAlpha_ = 1.0f;
    QVariantAnimation* gizmoAnim_ = nullptr;
};

}
