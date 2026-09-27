#pragma once
#include <QColor>
#include <QElapsedTimer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QRectF>
#include <vector>
#include "camera.h"
#include "document.h"
#include "image.h"
#include "select.h"
#include "stroke.h"
#include "text.h"

class QImage;
class QKeyEvent;
class QMouseEvent;
class QMimeData;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;
class QTabletEvent;
class QTimer;
class QVariantAnimation;
class QWheelEvent;

namespace notes {

struct EraserPt {
    float x, y, r;
    float born; // tiempo de creación en segundos
};

class Canvas : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    enum class Tool { Pencil, Highlighter, Eraser, Select, Text, Shape };
    enum class EraserMode { Full, Partial };

    explicit Canvas(QWidget* parent = nullptr);

    void setTool(Tool t);
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

    void selectBegin(const QPointF& p);
    void selectMove(const QPointF& p);
    void selectEnd();
    void deleteSelection();
    void beginSelectDrag(const QPointF& p);
    void clearSelection();
    QRectF selectionBox();
    void translateSelection(const QPointF& deltaWorld);
    void scaleSelection(const QPointF& w);
    void drawGizmo();
    void animateGizmoIn();

    void textBegin(const QPointF& p);
    void textKeyPress(QKeyEvent* e);
    void textInsert(const QString& s);
    void textRemovePreedit();
    void finishEditText();

    QOpenGLShaderProgram* prog_ = nullptr;
    QOpenGLShaderProgram* bgProg_ = nullptr;
    QOpenGLShaderProgram* presentProg_ = nullptr;
    GLuint bgVao_ = 0, bgVbo_ = 0;
    GLuint vao_[2]{}, vbo_[2]{};
    GLuint gizVao_ = 0, gizVbo_ = 0;
    GLuint eraserVao_ = 0, eraserVbo_ = 0;
    GLuint presentVao_ = 0, presentVbo_ = 0;
    QOpenGLFramebufferObject* ssFbo_ = nullptr;

    void recreateResolveFbo();
    void dumpDebug();
    static constexpr float kSupersample = 3.0f;

    Camera cam_;
    Document doc_;
    std::vector<float> verts_;
    bool dirty_ = true;

    Stroke cur_;
    bool curDirty_ = false;

    std::vector<Stroke> eraseBatch_;
    std::vector<TextBox> textEraseBatch_;
    std::vector<ImageItem> imageEraseBatch_;
    std::vector<ShapeItem> shapeEraseBatch_;
    std::vector<Stroke> partialAddBatch_;
    std::vector<EraserPt> eraserTrail_;
    std::vector<float> eraserVerts_;
    bool eraserDirty_ = false;
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

    Tool tool_ = Tool::Select;
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

    std::vector<int> selection_;
    QRectF selBoxWorld_;
    bool selBoxValid_ = false;
    bool selectDrag_ = false;
    bool marqueeActive_ = false;
    QPointF marqueeAnchorWorld_;
    QRectF marqueeRectWorld_;
    Handle selHandle_ = Handle::Move;
    QPointF dragLastScreen_;
    QRectF dragStartWorld_;
    QPointF dragAnchorWorld_;
    std::vector<Stroke> beforeSnapshot_;
    std::vector<TextBox> beforeTexts_;
    std::vector<ImageItem> beforeImages_;
    std::vector<ShapeItem> beforeShapes_;
    bool selChanged_ = false;
    float gizmoAlpha_ = 1.0f;
    QVariantAnimation* gizmoAnim_ = nullptr;
};

}