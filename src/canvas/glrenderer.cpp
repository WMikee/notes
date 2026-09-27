#include "canvas/glrenderer.h"
#include "scene/page.h"
#include "scene/select.h"
#include "scene/xform.h"
#include "theme.h"
#include <QDateTime>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QVector2D>
#include <QVector3D>
#include <algorithm>
#include <cmath>


extern "C" void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB,
                                    GLenum sfactorAlpha, GLenum dfactorAlpha);

namespace notes {

namespace {


inline void blendOver()
{
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
}

constexpr float kEraseLife = 0.6f;


QByteArray glstr(GLenum name)
{
    const GLubyte* s = glGetString(name);
    return s ? QByteArray(reinterpret_cast<const char*>(s)) : QByteArray("(null)");
}

bool fboUsable(QOpenGLFramebufferObject* fbo)
{


    if (!fbo || !fbo->isValid() || fbo->texture() == 0) return false;
    if (!fbo->bind()) return false;
    static const auto statusFn = reinterpret_cast<GLenum (*)(GLenum)>(
        QOpenGLContext::currentContext()
            ? QOpenGLContext::currentContext()->getProcAddress("glCheckFramebufferStatus")
            : nullptr);
    const bool complete =
        statusFn ? statusFn(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE : true;
    fbo->release();
    return complete;
}

constexpr float kEraseHeadAlpha = 0.5f;

const QColor kCaretColor(0x30, 0x90, 0xff);

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

void pushQuadPts(std::vector<float>& v, const QPointF& a, const QPointF& b,
                 const QPointF& c, const QPointF& d, const QColor& col)
{
    const float r = col.redF(), g = col.greenF(), bl = col.blueF(), al = col.alphaF();
    const auto add = [&](const QPointF& p) {
        v.insert(v.end(), {float(p.x()), float(p.y()), r, g, bl, al});
    };
    add(a); add(b); add(c);
    add(a); add(c); add(d);
}


void pushPolyBorder(std::vector<float>& v, const QPointF* pts, int n, double t, const QColor& c)
{
    for (int i = 0; i < n; ++i) {
        const QPointF a = pts[i], b = pts[(i + 1) % n];
        const double dx = b.x() - a.x(), dy = b.y() - a.y();
        const double len = std::hypot(dx, dy);
        if (len < 1e-9) continue;
        const QPointF nrm(-dy / len * t, dx / len * t);
        pushQuadPts(v, a, b, b + nrm, a + nrm, c);
    }
}


void pushLocalBox(std::vector<float>& v, const QPointF& c, double rot, double hw,
                  const QColor& fill, const QColor& line, double bt)
{
    const QPointF corners[4] = { QPointF(-hw, -hw), QPointF(hw, -hw),
                                 QPointF(hw, hw), QPointF(-hw, hw) };
    QPointF w[4];
    for (int i = 0; i < 4; ++i) w[i] = toWorld(c + corners[i], c, rot);
    if (fill.alpha() > 0) pushQuadPts(v, w[0], w[1], w[2], w[3], fill);
    if (line.alpha() > 0) pushPolyBorder(v, w, 4, bt, line);
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


bool needsAlphaMask(const Stroke& s) { return s.color.alpha() < 255; }

}

void pruneEraserTrail(std::vector<EraserPt>& trail, float now)
{
    trail.erase(std::remove_if(trail.begin(), trail.end(),
        [now](const EraserPt& s) { return now - s.born > kEraseLife; }),
        trail.end());
}

GLRenderer::GLRenderer(QObject* parent) : QObject(parent) {}

GLRenderer::~GLRenderer()
{
    if (ssFbo_ && QOpenGLContext::currentContext())
        delete ssFbo_;
}

void GLRenderer::initialize(const QSurfaceFormat& format)
{
    if (initialized_) return;
    initializeOpenGLFunctions();
    glEnable(GL_MULTISAMPLE);
    GLint glMaxSamples = 0;
    noSsaa_ = qEnvironmentVariableIsSet("NOTES_NO_SSAA");
    alphaMaskOn_ = !qEnvironmentVariableIsSet("NOTES_NO_ALPHAMASK");
    glGetIntegerv(GL_MAX_SAMPLES, &glMaxSamples);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &glMaxTexture_);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &glMaxRenderbuffer_);
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

    initialized_ = true;
}

void GLRenderer::resize(int w, int h, qreal dpr)
{


    resizeW_ = w;
    resizeH_ = h;
    resizeDpr_ = dpr;
    fboStale_ = true;
}

QSize GLRenderer::ssSizeFor(int w, int h, qreal dpr) const
{
    const int lim = glMaxTexture_ > 0
        ? std::max(256, std::min(int(glMaxTexture_), int(glMaxRenderbuffer_)))
        : 16384;


    const int quantum = 32;
    int fw = int(std::ceil(w * kSupersample * dpr / double(quantum))) * quantum;
    int fh = int(std::ceil(h * kSupersample * dpr / double(quantum))) * quantum;


    const double fit = std::min(1.0, double(lim) / std::max(fw, fh));
    fw = std::max(quantum, int(fw * fit / quantum + 0.5) * quantum);
    fh = std::max(quantum, int(fh * fit / quantum + 0.5) * quantum);
    return QSize(fw, fh);
}

void GLRenderer::recreateFbo(int w, int h, qreal dpr)
{
    delete ssFbo_;
    ssFbo_ = nullptr;
    fboHasDepth_ = false;
    if (w <= 0 || h <= 0) return;

    const QSize size = ssSizeFor(w, h, dpr);
    lastFboSize_ = size;
    if (noSsaa_) return;
    ssFactor_ = float(size.width()) / float(std::max(1, int(w * dpr + 0.5f)));


    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::Depth);
    ssFbo_ = new QOpenGLFramebufferObject(size, format);
    if (fboUsable(ssFbo_)) {
        fboHasDepth_ = true;
    } else {
        delete ssFbo_;
        ssFbo_ = new QOpenGLFramebufferObject(size);
        if (!fboUsable(ssFbo_)) {
            delete ssFbo_;
            ssFbo_ = nullptr;
            if (!warnedNoFbo_) {
                warnedNoFbo_ = true;
                qWarning("Sin framebuffer de supermuestreo (%dx%d): se dibuja "
                         "directo en la ventana y sin máscara de alpha",
                         size.width(), size.height());
            }
            return;
        }
    }

    if (!fboHasDepth_ && !warnedNoFbo_) {
        warnedNoFbo_ = true;
        qWarning("El framebuffer %dx%d no admite profundidad: el resaltador "
                 "volverá a acumular transparencia", size.width(), size.height());
    }

    ssFbo_->bind();


    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssFbo_->texture());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    ssFbo_->release();
}

void GLRenderer::setAlphaMask(bool on)
{
    alphaMaskOn_ = on;
}

void GLRenderer::paint(const Frame& f)
{
    if (!initialized_ || f.viewW <= 0 || f.viewH <= 0)
        return;

    const qreal dpr = f.dpr;
    const QSize wanted = ssSizeFor(f.viewW, f.viewH, dpr);


    if (fboStale_ || (ssFbo_ ? (ssFbo_->size() != wanted) : (lastFboSize_ != wanted)))
        recreateFbo(f.viewW, f.viewH, dpr);
    fboStale_ = false;


    const bool direct = (ssFbo_ == nullptr);
    if (direct)
        glBindFramebuffer(GL_FRAMEBUFFER, f.defaultFbo);
    else
        ssFbo_->bind();
    const QSize target = direct
        ? QSize(std::max(1, int(f.viewW * dpr + 0.5f)), std::max(1, int(f.viewH * dpr + 0.5f)))
        : ssFbo_->size();
    glViewport(0, 0, GLsizei(target.width()), GLsizei(target.height()));


    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.99f, 0.99f, 0.99f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | (fboHasDepth_ ? GL_DEPTH_BUFFER_BIT : 0));
    depthCleared_ = fboHasDepth_;

    bgProg_->bind();
    bgProg_->setUniformValue("offset", f.cam.offset);
    bgProg_->setUniformValue("zoom", f.cam.zoom);
    bgProg_->setUniformValue("dpr", float(dpr));
    bgProg_->setUniformValue("ss", direct ? 1.0f : ssFactor_);
    bgProg_->setUniformValue("viewport", QVector2D(float(f.viewW), float(f.viewH)));
    bgProg_->setUniformValue("baseSpacing", float(kGridBaseSpacing));
    bgProg_->setUniformValue("minPx", float(kGridMinSpacing));
    bgProg_->setUniformValue("fineOnset", 0.78f);
    bgProg_->setUniformValue("lineWidth", 1.0f);
    bgProg_->setUniformValue("gridOrigin", QVector2D(float(kPageMargin), float(kPageMargin)));
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
    prog_->setUniformValue("viewport", QVector2D(float(f.viewW), float(f.viewH)));
    prog_->setUniformValue("offset", f.cam.offset);
    prog_->setUniformValue("zoom", f.cam.zoom);

    glEnable(GL_BLEND);
    blendOver();

    drawStrokes(f);
    drawEraser(f);

    if (f.hasSelection || f.marqueeActive)
        drawGizmo(f);

    glBindVertexArray(0);
    prog_->release();

    if (!direct) {
        ssFbo_->release();
        present(f);
    }
}


void GLRenderer::drawStrokes(const Frame& f)
{
    const bool mask = alphaMaskOn_ && fboHasDepth_;
    int translucent = 0;
    for (const auto& s : f.strokes)
        if (mask && needsAlphaMask(s)) ++translucent;
    if (mask && f.current && needsAlphaMask(*f.current)) ++translucent;
    int slice = translucent;

    const auto beginStroke = [&](const Stroke& s) {
        if (!mask || !needsAlphaMask(s)) return;
        if (!depthCleared_) {
            glDepthMask(GL_TRUE);
            glClear(GL_DEPTH_BUFFER_BIT);
            depthCleared_ = true;
        }
        --slice;
        const double base = double(slice) / translucent;
        glDepthRange(base, base + 0.5 / translucent);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    };
    const auto endStroke = [&](const Stroke& s) {
        if (mask && needsAlphaMask(s)) glDisable(GL_DEPTH_TEST);
    };

    glBindVertexArray(vao_[0]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_[0]);
    if (strokesDirty_) {
        vertsScratch_.clear();
        for (const auto& s : f.strokes)
            vertsScratch_.insert(vertsScratch_.end(), s.verts.begin(), s.verts.end());
        glBufferData(GL_ARRAY_BUFFER, vertsScratch_.size() * sizeof(float),
                     vertsScratch_.empty() ? nullptr : vertsScratch_.data(), GL_DYNAMIC_DRAW);
        strokesDirty_ = false;
    }
    size_t offset = 0;
    for (const auto& s : f.strokes) {
        if (s.verts.empty()) continue;
        beginStroke(s);
        glDrawArrays(GL_TRIANGLES, GLint(offset / 6), GLsizei(s.verts.size() / 6));
        endStroke(s);
        offset += s.verts.size();
    }

    glBindVertexArray(vao_[1]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_[1]);
    if (currentDirty_) {
        const size_t n = f.current ? f.current->verts.size() : 0;
        glBufferData(GL_ARRAY_BUFFER, n * sizeof(float),
                     (n && f.current) ? f.current->verts.data() : nullptr, GL_STREAM_DRAW);
        currentDirty_ = false;
    }
    if (f.current && !f.current->verts.empty()) {
        beginStroke(*f.current);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(f.current->verts.size() / 6));
        endStroke(*f.current);
    }
    if (translucent) glDepthRange(0.0, 1.0);
}

void GLRenderer::drawEraser(const Frame& f)
{
    glBindVertexArray(eraserVao_);
    glBindBuffer(GL_ARRAY_BUFFER, eraserVbo_);
    if (eraserDirty_) {
        eraserVerts_.clear();
        if (f.eraserTrail) {


            for (auto it = f.eraserTrail->rbegin(); it != f.eraserTrail->rend(); ++it) {
                const EraserPt& s = *it;
                const float age = f.eraseNow - s.born;
                if (age > kEraseLife) continue;
                const float a = age / kEraseLife;
                float k = std::clamp((1.0f - a) / 0.35f, 0.0f, 1.0f);
                k = k * k * (3.0f - 2.0f * k);
                const int segs = std::clamp(int(3.0f * s.r * f.cam.zoom * float(f.dpr)),
                                            12, 48);
                pushDisc(eraserVerts_, s.x, s.y, s.r, QColor(60, 150, 255),
                         kEraseHeadAlpha * k, segs);
            }
        }
        glBufferData(GL_ARRAY_BUFFER, eraserVerts_.size() * sizeof(float),
                     eraserVerts_.empty() ? nullptr : eraserVerts_.data(), GL_STREAM_DRAW);
        eraserDirty_ = false;
    }
    if (!eraserVerts_.empty()) {


        if (fboHasDepth_) {
            if (!depthCleared_) {
                glDepthMask(GL_TRUE);
                glClear(GL_DEPTH_BUFFER_BIT);
                depthCleared_ = true;
            }


            glDepthRange(0.0, 1.0 / 1024.0);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
        }
        blendOver();
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(eraserVerts_.size() / 6));
        if (fboHasDepth_) {
            glDisable(GL_DEPTH_TEST);
            glDepthRange(0.0, 1.0);
        }
    }
}

void GLRenderer::drawGizmo(const Frame& f)
{
    std::vector<float> v;
    v.reserve(512);
    const double hw = 4.0 / f.cam.zoom;
    const double bw = 2.0 / f.cam.zoom;

    const float g = std::clamp(f.gizmoAlpha, 0.0f, 1.0f);
    const auto col = [g](int r, int gg, int b, int a) {
        return QColor(r, gg, b, int(a * g + 0.5f));
    };

    if (f.marqueeActive && !f.marquee.isNull()) {
        pushQuad(v, f.marquee.left(), f.marquee.top(),
                 f.marquee.right(), f.marquee.bottom(),
                 col(90, 150, 255, 28));
        pushBorder(v, f.marquee, bw, col(60, 120, 255, 230));
    }

    if (f.hasSelection && !f.selectionBox.isNull()) {
        const QRectF b = f.selectionBox.normalized();
        const double rot = f.selectionRot;
        const QPointF o = b.center();
        const QColor line = col(60, 110, 255, 255);
        const QColor fill = col(255, 255, 255, 255);
        const QPointF corners[4] = { toWorld(b.topLeft(), o, rot), toWorld(b.topRight(), o, rot),
                                     toWorld(b.bottomRight(), o, rot), toWorld(b.bottomLeft(), o, rot) };
        pushPolyBorder(v, corners, 4, bw, line);
        const Handle hs[8] = { Handle::N, Handle::NE, Handle::E, Handle::SE,
                               Handle::S, Handle::SW, Handle::W, Handle::NW };
        for (Handle h : hs) {
            const QPointF c = handlePos(b, h, rot);
            pushLocalBox(v, c, rot, hw, fill, line, bw * 0.5);
        }
        if (std::min(b.width(), b.height()) * f.cam.zoom > 24.0) {
            const double r = 5.0 / f.cam.zoom;
            pushArc(v, rotateHandlePos(b, rot, kRotateOffsetPx / f.cam.zoom).x(),
                    rotateHandlePos(b, rot, kRotateOffsetPx / f.cam.zoom).y(),
                    r - bw * 0.4, r, 0.0, 2.0 * 3.14159265358979323846, line, 20);
        }
    }

    if (v.empty()) return;
    glBindVertexArray(gizVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gizVbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_DYNAMIC_DRAW);
    glEnable(GL_BLEND);
    blendOver();
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(v.size() / 6));
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void GLRenderer::present(const Frame& f)
{
    glBindFramebuffer(GL_FRAMEBUFFER, f.defaultFbo);
    glViewport(0, 0, GLsizei(f.viewW * f.dpr), GLsizei(f.viewH * f.dpr));
    glDisable(GL_BLEND);
    presentProg_->bind();
    presentProg_->setUniformValue("uTex", 0);
    const QSize fsz = ssFbo_->size();


    const QVector2D texel = presentMode_ == 0
        ? QVector2D(1.0f / float(fsz.width()), 1.0f / float(fsz.height()))
        : QVector2D(0.0f, 0.0f);
    presentProg_->setUniformValue("uTexelSize", texel);
    const GLuint tex = ssFbo_->texture();
    if (tex == 0) {
        presentProg_->release();
        return;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(presentVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    presentProg_->release();
}

void GLRenderer::paintOverlay(QPainter& p, const Frame& f)
{
    const QPointF offset(f.cam.offset.x(), f.cam.offset.y());
    for (const ShapeItem& sh : f.shapes)
        drawShapeItem(p, sh, f.cam.zoom, offset);
    if (f.dragShape)
        drawShapeItem(p, *f.dragShape, f.cam.zoom, offset);
    for (const ImageItem& im : f.images)
        drawImageItem(p, im, f.cam.zoom, offset);
    for (const TextBox& t : f.texts)
        drawTextBox(p, t, f.cam.zoom, offset,
                    f.editingTextId == t.id, f.textCursor, kCaretColor,
                    f.editingTextId == t.id ? f.preeditStart : -1, f.preeditLen);
}

}
