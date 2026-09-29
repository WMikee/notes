#include "canvas/canvas.h"
#include "ui/anim.h"
#include "io/clipboard.h"
#include "io/storage.h"
#include "scene/page.h"
#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTabletEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr float kHighlightAlpha = 0.45f;
constexpr int kMaxEraseTrailSteps = 48;

QCursor pencilCursor()
{
    QPixmap pm(QStringLiteral(":/assets/pencil-cursor.png"));
    if (pm.isNull()) return QCursor(Qt::CrossCursor);
    return QCursor(pm, 4, pm.height() - 4);
}
}

namespace notes {

Canvas::Canvas(QWidget* parent)
    : QOpenGLWidget(parent),
      toolCtx_(doc_, cam_,
               [this] { renderer_.invalidateStrokes(); update(); },
               [this] { update(); })
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
        const float now = float(eraseClock_.elapsed()) / 1000.0f;
        pruneEraserTrail(eraserTrail_, now);
        if (eraserTrail_.empty())
            eraseTimer_->stop();
        renderer_.invalidateEraser();
        update();
    });

    selTool_ = new SelectTool(this);
    connect(selTool_, &SelectTool::gizmoAppeared, this, &Canvas::animateGizmoIn);
    connect(selTool_, &SelectTool::gizmoCleared, this, &Canvas::resetGizmo);
}

void Canvas::setTool(ToolId t)
{
    if (tool_ == t) return;
    if (tool_ == ToolId::Curve) curveAbort();
    finishInput();
    tool_ = t;
    if (t == ToolId::Curve) selTool_->clear(ctx());
    updateCursor();
}

void Canvas::updateCursor()
{
    switch (tool_) {
    case ToolId::Pencil: setCursor(pencilCursor()); break;
    case ToolId::Highlighter: setCursor(pencilCursor()); break;
    case ToolId::Eraser: setCursor(pencilCursor()); break;
    case ToolId::Shape:  setCursor(pencilCursor()); break;
    case ToolId::Curve:  setCursor(Qt::CrossCursor); break;
    case ToolId::Select: setCursor(Qt::ArrowCursor); break;
    case ToolId::Text:   setCursor(Qt::IBeamCursor); break;
    }
}

void Canvas::setEraserMode(EraserMode m)
{
    if (eraserMode_ == m) return;
    if (tool_ == ToolId::Eraser) finishInput();
    eraserMode_ = m;
    renderer_.invalidateEraser();
    update();
}

void Canvas::setShapeKind(ShapeKind k)
{
    if (shapeKind_ == k) return;
    if (tool_ == ToolId::Shape) finishInput();
    shapeKind_ = k;
    update();
}

void Canvas::setColor(const QColor& c)
{
    if (color_ == c) return;
    finishInput();
    color_ = c;
}

void Canvas::setFixedGrid(bool on)
{
    if (fixedGrid_ == on) return;
    fixedGrid_ = on;
    update();
}

void Canvas::setPressureEnabled(bool on)
{
    if (pressureEnabled_ == on) return;
    pressureEnabled_ = on;
}

void Canvas::setStabilizerEnabled(bool on)
{
    stabilizerEnabled_ = on;
}

void Canvas::setPostSmoothEnabled(bool on)
{
    postSmoothEnabled_ = on;
}

void Canvas::setHighlightBelow(bool on)
{
    if (highlightBelow_ == on) return;
    highlightBelow_ = on;
    update();
}

void Canvas::setStrokeSize(float size)
{
    size_ = std::clamp(size, 0.5f, 200.0f);
}

void Canvas::setEraserRadius(float radius)
{
    eraseRadius_ = std::clamp(radius, 0.5f, 200.0f);
    renderer_.invalidateEraser();
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
    for (int id : selTool_->selection()) {
        TextBox* tb = doc_.textById(id);
        if (!tb) continue;
        before.push_back(*tb);
        tb->fontPx = fontSize_;
        tb->layoutDirty = true;
        after.push_back(*tb);
    }
    if (!after.empty()) {
        doc_.commitTextTransform(before, after);
        renderer_.invalidateStrokes();
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
    for (int id : selTool_->selection()) {
        ShapeItem* sh = doc_.shapeById(id);
        if (!sh) continue;
        before.push_back(*sh);
        sh->penWidth = shapePenWidth_;
        after.push_back(*sh);
    }
    if (!after.empty()) {
        doc_.commitShapeTransform(before, after);
        renderer_.invalidateStrokes();
        update();
    }
}

void Canvas::undo()
{
    if (editingText_ >= 0) finishEditText();
    doc_.undo();
    selTool_->invalidateBox();
    renderer_.invalidateStrokes();
    update();
}

void Canvas::redo()
{
    if (editingText_ >= 0) finishEditText();
    doc_.redo();
    selTool_->invalidateBox();
    renderer_.invalidateStrokes();
    update();
}

void Canvas::copy()
{
    if (selTool_->selection().empty()) return;
    if (editingText_ >= 0) finishEditText();
    ClipboardData data;
    data.strokes = doc_.snapshot(selTool_->selection());
    data.texts = doc_.textSnapshot(selTool_->selection());
    data.images = doc_.imageSnapshot(selTool_->selection());
    data.shapes = doc_.shapeSnapshot(selTool_->selection());
    if (data.strokes.empty() && data.texts.empty() && data.images.empty() && data.shapes.empty()) return;
    QMimeData* mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-notes"), serializeClipboard(data));
    QGuiApplication::clipboard()->setMimeData(mime);
}

void Canvas::cut()
{
    if (selTool_->selection().empty()) return;
    copy();
    selTool_->deleteSelected(ctx());
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
            const QRectF r = t.rect();
            grow(r.left(), r.top());
            grow(r.right(), r.bottom());
        }
        for (const ImageItem& im : data.images) {
            const QRectF r = im.rect();
            grow(r.left(), r.top());
            grow(r.right(), r.bottom());
        }
        for (const ShapeItem& sh : data.shapes) {
            const QRectF r = sh.bounds();
            grow(r.left(), r.top());
            grow(r.right(), r.bottom());
        }
        const QPointF viewCenter = cam_.toWorld(QPointF(width() / 2.0, height() / 2.0));
        const QPointF delta = first
            ? QPointF(0, 0)
            : (viewCenter - box.center());

        std::vector<int> picked;
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
picked.push_back(s.id);
            added.push_back(std::move(s));
        }
        for (TextBox t : data.texts) {
            t.id = doc_.nextId();
            t.pos += delta;
            t.layoutDirty = true;
picked.push_back(t.id);
            addedTexts.push_back(std::move(t));
        }
        for (ImageItem im : data.images) {
            im.id = doc_.nextId();
            im.pos += delta;
picked.push_back(im.id);
            addedImages.push_back(std::move(im));
        }
        for (ShapeItem sh : data.shapes) {
            sh.id = doc_.nextId();
            sh.rect.translate(delta);
picked.push_back(sh.id);
            addedShapes.push_back(std::move(sh));
        }
doc_.commitAdd(std::move(added), std::move(addedTexts),
                   std::move(addedImages), std::move(addedShapes));
        selTool_->setSelection(std::move(picked), doc_);
        renderer_.invalidateStrokes();
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
    doc_.commitAdd({}, {}, {im}, {});
    selTool_->setSelection({ im.id }, doc_);
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
    selTool_->clear(ctx());
    for (Stroke& s : data.strokes)
        s.verts.clear();
    doc_.replaceAll(std::move(data.strokes), std::move(data.texts),
                    std::move(data.images), std::move(data.shapes), data.nextId);
    cam_ = data.camera;
    selTool_->invalidateBox();
    renderer_.invalidateStrokes();
    update();
    return true;
}

void Canvas::newDocument()
{
    if (editingText_ >= 0) finishEditText();
    selTool_->clear(ctx());
    doc_.replaceAll({}, {}, {}, {}, 1);
    cam_ = Camera{};
    selTool_->invalidateBox();
    renderer_.invalidateStrokes();
    update();
}

void Canvas::initializeGL()
{
    renderer_.initialize(format());
}

void Canvas::resizeGL(int w, int h)
{
    cam_.setViewSize(QSizeF(double(w), double(h)));
    renderer_.resize(w, h, devicePixelRatioF());
}

void Canvas::paintGL()
{
    const bool canUndo = doc_.undoAvailable();
    const bool canRedo = doc_.redoAvailable();
    if (canUndo != lastCanUndo_ || canRedo != lastCanRedo_) {
        lastCanUndo_ = canUndo;
        lastCanRedo_ = canRedo;
        emit historyChanged(canUndo, canRedo);
    }

    ToolContext& c = ctx();
    Frame f(cam_, doc_.strokes(), doc_.shapes(), doc_.images(), doc_.texts());
    f.current = &cur_;
    f.eraserTrail = &eraserTrail_;
    f.eraseNow = float(eraseClock_.elapsed()) / 1000.0f;
    if (shapeDragging_ || (tool_ == ToolId::Curve && !curvePts_.empty()))
        f.dragShape = &dragShape_;

    f.curveEditId = -1;
    const bool curveToolEditing = (tool_ == ToolId::Curve && curveEditId_ >= 0);
    if (curveToolEditing) {
        f.curveEditId = curveEditId_;
    } else if (tool_ == ToolId::Select && selTool_->selection().size() == 1) {
        const ShapeItem* sh = doc_.shapeById(selTool_->selection().front());
        if (sh && sh->kind == ShapeKind::Curve) f.curveEditId = sh->id;
    }

    if (!selTool_->selection().empty() && !curveToolEditing) {
        f.selectionBox = selTool_->gizmoBox(c);
        f.selectionRot = float(selTool_->gizmoRot());
        f.hasSelection = true;
    }
    f.marquee = selTool_->marqueeRect();
    f.marqueeActive = selTool_->marqueeActive();
    f.gizmoAlpha = gizmoAlpha_;
    f.editingTextId = editingText_;
    f.textCursor = textCursor_;
    f.preeditStart = preeditStart_;
    f.preeditLen = preeditLen_;
    f.viewW = width();
    f.viewH = height();
    f.dpr = devicePixelRatioF();
    f.defaultFbo = defaultFramebufferObject();
    f.fixedGrid = fixedGrid_;
    f.highlightBelow = highlightBelow_;

    renderer_.paint(f);

    QPainter p(this);
    GLRenderer::paintOverlay(p, f);
    p.end();
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
            if (tool_ == ToolId::Pencil || tool_ == ToolId::Highlighter) beginStroke(p, pressureValue(float(e->pressure())));
            else if (tool_ == ToolId::Eraser) beginErase(p);
            else if (tool_ == ToolId::Select) { stylusMode_ = Gesture::Select; selTool_->onPress({ p, 0.5f, (e->modifiers() & Qt::ShiftModifier) != 0 }, ctx()); }
            else if (tool_ == ToolId::Shape) beginShape(p);
            else if (tool_ == ToolId::Curve) { stylusMode_ = Gesture::None; curvePress(p); }
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
                selTool_->onMove({ p, 0.5f, (e->modifiers() & Qt::ShiftModifier) != 0 }, ctx());
            } else if (stylusMode_ == Gesture::Draw) {
                if (tool_ == ToolId::Pencil || tool_ == ToolId::Highlighter) addPoint(p, pressureValue(float(e->pressure())));
                else if (tool_ == ToolId::Eraser) eraseAt(p);
                else if (tool_ == ToolId::Shape) updateShape(p);
            } else if (tool_ == ToolId::Select) {
                stylusMode_ = Gesture::Select;
                selTool_->onPress({ p, 0.5f, (e->modifiers() & Qt::ShiftModifier) != 0 }, ctx());
            } else if (tool_ == ToolId::Text) {
                stylusMode_ = Gesture::None;
            } else if (tool_ == ToolId::Shape) {
                stylusMode_ = Gesture::Draw;
                beginShape(p);
            } else if (tool_ == ToolId::Curve) {
                curveMove(p);
            } else {
                stylusMode_ = Gesture::Draw;
                if (tool_ == ToolId::Pencil || tool_ == ToolId::Highlighter) beginStroke(p, pressureValue(float(e->pressure())));
                else beginErase(p);
            }
        }
        break;
    case QEvent::TabletRelease:
        if (stylusMode_ == Gesture::Draw || stylusMode_ == Gesture::Select) finishInput();
        if (tool_ == ToolId::Curve) curveRelease();
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
    if (tool_ == ToolId::Pencil || tool_ == ToolId::Highlighter) beginStroke(e->position(), 0.5f);
    else if (tool_ == ToolId::Eraser) beginErase(e->position());
    else if (tool_ == ToolId::Shape) beginShape(e->position());
    else if (tool_ == ToolId::Curve) curvePress(e->position());
    else if (tool_ == ToolId::Text) { textEditJustStarted_ = true; textBegin(e->position()); }
    else selTool_->onPress({ e->position(), 0.5f, (e->modifiers() & Qt::ShiftModifier) != 0 }, ctx());
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
    else if (tool_ == ToolId::Curve) curveMove(e->position());
    else if (selTool_->dragging())
        selTool_->onMove({ e->position(), 0.5f, (e->modifiers() & Qt::ShiftModifier) != 0 }, ctx());
}

void Canvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        panning_ = false;
        return;
    }
    if (tool_ == ToolId::Text) {
        if (textEditJustStarted_) {
            textEditJustStarted_ = false;
            return;
        }
    }
    if (tool_ == ToolId::Curve) {
        curveRelease();
        return;
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

bool Canvas::event(QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (editingText_ >= 0 && ke->modifiers() == Qt::NoModifier) {
            switch (ke->key()) {
            case Qt::Key_V:
            case Qt::Key_B:
            case Qt::Key_H:
            case Qt::Key_E:
            case Qt::Key_T:
            case Qt::Key_F:
            case Qt::Key_C:
                e->accept();
                return true;
            default:
                break;
            }
        } else if (tool_ == ToolId::Curve && !curvePts_.empty()
                   && ke->key() == Qt::Key_Z && ke->modifiers() == Qt::ControlModifier) {
            e->accept();
            return true;
        }
    }
    return QOpenGLWidget::event(e);
}

void Canvas::keyPressEvent(QKeyEvent* e)
{
    if (editingText_ >= 0) {
        textKeyPress(e);
        return;
    }
    if (e->key() == Qt::Key_F9) {
        renderer_.setPresentMode(renderer_.presentMode() == 0 ? 1 : 0);
        renderer_.setAlphaMask(renderer_.alphaMask());
        update();
        return;
    }
    if (e->key() == Qt::Key_F10) {
        renderer_.setAlphaMask(!renderer_.alphaMask());
        update();
        return;
    }
    if (e->key() == Qt::Key_Delete) { deleteSelection(); return; }
    if (tool_ == ToolId::Curve) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { curveFinish(); return; }
        if (e->key() == Qt::Key_Escape) { curveCancel(); return; }
    }
    if (e->modifiers() & Qt::ControlModifier) {
        if (e->key() == Qt::Key_C) { copy(); return; }
        if (e->key() == Qt::Key_X) { cut(); return; }
        if (e->key() == Qt::Key_V) { paste(); return; }
        if (e->key() == Qt::Key_Z && tool_ == ToolId::Curve && !curvePts_.empty()) {
            curvePts_.pop_back();
            if (!curvePts_.empty())
                curveRebuildDraft(false);
            else
                dragShape_ = ShapeItem{};
            update();
            return;
        }
    }
    const bool mods = (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0;
    if (e->key() == Qt::Key_Shift)
        shiftDown_ = true;
    else if (e->key() == Qt::Key_Space && !mods)
        spaceDown_ = true;
    else if (e->key() == Qt::Key_Z && e->modifiers() == Qt::NoModifier)
        zDown_ = true;
    else if (e->key() == Qt::Key_Escape) {
        selTool_->finish(ctx());
        selTool_->clear(ctx());
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
    cur_.stabilized = stabilizerEnabled_;
    if (tool_ == ToolId::Highlighter) {
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
        renderer_.invalidateCurrent();
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
    renderer_.invalidateCurrent();
    update();
}

void Canvas::endStroke()
{
    if (!drawing_) return;
    drawing_ = false;
    if (!cur_.pts.empty()) {
        if (postSmoothEnabled_)
            smoothStrokePoints(cur_.pts);
        cur_.complete = true;
        retessellate(cur_);
        doc_.add(cur_);
        renderer_.invalidateStrokes();
    }
    cur_ = Stroke{};
    renderer_.invalidateCurrent();
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
    const int n = std::min(kMaxEraseTrailSteps,
                           std::max(1, int(std::ceil(dist / step))));
    const float now = float(eraseClock_.elapsed()) / 1000.0f;
    for (int i = 0; i < n; ++i) {
        const float t = float(i + 1) / float(n);
        const QPointF sp(prev.x() + (w.x() - prev.x()) * t,
                         prev.y() + (w.y() - prev.y()) * t);
        eraserTrail_.push_back({float(sp.x()), float(sp.y()), r, now});
    }

    Document::EraseResult removed = (eraserMode_ == EraserMode::Partial)
        ? doc_.erasePartial(prev, w, r)
        : doc_.eraseNear(prev, w, r);
    for (auto& s : removed.strokes) eraseBatch_.push_back(std::move(s));
    for (auto& t2 : removed.texts) textEraseBatch_.push_back(std::move(t2));
    for (auto& im : removed.images) imageEraseBatch_.push_back(std::move(im));
    for (auto& sh : removed.shapes) shapeEraseBatch_.push_back(std::move(sh));
    for (auto& s : removed.added) partialAddBatch_.push_back(std::move(s));

    if (!eraseTimer_->isActive())
        eraseTimer_->start();
    renderer_.invalidateStrokes();
    renderer_.invalidateEraser();
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
    renderer_.invalidateEraser();
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
    selTool_->setSelection({ dragShape_.id }, doc_);
    update();
}

void Canvas::curveRebuildDraft(bool withPreview)
{
    std::vector<QPointF> pts = curvePts_;
    if (withPreview && !pts.empty())
        pts.push_back(curvePreview_);
    if (pts.empty()) return;
    dragShape_ = ShapeItem{};
    curveBuildFromPoints(pts, dragShape_);
    dragShape_.color = color_;
    dragShape_.penWidth = shapePenWidth_;
}

int Canvas::curveCommitDraft()
{
    if (curvePts_.size() < 2) return -1;
    curveRebuildDraft(false);
    dragShape_.id = doc_.nextId();
    doc_.addShape(dragShape_);
    return dragShape_.id;
}

void Canvas::curvePress(const QPointF& screen)
{
    const QPointF w = cam_.toWorld(screen);
    if (curvePts_.empty() && curveEditId_ >= 0) {
        ShapeItem* sh = doc_.shapeById(curveEditId_);
        if (sh && sh->kind == ShapeKind::Curve) {
            const CurveHit hit = curveHit(*sh, w, 10.0 / cam_.zoom);
            if (hit.part != CurvePart::None) {
                curveDragging_ = true;
                curveNode_ = hit.index;
                curvePart_ = hit.part;
                curveBefore_ = *sh;
                curveMoved_ = false;
                update();
                return;
            }
        }
        curveEditId_ = -1;
    }
    if (!curvePts_.empty()) {
        const QPointF& last = curvePts_.back();
        const double minWorld = 4.0 / std::max(cam_.zoom, 0.01f);
        if (std::hypot(w.x() - last.x(), w.y() - last.y()) < minWorld)
            return;
    }
    curvePts_.push_back(w);
    curvePreview_ = w;
    curveRebuildDraft(false);
    update();
}

void Canvas::curveMove(const QPointF& screen)
{
    const QPointF w = cam_.toWorld(screen);
    if (curveDragging_) {
        ShapeItem* sh = doc_.shapeById(curveEditId_);
        if (!sh) {
            curveDragging_ = false;
            return;
        }
        if (curvePart_ == CurvePart::Node)
            curveMoveNode(*sh, curveNode_, w);
        else
            curveMoveHandle(*sh, curveNode_, curvePart_ == CurvePart::HandleOut, w);
        curveMoved_ = true;
        update();
        return;
    }
    if (!curvePts_.empty()) {
        curvePreview_ = w;
        curveRebuildDraft(true);
        update();
    }
}

void Canvas::curveRelease()
{
    if (!curveDragging_) return;
    curveDragging_ = false;
    if (curveMoved_) {
        ShapeItem* sh = doc_.shapeById(curveEditId_);
        if (sh) doc_.commitShapeTransform({ curveBefore_ }, { *sh });
    }
    curveMoved_ = false;
    update();
}

void Canvas::curveFinish()
{
    if (!curvePts_.empty()) {
        const int id = curveCommitDraft();
        if (id >= 0) {
            selTool_->setSelection({ id }, doc_);
            curveEditId_ = id;
        }
        curvePts_.clear();
        curvePreview_ = QPointF();
        dragShape_ = ShapeItem{};
        update();
        return;
    }
    if (curveEditId_ >= 0) {
        curveEditId_ = -1;
        update();
    }
}

void Canvas::curveCancel()
{
    if (!curvePts_.empty()) {
        curvePts_.clear();
        curvePreview_ = QPointF();
        dragShape_ = ShapeItem{};
        update();
        return;
    }
    if (curveEditId_ >= 0) {
        curveEditId_ = -1;
        update();
    }
}

void Canvas::curveAbort()
{
    if (curveDragging_) {
        curveDragging_ = false;
        if (curveMoved_) {
            ShapeItem* sh = doc_.shapeById(curveEditId_);
            if (sh) doc_.commitShapeTransform({ curveBefore_ }, { *sh });
        }
        curveMoved_ = false;
    }
    if (!curvePts_.empty()) {
        const int id = curveCommitDraft();
        if (id >= 0)
            selTool_->setSelection({ id }, doc_);
        curvePts_.clear();
        curvePreview_ = QPointF();
        dragShape_ = ShapeItem{};
    }
    curveEditId_ = -1;
    curveDragging_ = false;
    curveMoved_ = false;
    update();
}

void Canvas::finishInput()
{
    if (tool_ == ToolId::Pencil || tool_ == ToolId::Highlighter) endStroke();
    else if (tool_ == ToolId::Eraser) endErase();
    else if (tool_ == ToolId::Text) finishEditText();
    else if (tool_ == ToolId::Shape) endShape();
    else if (tool_ == ToolId::Select) selTool_->finish(ctx());
}

void Canvas::deleteSelection()
{
    if (editingText_ >= 0) return;
    selTool_->deleteSelected(ctx());
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
        selTool_->forget(id);
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

void Canvas::resetGizmo()
{
    if (gizmoAnim_) {
        gizmoAnim_->stop();
        gizmoAnim_->deleteLater();
        gizmoAnim_ = nullptr;
    }
    gizmoAlpha_ = 1.0f;
}

void Canvas::animateGizmoIn()
{
    resetGizmo();
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

}
