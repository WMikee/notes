#include "canvas.h"
#include "anim.h"
#include "clipboard.h"
#include "storage.h"
#include "theme.h"
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QTabletEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QVector3D>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr float kHandleTolPx = 10.0f;
constexpr float kClickTolPx = 6.0f;
constexpr float kGrabPx = 6.0f;

constexpr float kEraseLife = 0.6f;           
constexpr float kEraseHeadAlpha = 0.35f;     

constexpr float kHighlightAlpha = 0.45f;     

constexpr double kPageWidth = 5760.0;  
constexpr double kPageHeight = 8160.0; 
constexpr double kPageMargin = 192.0;     
constexpr double kPageCornerRadius = 0.0;

QCursor pencilCursor()
{
    QPixmap pm(QStringLiteral(":/assets/pencil-cursor.png"));
    if (pm.isNull()) return QCursor(Qt::CrossCursor);
    return QCursor(pm, 4, pm.height() - 4);  // hotspot: 4 px de la izquierda y 4 de abajo
}

void pushDisc(std::vector<float>& v, double cx, double cy, double r, const QColor& c, float alpha, int segs)
{
    const float rr = c.redF(), g = c.greenF(), b = c.blueF(), a = c.alphaF() * alpha;
    for (int k = 0; k < segs; ++k) {
        const double a0 = 2.0 * 3.14159265358979323846 * k / segs;
        const double a1 = 2.0 * 3.14159265358979323846 * (k + 1) / segs;
        v.insert(v.end(), {float(cx), float(cy), rr, g, b, a,
                           float(cx + r * std::cos(a0)), float(cy + r * std::sin(a0)), rr, g, b, a,
                           float(cx + r * std::cos(a1)), float(cy + r * std::sin(a1)), rr, g, b, a});
    }
}
}

namespace notes {

Canvas::Canvas(QWidget* parent) : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    updateCursor();
    cam_.setWorldBounds(QRectF(0.0, 0.0, kPageWidth + 2.0 * kPageMargin,
                               kPageHeight + 2.0 * kPageMargin));
    cam_.zoom = 1.0f;
    cam_.centerOnPage();
    eraseClock_.start();
    eraseTimer_ = new QTimer(this);
    eraseTimer_->setInterval(16);
    connect(eraseTimer_, &QTimer::timeout, this, [this] {
        eraserDirty_ = true;
        update();
    });
}

void Canvas::setTool(Tool t)
{
    if (tool_ == t) return;
    finishInput();
    tool_ = t;
    updateCursor();
}

void Canvas::updateCursor()
{
    switch (tool_) {
    case Tool::Pencil: setCursor(pencilCursor()); break;
    case Tool::Highlighter: setCursor(pencilCursor()); break;
    case Tool::Eraser: setCursor(pencilCursor()); break;
    case Tool::Shape:  setCursor(pencilCursor()); break;
    case Tool::Select: setCursor(Qt::ArrowCursor); break;
    case Tool::Text:   setCursor(Qt::IBeamCursor); break;
    }
}

void Canvas::setEraserMode(EraserMode m)
{
    if (eraserMode_ == m) return;
    if (tool_ == Tool::Eraser) finishInput();
    eraserMode_ = m;
    eraserDirty_ = true;
    update();
}

void Canvas::setShapeKind(ShapeKind k)
{
    if (shapeKind_ == k) return;
    if (tool_ == Tool::Shape) finishInput();
    shapeKind_ = k;
    update();
}

void Canvas::setColor(const QColor& c)
{
    if (color_ == c) return;
    finishInput();
    color_ = c;
}

void Canvas::setPressureEnabled(bool on)
{
    if (pressureEnabled_ == on) return;
    pressureEnabled_ = on;
}

void Canvas::setStrokeSize(float size)
{
    size_ = std::clamp(size, 0.5f, 200.0f);
}

void Canvas::setEraserRadius(float radius)
{
    eraseRadius_ = std::clamp(radius, 0.5f, 200.0f);
    eraserDirty_ = true;
    update();
}

void Canvas::setFontSize(float px)
{
    fontSize_ = std::clamp(px, 4.0f, 400.0f);
    if (editingText_ >= 0) {
        if (TextBox* tb = doc_.textById(editingText_)) {
            tb->fontPx = fontSize_;
            tb->layoutDirty = true;
            update();
        }
        return;
    }
    std::vector<TextBox> before, after;
    for (int id : selection_) {
        TextBox* tb = doc_.textById(id);
        if (!tb) continue;
        before.push_back(*tb);
        tb->fontPx = fontSize_;
        tb->layoutDirty = true;
        after.push_back(*tb);
    }
    if (!after.empty()) {
        doc_.commitTextTransform(before, after);
        dirty_ = true;
        update();
    }
}

void Canvas::setShapePenWidth(float width)
{
    shapePenWidth_ = std::clamp(width, 0.5f, 200.0f);
    if (shapeDragging_) {
        dragShape_.penWidth = shapePenWidth_;
        update();
        return;
    }
    std::vector<ShapeItem> before, after;
    for (int id : selection_) {
        ShapeItem* sh = doc_.shapeById(id);
        if (!sh) continue;
        before.push_back(*sh);
        sh->penWidth = shapePenWidth_;
        after.push_back(*sh);
    }
    if (!after.empty()) {
        doc_.commitShapeTransform(before, after);
        dirty_ = true;
        update();
    }
}

void Canvas::undo()
{
    if (editingText_ >= 0) finishEditText();
    doc_.undo();
    dirty_ = true;
    update();
}

void Canvas::redo()
{
    if (editingText_ >= 0) finishEditText();
    doc_.redo();
    dirty_ = true;
    update();
}

void Canvas::copy()
{
    if (selection_.empty()) return;
    if (editingText_ >= 0) finishEditText();
    ClipboardData data;
    data.strokes = doc_.snapshot(selection_);
    data.texts = doc_.textSnapshot(selection_);
    data.images = doc_.imageSnapshot(selection_);
    data.shapes = doc_.shapeSnapshot(selection_);
    if (data.strokes.empty() && data.texts.empty() && data.images.empty() && data.shapes.empty()) return;
    QMimeData* mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-notes"), serializeClipboard(data));
    QGuiApplication::clipboard()->setMimeData(mime);
}

void Canvas::cut()
{
    if (selection_.empty()) return;
    copy();
    doc_.commitRemove(selection_);
    clearSelection();
    dirty_ = true;
    update();
}

void Canvas::imageMimeDataPaste(const QMimeData& mime)
{
    if (mime.hasFormat(QStringLiteral("application/x-notes"))) {
        ClipboardData data;
        if (!deserializeClipboard(mime.data(QStringLiteral("application/x-notes")), data)) return;
        if (data.strokes.empty() && data.texts.empty() && data.images.empty()
            && data.shapes.empty()) return;

        QRectF box;
        bool first = true;
        const auto grow = [&](double x, double y) {
            const QPointF p(x, y);
            if (first) { box = QRectF(p, p); first = false; }
            else box = box.united(QRectF(p, p));
        };
        for (const Stroke& s : data.strokes)
            for (const Pt& p : s.pts) grow(p.x, p.y);
        for (const TextBox& t : data.texts) {
            grow(t.pos.x(), t.pos.y());
            grow(t.pos.x() + t.width, t.pos.y() + t.height);
        }
        for (const ImageItem& im : data.images) {
            const QRectF r = im.rect();
            grow(r.left(), r.top());
            grow(r.right(), r.bottom());
        }
        for (const ShapeItem& sh : data.shapes) {
            const QRectF r = sh.rect;
            grow(r.left(), r.top());
            grow(r.right(), r.bottom());
        }
        const QPointF viewCenter = cam_.toWorld(QPointF(width() / 2.0, height() / 2.0));
        const QPointF delta = first
            ? QPointF(0, 0)
            : (viewCenter - box.center());

        selection_.clear();
        std::vector<Stroke> added;
        std::vector<TextBox> addedTexts;
        std::vector<ImageItem> addedImages;
        std::vector<ShapeItem> addedShapes;
        added.reserve(data.strokes.size());
        for (Stroke s : data.strokes) {
            s.id = doc_.nextId();
            for (Pt& p : s.pts) { p.x += float(delta.x()); p.y += float(delta.y()); }
            s.verts.clear();
            retessellate(s);
            selection_.push_back(s.id);
            added.push_back(std::move(s));
        }
        for (TextBox t : data.texts) {
            t.id = doc_.nextId();
            t.pos += delta;
            t.layoutDirty = true;
            selection_.push_back(t.id);
            addedTexts.push_back(std::move(t));
        }
        for (ImageItem im : data.images) {
            im.id = doc_.nextId();
            im.pos += delta;
            selection_.push_back(im.id);
            addedImages.push_back(std::move(im));
        }
        for (ShapeItem sh : data.shapes) {
            sh.id = doc_.nextId();
            sh.rect.translate(delta);
            selection_.push_back(sh.id);
            addedShapes.push_back(std::move(sh));
        }
doc_.commitAdd(std::move(added), std::move(addedTexts),
                   std::move(addedImages), std::move(addedShapes));
        selBoxValid_ = false;
        dirty_ = true;
        animateGizmoIn();
        update();
        return;
    }

    if (mime.hasImage()) {
        const QImage img = mime.imageData().value<QImage>();
        if (!img.isNull())
            insertImage(img);
    }
}

void Canvas::paste()
{
    if (editingText_ >= 0) finishEditText();
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (mime) imageMimeDataPaste(*mime);
}

void Canvas::insertImageAt(const QImage& image, const QPointF& worldPos,
                           double width, double height)
{
    if (image.isNull()) return;
    if (editingText_ >= 0) finishEditText();
    ImageItem im;
    im.id = doc_.nextId();
    im.source = image;
    im.pos = worldPos;
    im.width = width;
    im.height = height;
    selection_ = { im.id };
    doc_.commitAdd({}, {}, {im}, {});
    selBoxValid_ = false;
    dirty_ = true;
    animateGizmoIn();
    update();
}

void Canvas::insertImage(const QImage& image)
{
    if (image.isNull()) return;
    const QSizeF sz = image.size();
    double w = sz.width(), h = sz.height();
    const double maxSide = 400.0;
    if (w > maxSide || h > maxSide) {
        const double k = w >= h ? maxSide / w : maxSide / h;
        w *= k;
        h *= k;
    }
    const QPointF viewCenter = cam_.toWorld(QPointF(width() / 2.0, height() / 2.0));
    const QPointF pos(viewCenter.x() - w / 2.0, viewCenter.y() - h / 2.0);
    insertImageAt(image, pos, w, h);
}

QByteArray Canvas::serializeState() const
{
    DocumentData data;
    data.strokes = doc_.strokes();
    for (Stroke& s : data.strokes) s.verts.clear();
    data.texts = doc_.texts();
    data.images = doc_.images();
    data.shapes = doc_.shapes();
    data.camera = cam_;
    data.nextId = doc_.nextIdValue();
    return serializeDocument(data);
}

bool Canvas::loadState(const QByteArray& bytes)
{
    DocumentData data;
    if (!deserializeDocument(bytes, data)) return false;
    if (editingText_ >= 0) finishEditText();
    clearSelection();
    for (Stroke& s : data.strokes)
        s.verts.clear();
    doc_.replaceAll(std::move(data.strokes), std::move(data.texts),
                    std::move(data.images), std::move(data.shapes), data.nextId);
    cam_ = data.camera;
    selBoxValid_ = false;
    dirty_ = true;
    update();
    return true;
}

void Canvas::newDocument()
{
    if (editingText_ >= 0) finishEditText();
    clearSelection();
    doc_.replaceAll({}, {}, {}, {}, 1);
    cam_ = Camera{};
    selBoxValid_ = false;
    selBoxWorld_ = QRectF();
    dirty_ = true;
    update();
}

void Canvas::initializeGL()
{
    initializeOpenGLFunctions();
    glEnable(GL_MULTISAMPLE);
    GLint glMaxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &glMaxSamples);
    qDebug().noquote() << "GL samples (widget format):" << format().samples()
                       << "GL_MAX_SAMPLES:" << glMaxSamples;

    bgProg_ = new QOpenGLShaderProgram(this);
    bgProg_->addShaderFromSourceFile(QOpenGLShader::Vertex, QStringLiteral(":/shaders/background.vert"));
    bgProg_->addShaderFromSourceFile(QOpenGLShader::Fragment, QStringLiteral(":/shaders/background.frag"));
    bgProg_->link();

    glGenVertexArrays(1, &bgVao_);
    glGenBuffers(1, &bgVbo_);
    static const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glBindVertexArray(bgVao_);
    glBindBuffer(GL_ARRAY_BUFFER, bgVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    prog_ = new QOpenGLShaderProgram(this);
    prog_->addShaderFromSourceFile(QOpenGLShader::Vertex, QStringLiteral(":/shaders/stroke.vert"));
    prog_->addShaderFromSourceFile(QOpenGLShader::Fragment, QStringLiteral(":/shaders/stroke.frag"));
    prog_->link();

    glGenVertexArrays(2, vao_);
    glGenBuffers(2, vbo_);
    const GLsizei stride = 6 * sizeof(GLfloat);
    for (int i = 0; i < 2; ++i) {
        glBindVertexArray(vao_[i]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_[i]);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (const void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (const void*)(2 * sizeof(GLfloat)));
    }
    glBindVertexArray(0);

    glGenVertexArrays(1, &gizVao_);
    glGenBuffers(1, &gizVbo_);
    glBindVertexArray(gizVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gizVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (const void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (const void*)(2 * sizeof(GLfloat)));
    glBindVertexArray(0);

    glGenVertexArrays(1, &eraserVao_);
    glGenBuffers(1, &eraserVbo_);
    glBindVertexArray(eraserVao_);
    glBindBuffer(GL_ARRAY_BUFFER, eraserVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (const void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (const void*)(2 * sizeof(GLfloat)));
    glBindVertexArray(0);

    presentProg_ = new QOpenGLShaderProgram(this);
    presentProg_->addShaderFromSourceFile(QOpenGLShader::Vertex, QStringLiteral(":/shaders/present.vert"));
    presentProg_->addShaderFromSourceFile(QOpenGLShader::Fragment, QStringLiteral(":/shaders/present.frag"));
    presentProg_->link();

    glGenVertexArrays(1, &presentVao_);
    glGenBuffers(1, &presentVbo_);
    const float presentQuad[16] = {
        -1, -1, 0, 0,
         1, -1, 1, 0,
        -1,  1, 0, 1,
         1,  1, 1, 1,
    };
    glBindVertexArray(presentVao_);
    glBindBuffer(GL_ARRAY_BUFFER, presentVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(presentQuad), presentQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

void Canvas::resizeGL(int w, int h)
{
    cam_.setViewSize(QSizeF(double(w), double(h)));
    recreateResolveFbo();
}

void Canvas::recreateResolveFbo()
{
    if (width() <= 0 || height() <= 0)
        return;
    delete ssFbo_;
    ssFbo_ = nullptr;
    ssFbo_ = new QOpenGLFramebufferObject(
        QSize(int(width() * kSupersample * devicePixelRatioF() + 0.5f),
              int(height() * kSupersample * devicePixelRatioF() + 0.5f)));
    ssFbo_->bind();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ssFbo_->release();
}

void Canvas::dumpDebug()
{
    const QString base = QStringLiteral("/tmp/notes_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QImage ssImg;
    if (ssFbo_) {
        ssFbo_->bind();
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        QImage img(ssFbo_->size(), QImage::Format_RGBA8888);
        glReadPixels(0, 0, img.width(), img.height(), GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        ssImg = img.flipped(Qt::Vertical);
        ssFbo_->release();
    }
    const QImage finalImg = grabFramebuffer();
    qDebug("dumpDebug: ssaa=%dx%d (%s) final=%dx%d (%s) dpr=%g zoom=%g offset=(%g,%g)",
           ssImg.width(), ssImg.height(), ssImg.isNull() ? "null" : "ok",
           finalImg.width(), finalImg.height(), finalImg.isNull() ? "null" : "ok",
           double(devicePixelRatioF()), double(cam_.zoom),
           double(cam_.offset.x()), double(cam_.offset.y()));
    if (!ssImg.isNull())
        ssImg.save(base + "_ssaa_3x.png");
    if (!finalImg.isNull())
        finalImg.save(base + "_screen.png");
}

void Canvas::paintGL()
{
    const qreal dpr = devicePixelRatioF();
    const QSize deviceSize(int(width() * kSupersample * dpr + 0.5f),
                           int(height() * kSupersample * dpr + 0.5f));
    if (!ssFbo_ || ssFbo_->size() != deviceSize)
        recreateResolveFbo();

    ssFbo_->bind();
    glViewport(0, 0, GLsizei(deviceSize.width()), GLsizei(deviceSize.height()));
    glClearColor(0.99f, 0.99f, 0.99f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    bgProg_->bind();
    bgProg_->setUniformValue("offset", cam_.offset);
    bgProg_->setUniformValue("zoom", cam_.zoom);
    bgProg_->setUniformValue("dpr", float(dpr));
    bgProg_->setUniformValue("ss", kSupersample);
    bgProg_->setUniformValue("viewport", QVector2D(float(width()), float(height())));
    bgProg_->setUniformValue("baseSpacing", 24.0f);
    bgProg_->setUniformValue("minPx", 24.0f);
    bgProg_->setUniformValue("fineOnset", 0.78f);
    bgProg_->setUniformValue("lineWidth", 1.0f);
    bgProg_->setUniformValue("paperColor", QVector3D(0.99f, 0.99f, 0.99f));
    bgProg_->setUniformValue("gridColor", QVector3D(0.87f, 0.89f, 0.93f));
    bgProg_->setUniformValue("outsideColor", QVector3D(float(theme::kCanvasBackground.redF()),
                                                       float(theme::kCanvasBackground.greenF()),
                                                       float(theme::kCanvasBackground.blueF())));
    bgProg_->setUniformValue("pageMin", QVector2D(float(kPageMargin), float(kPageMargin)));
    bgProg_->setUniformValue("pageMax", QVector2D(float(kPageMargin + kPageWidth),
                                                   float(kPageMargin + kPageHeight)));
    bgProg_->setUniformValue("pageRadius", float(kPageCornerRadius));
    glBindVertexArray(bgVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    bgProg_->release();

    prog_->bind();
    prog_->setUniformValue("viewport", QVector2D(float(width()), float(height())));
    prog_->setUniformValue("offset", cam_.offset);
    prog_->setUniformValue("zoom", cam_.zoom);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(vao_[0]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_[0]);
    if (dirty_) {
        verts_.clear();
        for (const auto& s : doc_.strokes())
            verts_.insert(verts_.end(), s.verts.begin(), s.verts.end());
        glBufferData(GL_ARRAY_BUFFER, verts_.size() * sizeof(float),
                     verts_.empty() ? nullptr : verts_.data(), GL_DYNAMIC_DRAW);
        dirty_ = false;
    }
    size_t offset = 0;
    for (const auto& s : doc_.strokes()) {
        if (s.verts.empty()) continue;
        glDrawArrays(GL_TRIANGLES, GLint(offset / 6), GLsizei(s.verts.size() / 6));
        offset += s.verts.size();
    }

    glBindVertexArray(vao_[1]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_[1]);
    if (curDirty_) {
        glBufferData(GL_ARRAY_BUFFER, cur_.verts.size() * sizeof(float),
                     cur_.verts.empty() ? nullptr : cur_.verts.data(), GL_STREAM_DRAW);
        curDirty_ = false;
    }
    if (!cur_.verts.empty())
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(cur_.verts.size() / 6));

    glBindVertexArray(eraserVao_);
    glBindBuffer(GL_ARRAY_BUFFER, eraserVbo_);
    if (eraserDirty_) {
        eraserVerts_.clear();
        const float now = float(eraseClock_.elapsed()) / 1000.0f;
        eraserTrail_.erase(std::remove_if(eraserTrail_.begin(), eraserTrail_.end(),
            [now](const EraserPt& s) { return now - s.born > kEraseLife; }),
            eraserTrail_.end());
        for (const EraserPt& s : eraserTrail_) {
            const float age = now - s.born;
            const float a = age / kEraseLife;
            float k = std::clamp((1.0f - a) / 0.3f, 0.0f, 1.0f);
            k = k * k * (3.0f - 2.0f * k);
            pushDisc(eraserVerts_, s.x, s.y, s.r, QColor(60, 150, 255), kEraseHeadAlpha * k, 12);
        }
        glBufferData(GL_ARRAY_BUFFER, eraserVerts_.size() * sizeof(float),
                     eraserVerts_.empty() ? nullptr : eraserVerts_.data(), GL_STREAM_DRAW);
        eraserDirty_ = false;
        if (eraserTrail_.empty())
            eraseTimer_->stop();
    }
    if (!eraserVerts_.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(eraserVerts_.size() / 6));
        glDisable(GL_BLEND);
    }

    if (!selection_.empty() || marqueeActive_)
        drawGizmo();

    glBindVertexArray(0);
    prog_->release();

    ssFbo_->release();

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, GLsizei(width() * dpr), GLsizei(height() * dpr));
    glDisable(GL_BLEND);
    presentProg_->bind();
    presentProg_->setUniformValue("uTex", 0);
    presentProg_->setUniformValue("uTexelSize",
                                  QVector2D(1.0f / float(ssFbo_->size().width()),
                                            1.0f / float(ssFbo_->size().height())));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssFbo_->texture());
    glBindVertexArray(presentVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    presentProg_->release();

    {
        QPainter p(this);
        const QPointF offset(cam_.offset.x(), cam_.offset.y());
        for (const ShapeItem& sh : doc_.shapes())
            drawShapeItem(p, sh, cam_.zoom, offset);
        if (shapeDragging_)
            drawShapeItem(p, dragShape_, cam_.zoom, offset);
        for (const ImageItem& im : doc_.images())
            drawImageItem(p, im, cam_.zoom, offset);
        for (const TextBox& t : doc_.texts())
            drawTextBox(p, t, cam_.zoom, offset,
                        editingText_ == t.id, textCursor_,
                        QColor(0x30, 0x90, 0xff),
                        editingText_ == t.id ? preeditStart_ : -1, preeditLen_);
        p.end();
    }
}

void Canvas::tabletEvent(QTabletEvent* e)
{
    e->accept();
    const QPointF p = e->position();
    switch (e->type()) {
    case QEvent::TabletPress:
        setFocus(Qt::MouseFocusReason);
        if (spaceDown_ || zDown_) {
            penDown_ = true;
            if (spaceDown_) {
                stylusMode_ = Gesture::Pan;
                penLast_ = p;
            } else {
                beginZoom(p);
            }
        } else {
            penDown_ = true;
            stylusMode_ = Gesture::Draw;
            if (tool_ == Tool::Pencil || tool_ == Tool::Highlighter) beginStroke(p, pressureValue(float(e->pressure())));
            else if (tool_ == Tool::Eraser) beginErase(p);
            else if (tool_ == Tool::Select) { stylusMode_ = Gesture::Select; selectBegin(p); }
            else if (tool_ == Tool::Shape) beginShape(p);
            else { stylusMode_ = Gesture::None; textBegin(p); }
        }
        break;
    case QEvent::TabletMove:
        if (spaceDown_) {
            if (stylusMode_ != Gesture::Pan) {
                if (stylusMode_ == Gesture::Draw) finishInput();
                stylusMode_ = Gesture::Pan;
                penLast_ = p;
            }
            if (penDown_) panStylus(p);
        } else if (zDown_) {
            if (stylusMode_ != Gesture::Zoom) {
                if (stylusMode_ == Gesture::Draw) finishInput();
                beginZoom(p);
            }
            if (penDown_) zoomStylus(p);
        } else if (penDown_) {
            if (stylusMode_ == Gesture::Select) {
                selectMove(p);
            } else if (stylusMode_ == Gesture::Draw) {
                if (tool_ == Tool::Pencil || tool_ == Tool::Highlighter) addPoint(p, pressureValue(float(e->pressure())));
                else if (tool_ == Tool::Eraser) eraseAt(p);
                else if (tool_ == Tool::Shape) updateShape(p);
            } else if (tool_ == Tool::Select) {
                stylusMode_ = Gesture::Select;
                selectBegin(p);
            } else if (tool_ == Tool::Text) {
                stylusMode_ = Gesture::None;
            } else if (tool_ == Tool::Shape) {
                stylusMode_ = Gesture::Draw;
                beginShape(p);
            } else {
                stylusMode_ = Gesture::Draw;
                if (tool_ == Tool::Pencil || tool_ == Tool::Highlighter) beginStroke(p, pressureValue(float(e->pressure())));
                else beginErase(p);
            }
        }
        break;
    case QEvent::TabletRelease:
        if (stylusMode_ == Gesture::Draw || stylusMode_ == Gesture::Select) finishInput();
        penDown_ = false;
        stylusMode_ = Gesture::None;
        break;
    default:
        break;
    }
}

void Canvas::mousePressEvent(QMouseEvent* e)
{
    setFocus(Qt::MouseFocusReason);
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        panning_ = true;
        panLast_ = e->position();
        return;
    }
    if (tool_ == Tool::Pencil || tool_ == Tool::Highlighter) beginStroke(e->position(), 0.5f);
    else if (tool_ == Tool::Eraser) beginErase(e->position());
    else if (tool_ == Tool::Shape) beginShape(e->position());
    else if (tool_ == Tool::Text) { textEditJustStarted_ = true; textBegin(e->position()); }
    else selectBegin(e->position());
}

void Canvas::mouseMoveEvent(QMouseEvent* e)
{
    if (panning_) {
        const QPointF p = e->position();
        cam_.panBy(QPointF(p.x() - panLast_.x(), p.y() - panLast_.y()));
        panLast_ = p;
        update();
        return;
    }
    if (drawing_) addPoint(e->position(), 0.5f);
    else if (erasing_) eraseAt(e->position());
    else if (shapeDragging_) updateShape(e->position());
    else if (selectDrag_) selectMove(e->position());
}

void Canvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        panning_ = false;
        return;
    }
    if (tool_ == Tool::Text) {
        if (textEditJustStarted_) {
            textEditJustStarted_ = false;
            return;
        }
    }
    finishInput();
}

void Canvas::wheelEvent(QWheelEvent* e)
{
    const QPointF p = e->position();
    const double factor = e->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
    cam_.zoomAt(p, cam_.toWorld(p), factor);
    update();
}

void Canvas::keyPressEvent(QKeyEvent* e)
{
    if (editingText_ >= 0) {
        textKeyPress(e);
        return;
    }
    if (e->key() == Qt::Key_F8) { dumpDebug(); return; }
    if (e->key() == Qt::Key_Delete) { deleteSelection(); return; }
    if (e->modifiers() & Qt::ControlModifier) {
        if (e->key() == Qt::Key_C) { copy(); return; }
        if (e->key() == Qt::Key_X) { cut(); return; }
        if (e->key() == Qt::Key_V) { paste(); return; }
    }
    const bool mods = (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0;
    if (e->key() == Qt::Key_Shift)
        shiftDown_ = true;
    else if (e->key() == Qt::Key_Space && !mods)
        spaceDown_ = true;
    else if (e->key() == Qt::Key_Z && e->modifiers() == Qt::NoModifier)
        zDown_ = true;
    else if (e->key() == Qt::Key_Escape) {
        if (selectDrag_) selectEnd();
        clearSelection();
    }

    if (penDown_ && (stylusMode_ == Gesture::Draw || stylusMode_ == Gesture::Select)
        && (spaceDown_ || zDown_))
        finishInput();
}

void Canvas::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space) spaceDown_ = false;
    else if (e->key() == Qt::Key_Z) zDown_ = false;
    else if (e->key() == Qt::Key_Shift) shiftDown_ = false;
}

void Canvas::panStylus(const QPointF& p)
{
    cam_.panBy(QPointF(p.x() - penLast_.x(), p.y() - penLast_.y()));
    penLast_ = p;
    update();
}

void Canvas::beginZoom(const QPointF& p)
{
    zoomAnchorPos_ = p;
    zoomAnchorWorld_ = cam_.toWorld(p);
    zoomStartScale_ = cam_.zoom;
    stylusMode_ = Gesture::Zoom;
    penLast_ = p;
}

void Canvas::zoomStylus(const QPointF& p)
{
    const float dx = float(p.x() - zoomAnchorPos_.x());
    cam_.setScale(zoomAnchorPos_, zoomAnchorWorld_, zoomStartScale_ * std::exp(dx * 0.008));
    update();
}

float Canvas::pressureValue(float p) const
{
    if (!pressureEnabled_) return 0.5f;
    return std::clamp(p, 0.0f, 1.0f);
}

void Canvas::beginStroke(const QPointF& p, float pr)
{
    cur_ = Stroke{};
    cur_.id = doc_.nextId();
    cur_.color = color_;
    cur_.size = size_;
    if (tool_ == Tool::Highlighter) {
        QColor c = color_;
        c.setAlphaF(kHighlightAlpha);
        cur_.color = c;
    }
    cur_.complete = false;
    strokeTessClock_.restart();
    drawing_ = true;
    addPoint(p, pr);
}

void Canvas::addPoint(const QPointF& p, float pr)
{
    const QPointF w = cam_.toWorld(p);
    const Pt pt{float(w.x()), float(w.y()), pr};
    const float minWorldDist = 1.0f / std::max(cam_.zoom, 0.01f);
    if (shiftDown_ && !cur_.pts.empty()) {
        const Pt& anchor = cur_.pts.front();
        const float dx = pt.x - anchor.x, dy = pt.y - anchor.y;
        const float len = std::hypot(dx, dy);
        if (len < minWorldDist) return;
        constexpr double step = 3.14159265358979323846 / 4.0;
        const double ang = std::round(std::atan2(dy, dx) / step) * step;
        const Pt snapped{anchor.x + float(len * std::cos(ang)), anchor.y + float(len * std::sin(ang)), pr};
        if (cur_.pts.size() == 2
            && std::hypot(snapped.x - cur_.pts[1].x, snapped.y - cur_.pts[1].y) < minWorldDist)
            return;
        cur_.pts.resize(1);
        cur_.pts.push_back(snapped);
        cur_.complete = true;
        retessellate(cur_);
        curDirty_ = true;
        update();
        return;
    }
    if (!cur_.pts.empty() && std::hypot(pt.x - cur_.pts.back().x, pt.y - cur_.pts.back().y) < minWorldDist)
        return;
    cur_.pts.push_back(pt);
    cur_.complete = false;
    const bool throttle = cur_.pts.size() > 256 && strokeTessClock_.elapsed() < 12;
    if (!throttle) {
        retessellate(cur_);
        strokeTessClock_.restart();
    }
    curDirty_ = true;
    update();
}

void Canvas::endStroke()
{
    if (!drawing_) return;
    drawing_ = false;
    if (!cur_.pts.empty()) {
        cur_.complete = true;
        retessellate(cur_);
        doc_.add(cur_);
        dirty_ = true;
    }
    cur_ = Stroke{};
    curDirty_ = true;
    update();
}

void Canvas::beginErase(const QPointF& p)
{
    erasing_ = true;
    eraserTrail_.clear();
    eraseAt(p);
}

void Canvas::eraseAt(const QPointF& p)
{
    if (editingText_ >= 0) finishEditText();
    const QPointF w = cam_.toWorld(p);
    const float radius = (eraserMode_ == EraserMode::Partial)
        ? eraseRadius_ * 0.6f
        : eraseRadius_;
    const float r = radius / cam_.zoom;
    const float step = std::max(0.5f, r * 0.4f);

    QPointF prev = w;
    if (!eraserTrail_.empty())
        prev = QPointF(eraserTrail_.back().x, eraserTrail_.back().y);

    const float dist = std::hypot(w.x() - prev.x(), w.y() - prev.y());
    const int n = std::max(1, int(std::ceil(dist / step)));
    const float now = float(eraseClock_.elapsed()) / 1000.0f;
    for (int i = 0; i < n; ++i) {
        const float t = float(i + 1) / float(n);
        const QPointF sp(prev.x() + (w.x() - prev.x()) * t,
                         prev.y() + (w.y() - prev.y()) * t);
        eraserTrail_.push_back({float(sp.x()), float(sp.y()), r, now});
        Document::EraseResult removed =
            (eraserMode_ == EraserMode::Partial)
            ? doc_.erasePartial(sp, r)
            : doc_.eraseNear(sp, r);
        for (auto& s : removed.strokes) eraseBatch_.push_back(std::move(s));
        for (auto& t2 : removed.texts) textEraseBatch_.push_back(std::move(t2));
        for (auto& im : removed.images) imageEraseBatch_.push_back(std::move(im));
        for (auto& sh : removed.shapes) shapeEraseBatch_.push_back(std::move(sh));
        for (auto& s : removed.added) partialAddBatch_.push_back(std::move(s));
    }
    if (!eraseTimer_->isActive())
        eraseTimer_->start();
    dirty_ = true;
    eraserDirty_ = true;
    update();
}

void Canvas::endErase()
{
    erasing_ = false;
    if (!eraseBatch_.empty() || !textEraseBatch_.empty() || !imageEraseBatch_.empty()
        || !partialAddBatch_.empty() || !shapeEraseBatch_.empty()) {
        Document::EraseResult res;
        res.strokes = std::move(eraseBatch_);
        res.texts = std::move(textEraseBatch_);
        res.images = std::move(imageEraseBatch_);
        res.added = std::move(partialAddBatch_);
        res.shapes = std::move(shapeEraseBatch_);
        doc_.commitErase(std::move(res));
    }
    eraseBatch_.clear();
    textEraseBatch_.clear();
    imageEraseBatch_.clear();
    partialAddBatch_.clear();
    shapeEraseBatch_.clear();
    eraserDirty_ = true;
    update();
}

void Canvas::beginShape(const QPointF& p)
{
    const QPointF w = cam_.toWorld(p);
    shapeDragging_ = true;
    shapeAnchor_ = w;
    dragShape_.kind = shapeKind_;
    dragShape_.rect = QRectF(w, w);
    dragShape_.color = color_;
    dragShape_.penWidth = shapePenWidth_;
    update();
}

void Canvas::updateShape(const QPointF& p)
{
    const QPointF w = cam_.toWorld(p);
    QRectF r = QRectF(shapeAnchor_, w).normalized();
    if (shiftDown_) {
        const double side = std::max(r.width(), r.height());
        r.setWidth(side);
        r.setHeight(side);
    }
    dragShape_.rect = r;
    update();
}

void Canvas::endShape()
{
    if (!shapeDragging_) return;
    shapeDragging_ = false;
    const QRectF r = dragShape_.rect;
    const double minPx = std::min(r.width(), r.height()) * cam_.zoom;
    if (r.width() < 1e-6 || r.height() < 1e-6 || minPx < 4.0) {
        update();
        return;
    }
    dragShape_.id = doc_.nextId();
    doc_.addShape(dragShape_);
    selection_ = { dragShape_.id };
    selBoxValid_ = false;
    dirty_ = true;
    animateGizmoIn();
    update();
}

void Canvas::finishInput()
{
    if (tool_ == Tool::Pencil || tool_ == Tool::Highlighter) endStroke();
    else if (tool_ == Tool::Eraser) endErase();
    else if (tool_ == Tool::Text) finishEditText();
    else if (tool_ == Tool::Shape) endShape();
    else if (selectDrag_) selectEnd();
}

void Canvas::clearSelection()
{
    if (gizmoAnim_) {
        gizmoAnim_->stop();
        gizmoAnim_->deleteLater();
        gizmoAnim_ = nullptr;
    }
    gizmoAlpha_ = 1.0f;
    selection_.clear();
    selBoxValid_ = false;
    beforeSnapshot_.clear();
    beforeTexts_.clear();
    beforeImages_.clear();
    beforeShapes_.clear();
    update();
}

QRectF Canvas::selectionBox()
{
    if (selBoxValid_) return selBoxWorld_;
    selBoxValid_ = true;
    selBoxWorld_ = QRectF();
    if (selection_.empty()) return selBoxWorld_;
    selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
        [this](int id) {
            return doc_.strokeById(id) == nullptr && doc_.textById(id) == nullptr
                && doc_.imageById(id) == nullptr && doc_.shapeById(id) == nullptr;
        }),
        selection_.end());
    std::vector<Stroke> items;
    items.reserve(selection_.size());
    for (int id : selection_) {
        if (const Stroke* s = doc_.strokeById(id)) items.push_back(*s);
    }
    QRectF b = items.empty() ? QRectF() : bboxOf(items);
    for (int id : selection_) {
        if (const TextBox* t = doc_.textById(id)) {
            relayoutTextBox(*t);
            b = b.united(t->rect());
        }
    }
    for (int id : selection_) {
        if (const ImageItem* im = doc_.imageById(id))
            b = b.united(im->rect());
    }
    for (int id : selection_) {
        if (const ShapeItem* sh = doc_.shapeById(id))
            b = b.united(sh->rect);
    }
    selBoxWorld_ = b;
    return selBoxWorld_;
}

void Canvas::beginSelectDrag(const QPointF& p)
{
    selHandle_ = Handle::Move;
    dragLastScreen_ = p;
    beforeSnapshot_ = doc_.snapshot(selection_);
    beforeTexts_ = doc_.textSnapshot(selection_);
    beforeImages_ = doc_.imageSnapshot(selection_);
    beforeShapes_ = doc_.shapeSnapshot(selection_);
    selChanged_ = false;
    selectDrag_ = true;
}

void Canvas::selectBegin(const QPointF& p)
{
    const QPointF w = cam_.toWorld(p);
    int hit = imageAt(doc_.images(), w, kClickTolPx / cam_.zoom);
    if (hit < 0) hit = textAt(doc_.texts(), w, kClickTolPx / cam_.zoom);
    if (hit < 0) hit = shapeAt(doc_.shapes(), w, kClickTolPx / cam_.zoom);
    if (hit < 0) hit = strokeAt(doc_.strokes(), w, kClickTolPx / cam_.zoom);
    if (hit >= 0) {
        if (std::find(selection_.begin(), selection_.end(), hit) == selection_.end()) {
            selection_ = { hit };
            selBoxValid_ = false;
            animateGizmoIn();
        }
        beginSelectDrag(p);
        return;
    }

    if (!selection_.empty()) {
        const QRectF b = selectionBox();
        if (!b.isNull()) {
            const double minPx = std::min(b.width(), b.height()) * cam_.zoom;
            if (minPx > 24.0) {
                const Handle h = hitHandle(b, w, kHandleTolPx / cam_.zoom);
                if (h != Handle::None) {
                    selHandle_ = h;
                    dragStartWorld_ = b;
                    dragAnchorWorld_ = handleAnchor(b, h);
                    beforeSnapshot_ = doc_.snapshot(selection_);
                    beforeTexts_ = doc_.textSnapshot(selection_);
                    beforeImages_ = doc_.imageSnapshot(selection_);
                    selChanged_ = false;
                    dragLastScreen_ = p;
                    selectDrag_ = true;
                    return;
                }
            }
            const double grab = kGrabPx / cam_.zoom;
            if (b.adjusted(-grab, -grab, grab, grab).contains(w)) {
                beginSelectDrag(p);
                return;
            }
        }
    }
    selectDrag_ = true;
    selHandle_ = Handle::None;
    marqueeActive_ = true;
    marqueeAnchorWorld_ = w;
    marqueeRectWorld_ = QRectF(w, w);
    if (selection_.empty()) animateGizmoIn();
}

void Canvas::selectMove(const QPointF& p)
{
     qDebug() << "selectMove called, selHandle_=" << int(selHandle_) << "selection size=" << selection_.size();
    if (selHandle_ == Handle::None) {
        marqueeRectWorld_ = QRectF(marqueeAnchorWorld_, cam_.toWorld(p)).normalized();
        update();
        return;
    }
    if (selHandle_ == Handle::Move) {
        const QPointF d = cam_.toWorld(p) - cam_.toWorld(dragLastScreen_);
        dragLastScreen_ = p;
        if (d.x() != 0 || d.y() != 0) {
            translateSelection(QPointF(d.x(), d.y()));
            selChanged_ = true;
        }
        return;
    }
    scaleSelection(cam_.toWorld(p));
}

void Canvas::selectEnd()
{
    const Handle h = selHandle_;
    selHandle_ = Handle::Move;
    selectDrag_ = false;

    if (h == Handle::None) {
        marqueeActive_ = false;
        const QRectF r = marqueeRectWorld_;
        marqueeRectWorld_ = QRectF();
        if (r.isNull()) { clearSelection(); return; }
        const double pw = r.width() * cam_.zoom, ph = r.height() * cam_.zoom;
        if (pw < 5 && ph < 5) {
            clearSelection();
            return;
        }
        selection_.clear();
        for (const auto& s : doc_.strokes())
            if (strokeHitsRect(s, r)) selection_.push_back(s.id);
        for (const auto& t : doc_.texts())
            if (textHitsRect(t, r)) selection_.push_back(t.id);
        for (const auto& im : doc_.images())
            if (imageHitsRect(im, r)) selection_.push_back(im.id);
        for (const auto& sh : doc_.shapes())
            if (shapeHitsRect(sh, r)) selection_.push_back(sh.id);
        selBoxValid_ = false;
        if (!selection_.empty()) animateGizmoIn();
        update();
        return;
    }

    if (selChanged_) {
        doc_.commitTransform(beforeSnapshot_, doc_.snapshot(selection_));
        doc_.commitTextTransform(beforeTexts_, doc_.textSnapshot(selection_));
        doc_.commitImageTransform(beforeImages_, doc_.imageSnapshot(selection_));
        doc_.commitShapeTransform(beforeShapes_, doc_.shapeSnapshot(selection_));
    }
    beforeSnapshot_.clear();
    beforeTexts_.clear();
    beforeImages_.clear();
    beforeShapes_.clear();
    selBoxValid_ = false;
    update();
}

void Canvas::deleteSelection()
{
    if (editingText_ >= 0 || selection_.empty()) return;
    doc_.commitRemove(selection_);
    clearSelection();
    dirty_ = true;
    update();
}

void Canvas::translateSelection(const QPointF& deltaWorld)
{
    for (int id : selection_) {
        Stroke* s = doc_.strokeById(id);
        if (s) {
            for (Pt& p : s->pts) {
                p.x += float(deltaWorld.x());
                p.y += float(deltaWorld.y());
            }
            retessellate(*s);
            continue;
        }
        TextBox* t = doc_.textById(id);
        if (t) {
            t->pos += deltaWorld;
            t->layoutDirty = true;
            continue;
        }
        ImageItem* im = doc_.imageById(id);
        if (im) {
            im->pos += deltaWorld;
            continue;
        }
        ShapeItem* sh = doc_.shapeById(id);
        if (sh)
            sh->rect.translate(deltaWorld);
    }
    selBoxValid_ = false;
    dirty_ = true;
    update();
}

void Canvas::scaleSelection(const QPointF& w)
{
    const QRectF b = dragStartWorld_;
    if (b.width() < 1e-9 || b.height() < 1e-9) return;
    const QPointF a = dragAnchorWorld_;
    const QPointF g = handlePos(b, selHandle_);
    auto safe = [](double denom) { return std::abs(denom) < 1e-6 ? 1.0 : denom; };
    double sx = 1.0, sy = 1.0;
    switch (selHandle_) {
    case Handle::E: case Handle::W:
        sx = (w.x() - a.x()) / safe(g.x() - a.x()); break;
    case Handle::N: case Handle::S:
        sy = (w.y() - a.y()) / safe(g.y() - a.y()); break;
    default:
        sx = (w.x() - a.x()) / safe(g.x() - a.x());
        sy = (w.y() - a.y()) / safe(g.y() - a.y()); break;
    }
    constexpr double kMin = 0.02;
    if (sx < kMin) sx = kMin;
    if (sy < kMin) sy = kMin;

    double k;
    switch (selHandle_) {
    case Handle::E: case Handle::W: k = std::fabs(sx); break;
    case Handle::N: case Handle::S: k = std::fabs(sy); break;
    default: k = 0.5 * (std::fabs(sx) + std::fabs(sy)); break;
    }
    if (k < kMin) k = kMin;

    for (const Stroke& orig : beforeSnapshot_) {
        Stroke* s = doc_.strokeById(orig.id);
        if (!s) continue;
        for (size_t i = 0; i < s->pts.size() && i < orig.pts.size(); ++i) {
            s->pts[i].x = float(a.x() + (double(orig.pts[i].x) - a.x()) * sx);
            s->pts[i].y = float(a.y() + (double(orig.pts[i].y) - a.y()) * sy);
        }
        s->size = float(orig.size * k);
        retessellate(*s);
    }

    for (const TextBox& orig : beforeTexts_) {
        TextBox* t = doc_.textById(orig.id);
        if (!t) continue;
        t->pos = QPointF(a.x() + (orig.pos.x() - a.x()) * k,
                         a.y() + (orig.pos.y() - a.y()) * k);
        t->width = orig.width * k;
        t->fontPx = orig.fontPx * k;
        t->color = orig.color;
        t->layoutDirty = true;
    }
    for (const ImageItem& orig : beforeImages_) {
        ImageItem* im = doc_.imageById(orig.id);
        if (!im) continue;
        im->pos = QPointF(a.x() + (orig.pos.x() - a.x()) * k,
                          a.y() + (orig.pos.y() - a.y()) * k);
        im->width = orig.width * k;
        im->height = orig.height * k;
    }
    for (const ShapeItem& orig : beforeShapes_) {
        ShapeItem* sh = doc_.shapeById(orig.id);
        if (!sh) continue;
        const QRectF r(orig.rect.x() * sx + a.x() * (1 - sx),
                       orig.rect.y() * sy + a.y() * (1 - sy),
                       orig.rect.width() * sx,
                       orig.rect.height() * sy);
        sh->rect = r;
        sh->penWidth = orig.penWidth * k;
    }
    selChanged_ = true;
    selBoxValid_ = false;
    dirty_ = true;
    update();
}

void Canvas::textBegin(const QPointF& p)
{
    if (editingText_ >= 0) finishEditText();
    const QPointF w = cam_.toWorld(p);
    TextBox* tb = nullptr;
    const int tid = textAt(doc_.texts(), w, 4.0 / cam_.zoom);
    if (tid >= 0) tb = doc_.textById(tid);
    if (!tb) {
        TextBox t;
        t.id = doc_.nextId();
        t.pos = w;
        t.color = color_;
        t.width = 240.0;
        t.fontPx = fontSize_;
        t.layoutDirty = true;
        textDirtyBefore_ = doc_.isDirty();
        doc_.addText(t);
        tb = doc_.textById(t.id);
    }
    if (!tb) return;
    editingText_ = tb->id;
    textEditBefore_ = *tb;
    textEditBefore_.layoutDirty = false;
    textCursor_ = tb->text.size();
    update();
}

void Canvas::textKeyPress(QKeyEvent* e)
{
    TextBox* tb = doc_.textById(editingText_);
    if (!tb) {
        finishEditText();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        finishEditText();
        update();
        return;
    }
    if (e->modifiers() & Qt::ControlModifier) return;

    textRemovePreedit();

    const auto mark = [&] {
        tb->layoutDirty = true;
        update();
    };
    switch (e->key()) {
    case Qt::Key_Backspace:
        if (textCursor_ > 0) {
            tb->text.remove(textCursor_ - 1, 1);
            --textCursor_;
            mark();
        }
        break;
    case Qt::Key_Delete:
        if (textCursor_ < tb->text.size()) {
            tb->text.remove(textCursor_, 1);
            mark();
        }
        break;
    case Qt::Key_Left:
        textCursor_ = std::max(0, textCursor_ - 1);
        update();
        break;
    case Qt::Key_Right:
        textCursor_ = std::min(int(tb->text.size()), textCursor_ + 1);
        update();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        tb->text.insert(textCursor_, QLatin1Char('\n'));
        ++textCursor_;
        mark();
        break;
    case Qt::Key_Tab:
        tb->text.insert(textCursor_, QStringLiteral("    "));
        textCursor_ += 4;
        mark();
        break;
    default: {
        const QString t = e->text();
        if (!t.isEmpty() && t.at(0).isPrint()) {
            textInsert(t);
        }
        break;
    }
    }
}

void Canvas::textRemovePreedit()
{
    const int start = preeditStart_;
    const int len = preeditLen_;
    preeditStart_ = -1;
    preeditLen_ = 0;
    if (start < 0 || len <= 0) return;
    TextBox* tb = doc_.textById(editingText_);
    if (!tb) return;
    const int from = qBound(0, start, int(tb->text.size()));
    const int count = qMin(len, int(tb->text.size()) - from);
    tb->text.remove(from, count);
    textCursor_ = qBound(0, textCursor_ - count, int(tb->text.size()));
    tb->layoutDirty = true;
    update();
}

void Canvas::textInsert(const QString& s)
{
    TextBox* tb = doc_.textById(editingText_);
    if (!tb || s.isEmpty()) return;
    const int pos = qBound(0, textCursor_, int(tb->text.size()));
    tb->text.insert(pos, s);
    textCursor_ = pos + s.size();
    tb->layoutDirty = true;
    update();
}

void Canvas::inputMethodEvent(QInputMethodEvent* e)
{
    if (editingText_ < 0) {
        QOpenGLWidget::inputMethodEvent(e);
        return;
    }
    if (!e->commitString().isEmpty()) {
        textRemovePreedit();
        textInsert(e->commitString());
    }
    const QString preedit = e->preeditString();
    if (preedit.isEmpty()) {
        textRemovePreedit();
    } else {
        textRemovePreedit();
        textInsert(preedit);
        if (TextBox* tb = doc_.textById(editingText_)) {
            preeditStart_ = qBound(0, textCursor_ - preedit.size(), int(tb->text.size()));
            preeditLen_ = preedit.size();
        }
    }
    update();
}

void Canvas::finishEditText()
{
    if (editingText_ < 0) return;
    const int id = editingText_;
    editingText_ = -1;
    textCursor_ = 0;
    preeditStart_ = -1;
    preeditLen_ = 0;
    TextBox* cur = doc_.textById(id);
    if (!cur) {
        update();
        return;
    }
    if (cur->text.trimmed().isEmpty()) {
        doc_.discardText(id, textDirtyBefore_);
        selection_.erase(std::remove(selection_.begin(), selection_.end(), id), selection_.end());
        update();
        return;
    }
    if (cur->text != textEditBefore_.text
        || cur->width != textEditBefore_.width
        || cur->fontPx != textEditBefore_.fontPx) {
        std::vector<TextBox> before = { textEditBefore_ };
        doc_.commitTextEdit(before, doc_.textSnapshot({id}));
    }
    update();
}

namespace {

void pushQuad(std::vector<float>& v, double x0, double y0, double x1, double y1, const QColor& c)
{
    const float r = c.redF(), g = c.greenF(), b = c.blueF(), a = c.alphaF();
    const auto add = [&](float x, float y) { v.insert(v.end(), {x, y, r, g, b, a}); };
    add(float(x0), float(y0));
    add(float(x0), float(y1));
    add(float(x1), float(y0));
    add(float(x1), float(y0));
    add(float(x0), float(y1));
    add(float(x1), float(y1));
}

void pushBorder(std::vector<float>& v, const QRectF& r, double t, const QColor& c)
{
    pushQuad(v, r.left(), r.top(), r.right(), r.top() + t, c);
    pushQuad(v, r.left(), r.bottom() - t, r.right(), r.bottom(), c);
    pushQuad(v, r.left(), r.top(), r.left() + t, r.bottom(), c);
    pushQuad(v, r.right() - t, r.top(), r.right(), r.bottom(), c);
}

void pushArc(std::vector<float>& v, double cx, double cy, double r0, double r1,
             double a0, double a1, const QColor& c, int segs)
{
    const float rr = c.redF(), g = c.greenF(), b = c.blueF(), a = c.alphaF();
    for (int i = 0; i < segs; ++i) {
        const double t0 = a0 + (a1 - a0) * (double(i) / segs);
        const double t1 = a0 + (a1 - a0) * (double(i + 1) / segs);
        const double c0 = std::cos(t0), s0 = std::sin(t0);
        const double c1 = std::cos(t1), s1 = std::sin(t1);
        const double ix0 = cx + r0 * c0, iy0 = cy + r0 * s0;
        const double ix1 = cx + r0 * c1, iy1 = cy + r0 * s1;
        const double ox0 = cx + r1 * c0, oy0 = cy + r1 * s0;
        const double ox1 = cx + r1 * c1, oy1 = cy + r1 * s1;
        const auto tri = [&](double ax, double ay, double bx, double by, double dx2, double dy2) {
            v.insert(v.end(), {float(ax), float(ay), rr, g, b, a});
            v.insert(v.end(), {float(bx), float(by), rr, g, b, a});
            v.insert(v.end(), {float(dx2), float(dy2), rr, g, b, a});
        };
        tri(ix0, iy0, ox0, oy0, ox1, oy1);
        tri(ix0, iy0, ox1, oy1, ix1, iy1);
    }
}

} // namespace

void Canvas::animateGizmoIn()
{
    if (gizmoAnim_) {
        gizmoAnim_->stop();
        gizmoAnim_->deleteLater();
        gizmoAnim_ = nullptr;
    }
    if (!notes::anim::enabled()) {
        gizmoAlpha_ = 1.0f;
        return;
    }
    gizmoAlpha_ = 0.0f;
    QVariantAnimation* anim = new QVariantAnimation(this);
    anim->setDuration(180);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        gizmoAlpha_ = float(v.toDouble());
        update();
    });
    gizmoAnim_ = anim;
    anim->start();
    update();
}

void Canvas::drawGizmo()
{
    std::vector<float> v;
    v.reserve(512);
    const double hw = 4.0 / cam_.zoom;
    const double bw = 2.0 / cam_.zoom;

    const float g = std::clamp(gizmoAlpha_, 0.0f, 1.0f);
    const auto col = [g](int r, int gg, int b, int a) {
        return QColor(r, gg, b, int(a * g + 0.5f));
    };

    if (marqueeActive_ && !marqueeRectWorld_.isNull()) {
        pushQuad(v, marqueeRectWorld_.left(), marqueeRectWorld_.top(),
                 marqueeRectWorld_.right(), marqueeRectWorld_.bottom(),
                 col(90, 150, 255, 28));
        pushBorder(v, marqueeRectWorld_, bw, col(60, 120, 255, 230));
    }

    if (!selection_.empty()) {
        const QRectF b = selectionBox();
        if (!b.isNull()) {
            const QColor line = col(60, 110, 255, 255);
            pushBorder(v, b, bw, line);
            const QColor fill = col(255, 255, 255, 255);
            const Handle hs[8] = { Handle::N, Handle::NE, Handle::E, Handle::SE,
                                   Handle::S, Handle::SW, Handle::W, Handle::NW };
            for (Handle h : hs) {
                const QPointF c = handlePos(b, h);
                pushQuad(v, c.x() - hw, c.y() - hw, c.x() + hw, c.y() + hw, fill);
                pushBorder(v, QRectF(c.x() - hw, c.y() - hw, 2 * hw, 2 * hw), bw * 0.5, line);
            }
        }
    }

    if (v.empty()) return;
    glBindVertexArray(gizVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gizVbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_DYNAMIC_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(v.size() / 6));
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

}