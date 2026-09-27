#pragma once
#include <QObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QRectF>
#include <vector>
#include "canvas/camera.h"
#include "scene/image.h"
#include "scene/shape.h"
#include "scene/stroke.h"
#include "scene/text.h"

class QImage;
class QPainter;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;
class QSurfaceFormat;

namespace notes {

struct EraserPt {
    float x, y, r;
    float born;
};


struct Frame {
    Frame(const Camera& camera,
          const std::vector<Stroke>& strokeList,
          const std::vector<ShapeItem>& shapeList,
          const std::vector<ImageItem>& imageList,
          const std::vector<TextBox>& textList)
        : cam(camera), strokes(strokeList), shapes(shapeList),
          images(imageList), texts(textList) {}

    const Camera& cam;
    const std::vector<Stroke>& strokes;
    const std::vector<ShapeItem>& shapes;
    const std::vector<ImageItem>& images;
    const std::vector<TextBox>& texts;

    const Stroke* current = nullptr;
    const ShapeItem* dragShape = nullptr;
    const std::vector<EraserPt>* eraserTrail = nullptr;
    float eraseNow = 0.0f;

    QRectF selectionBox;
    float selectionRot = 0.0f;
    bool hasSelection = false;
    QRectF marquee;
    bool marqueeActive = false;
    float gizmoAlpha = 1.0f;

    int editingTextId = -1;
    int textCursor = 0;
    int preeditStart = -1;
    int preeditLen = 0;

    int viewW = 0;
    int viewH = 0;
    qreal dpr = 1.0;
    GLuint defaultFbo = 0;
};


void pruneEraserTrail(std::vector<EraserPt>& trail, float now);

class GLRenderer : public QObject, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit GLRenderer(QObject* parent = nullptr);
    ~GLRenderer() override;

    void initialize(const QSurfaceFormat& format);
    void resize(int w, int h, qreal dpr);
    void paint(const Frame& f);
    void setPresentMode(int m) { presentMode_ = m; }
    int presentMode() const { return presentMode_; }
    void setAlphaMask(bool on);
    bool alphaMask() const { return alphaMaskOn_; }
    static void paintOverlay(QPainter& p, const Frame& f);

    void invalidateStrokes() { strokesDirty_ = true; }
    void invalidateCurrent() { currentDirty_ = true; }
    void invalidateEraser() { eraserDirty_ = true; }

private:
    static constexpr float kSupersample = 3.0f;


    QSize ssSizeFor(int w, int h, qreal dpr) const;
    void recreateFbo(int w, int h, qreal dpr);
    void drawStrokes(const Frame& f);
    void drawEraser(const Frame& f);
    void drawGizmo(const Frame& f);
    void present(const Frame& f);

    QOpenGLShaderProgram* prog_ = nullptr;
    QOpenGLShaderProgram* bgProg_ = nullptr;
    QOpenGLShaderProgram* presentProg_ = nullptr;
    GLuint bgVao_ = 0, bgVbo_ = 0;
    GLuint vao_[2]{}, vbo_[2]{};
    GLuint gizVao_ = 0, gizVbo_ = 0;
    GLuint eraserVao_ = 0, eraserVbo_ = 0;
    GLuint presentVao_ = 0, presentVbo_ = 0;
    QOpenGLFramebufferObject* ssFbo_ = nullptr;
    QSize lastFboSize_;
    GLsizei glMaxTexture_ = 0;
    GLsizei glMaxRenderbuffer_ = 0;
    float ssFactor_ = kSupersample;
    bool fboHasDepth_ = false;
    bool warnedNoFbo_ = false;
    bool fboStale_ = true;
    bool noSsaa_ = false;
    bool alphaMaskOn_ = true;
    int presentMode_ = 0;
    int resizeW_ = 0, resizeH_ = 0;
    qreal resizeDpr_ = 1.0;

    std::vector<float> vertsScratch_;
    std::vector<float> eraserVerts_;
    bool strokesDirty_ = true;
    bool currentDirty_ = false;
    bool eraserDirty_ = false;
    bool depthCleared_ = false;
    bool initialized_ = false;
};

}
