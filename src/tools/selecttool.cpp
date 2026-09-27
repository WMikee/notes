#include "tools/selecttool.h"
#include <algorithm>
#include <cmath>

namespace notes {

namespace {

constexpr float kHandleTolPx = 10.0f;
constexpr float kRotateTolPx = 13.0f;
constexpr float kClickTolPx = 6.0f;
constexpr float kGrabPx = 6.0f;
constexpr double kSnapAngle = 3.14159265358979323846 / 12.0;

double rotationOf(const Document& doc, int id)
{
    if (const TextBox* t = doc.textById(id)) return t->rot;
    if (const ImageItem* im = doc.imageById(id)) return im->rot;
    if (const ShapeItem* sh = doc.shapeById(id)) return sh->rot;
    return 0.0;
}

}

SelectTool::SelectTool(QObject* parent) : QObject(parent) {}

void SelectTool::setSelection(std::vector<int> ids, const Document& doc)
{
    selection_ = std::move(ids);
    boxValid_ = false;
    boxRot_ = selection_.size() == 1 ? rotationOf(doc, selection_.front()) : 0.0;
    emit gizmoAppeared();
}

void SelectTool::clear(ToolContext& ctx)
{
    selection_.clear();
    boxValid_ = false;
    boxRot_ = 0.0;
    forgetSnapshots();
    emit gizmoCleared();
    ctx.repaint();
}

void SelectTool::forget(int id)
{
    selection_.erase(std::remove(selection_.begin(), selection_.end(), id), selection_.end());
}

void SelectTool::forgetSnapshots()
{
    beforeStrokes_.clear();
    beforeTexts_.clear();
    beforeImages_.clear();
    beforeShapes_.clear();
}

void SelectTool::collectSnapshots(const Document& doc)
{
    beforeStrokes_ = doc.snapshot(selection_);
    beforeTexts_ = doc.textSnapshot(selection_);
    beforeImages_ = doc.imageSnapshot(selection_);
    beforeShapes_ = doc.shapeSnapshot(selection_);
}

void SelectTool::deleteSelected(ToolContext& ctx)
{
    if (selection_.empty()) return;
    ctx.doc.commitRemove(selection_);
    clear(ctx);
    ctx.invalidate();
}

QRectF SelectTool::gizmoBox(ToolContext& ctx)
{
    if (boxValid_) return boxWorld_;
    boxValid_ = true;
    boxWorld_ = QRectF();
    if (selection_.empty()) return boxWorld_;

    Document& doc = ctx.doc;
    selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
        [&doc](int id) {
            return doc.strokeById(id) == nullptr && doc.textById(id) == nullptr
                && doc.imageById(id) == nullptr && doc.shapeById(id) == nullptr;
        }),
        selection_.end());


    std::vector<QPointF> pts, flat;
    auto grow = [&pts, &flat](const QPointF& p) {
        pts.push_back(p);
        flat.push_back(p);
    };
    auto growRot = [&pts, &flat](const QRectF& r, const QPointF& anchor, double rot) {
        const QPointF c[4] = { r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft() };
        for (const QPointF& p : c) {
            pts.push_back(rotateAbout(p, anchor, rot));
            flat.push_back(p);
        }
    };
    for (int id : selection_) {
        if (const Stroke* s = doc.strokeById(id)) {
            for (const Pt& p : s->pts) grow(QPointF(p.x, p.y));
        }
    }
    for (int id : selection_) {
        if (const TextBox* t = doc.textById(id)) {
            relayoutTextBox(*t);
            growRot(t->localRect(), t->anchor(), t->rot);
        }
    }
    for (int id : selection_) {
        if (const ImageItem* im = doc.imageById(id))
            growRot(im->localRect(), im->anchor(), im->rot);
    }
    for (int id : selection_) {
        if (const ShapeItem* sh = doc.shapeById(id))
            growRot(sh->localRect(), sh->anchor(), sh->rot);
    }
    if (pts.empty()) return boxWorld_;


    const QPointF pivot = boundsOfPoints(flat).center();
    const QPointF frameCenter = boundsOfPoints(pts).center();

    for (QPointF& p : pts) p = toLocal(p, pivot, boxRot_);
    QRectF frame = boundsOfPoints(pts);
    frame.translate(-frame.center());
    frame.translate(frameCenter);


    boxWorld_ = frame;
    return boxWorld_;
}

double SelectTool::rotateOffset() const
{
    return kRotateOffsetPx;
}

void SelectTool::collect(const Document& doc, const QRectF& world, std::vector<int>& out)
{
    for (const auto& s : doc.strokes())
        if (strokeHitsRect(s, world)) out.push_back(s.id);
    for (const auto& t : doc.texts())
        if (textHitsRect(t, world)) out.push_back(t.id);
    for (const auto& im : doc.images())
        if (imageHitsRect(im, world)) out.push_back(im.id);
    for (const auto& sh : doc.shapes())
        if (shapeHitsRect(sh, world)) out.push_back(sh.id);
}

void SelectTool::onPress(const InputPoint& p, ToolContext& ctx)
{
    const QPointF w = ctx.cam.toWorld(p.pos);


    if (!selection_.empty()) {
        const QRectF b = gizmoBox(ctx);
        if (!b.isNull()) {
            const double minPx = std::min(b.width(), b.height()) * ctx.cam.zoom;
            if (minPx > 24.0) {
                const Handle h = hitHandle(b, w, kHandleTolPx / ctx.cam.zoom,
                                           boxRot_, rotateOffset() / ctx.cam.zoom,
                                           kRotateTolPx / ctx.cam.zoom);
                if (h == Handle::Rotate) {
                    handle_ = Handle::Rotate;
                    dragStartRot_ = boxRot_;
                    dragAnchorWorld_ = b.center();
                    dragStartAngle_ = std::atan2(w.y() - b.center().y(),
                                                 w.x() - b.center().x());
                    forgetSnapshots();
                    collectSnapshots(ctx.doc);
                    moved_ = false;
                    dragLastScreen_ = p.pos;
                    drag_ = true;
                    return;
                }
                if (h != Handle::None) {
                    handle_ = h;
                    dragStartWorld_ = b;
                    dragAnchorWorld_ = handleAnchor(b, h, boxRot_);
                    forgetSnapshots();
                    collectSnapshots(ctx.doc);
                    moved_ = false;
                    dragLastScreen_ = p.pos;
                    drag_ = true;
                    return;
                }
            }
            const double grab = kGrabPx / ctx.cam.zoom;
            if (b.adjusted(-grab, -grab, grab, grab).contains(w)) {
                beginDrag(p.pos, ctx);
                return;
            }
        }
    }

    int hit = imageAt(ctx.doc.images(), w, kClickTolPx / ctx.cam.zoom);
    if (hit < 0) hit = textAt(ctx.doc.texts(), w, kClickTolPx / ctx.cam.zoom);
    if (hit < 0) hit = shapeAt(ctx.doc.shapes(), w, kClickTolPx / ctx.cam.zoom);
    if (hit < 0) hit = strokeAt(ctx.doc.strokes(), w, kClickTolPx / ctx.cam.zoom);
    if (hit >= 0) {
        if (std::find(selection_.begin(), selection_.end(), hit) == selection_.end())
            setSelection({ hit }, ctx.doc);
        beginDrag(p.pos, ctx);
        return;
    }

    drag_ = true;
    handle_ = Handle::None;
    marqueeActive_ = true;
    marqueeAnchorWorld_ = w;
    marqueeRect_ = QRectF(w, w);
    if (selection_.empty()) emit gizmoAppeared();
}

void SelectTool::beginDrag(const QPointF& screen, ToolContext& ctx)
{
    handle_ = Handle::Move;
    dragLastScreen_ = screen;
    collectSnapshots(ctx.doc);
    moved_ = false;
    drag_ = true;
}

void SelectTool::onMove(const InputPoint& p, ToolContext& ctx)
{
    if (handle_ == Handle::None) {
        marqueeRect_ = QRectF(marqueeAnchorWorld_, ctx.cam.toWorld(p.pos)).normalized();
        ctx.repaint();
        return;
    }
    if (handle_ == Handle::Move) {
        const QPointF d = ctx.cam.toWorld(p.pos) - ctx.cam.toWorld(dragLastScreen_);
        dragLastScreen_ = p.pos;
        if (d.x() != 0 || d.y() != 0) {
            translate(d, ctx);
            moved_ = true;
        }
        return;
    }
    if (handle_ == Handle::Rotate) {
        rotate(ctx.cam.toWorld(p.pos), p.shift, ctx);
        moved_ = true;
        return;
    }
    scale(ctx.cam.toWorld(p.pos), ctx);
    moved_ = true;
}

void SelectTool::onRelease(const InputPoint& p, ToolContext& ctx)
{
    Q_UNUSED(p);
    const bool wasMarquee = (handle_ == Handle::None);
    drag_ = false;
    if (wasMarquee) endMarquee(ctx);
    else commitDrag(ctx);
}

void SelectTool::finish(ToolContext& ctx)
{
    if (!drag_) return;
    drag_ = false;
    if (handle_ == Handle::None) endMarquee(ctx);
    else commitDrag(ctx);
}

void SelectTool::endMarquee(ToolContext& ctx)
{
    handle_ = Handle::Move;
    marqueeActive_ = false;
    const QRectF r = marqueeRect_;
    marqueeRect_ = QRectF();
    if (r.isNull()) {
        clear(ctx);
        return;
    }
    const double pw = r.width() * ctx.cam.zoom, ph = r.height() * ctx.cam.zoom;
    if (pw < 5 && ph < 5) {
        clear(ctx);
        return;
    }
    std::vector<int> ids;
    collect(ctx.doc, r, ids);
    if (ids.empty()) {
        clear(ctx);
        return;
    }
    setSelection(std::move(ids), ctx.doc);
    ctx.repaint();
}

void SelectTool::commitDrag(ToolContext& ctx)
{
    handle_ = Handle::Move;
    if (moved_) {
        Document& doc = ctx.doc;
        doc.commitTransform(beforeStrokes_, doc.snapshot(selection_));
        doc.commitTextTransform(beforeTexts_, doc.textSnapshot(selection_));
        doc.commitImageTransform(beforeImages_, doc.imageSnapshot(selection_));
        doc.commitShapeTransform(beforeShapes_, doc.shapeSnapshot(selection_));
    }
    forgetSnapshots();
    boxValid_ = false;
    boxRot_ = selection_.size() == 1 ? rotationOf(ctx.doc, selection_.front()) : 0.0;
    ctx.repaint();
}

void SelectTool::translate(const QPointF& deltaWorld, ToolContext& ctx)
{
    Document& doc = ctx.doc;
    for (int id : selection_) {
        Stroke* s = doc.strokeById(id);
        if (s) {
            for (Pt& p : s->pts) {
                p.x += float(deltaWorld.x());
                p.y += float(deltaWorld.y());
            }
            retessellate(*s);
            continue;
        }
        TextBox* t = doc.textById(id);
        if (t) {
            t->pos += deltaWorld;
            t->layoutDirty = true;
            continue;
        }
        ImageItem* im = doc.imageById(id);
        if (im) {
            im->pos += deltaWorld;
            continue;
        }
        ShapeItem* sh = doc.shapeById(id);
        if (sh)
            sh->rect.translate(deltaWorld);
    }
    boxValid_ = false;
    ctx.invalidate();
}

void SelectTool::rotate(const QPointF& w, bool snap, ToolContext& ctx)
{
    const QPointF c = dragAnchorWorld_;
    double d = std::atan2(w.y() - c.y(), w.x() - c.x()) - dragStartAngle_;
    if (snap) {
        const double total = dragStartRot_ + d;
        d = std::round(total / kSnapAngle) * kSnapAngle - dragStartRot_;
    }
    if (std::abs(d) < 1e-9) return;
    boxRot_ = dragStartRot_ + d;


    const auto movedAnchor = [c, d](const QPointF& anchor) {
        return rotateAbout(anchor, c, d);
    };

    Document& doc = ctx.doc;
    for (const Stroke& orig : beforeStrokes_) {
        Stroke* st = doc.strokeById(orig.id);
        if (!st) continue;
        for (size_t i = 0; i < st->pts.size() && i < orig.pts.size(); ++i) {
            const QPointF np = rotateAbout(QPointF(orig.pts[i].x, orig.pts[i].y), c, d);
            st->pts[i].x = float(np.x());
            st->pts[i].y = float(np.y());
        }
        retessellate(*st);
    }
    for (const TextBox& orig : beforeTexts_) {
        TextBox* t = doc.textById(orig.id);
        if (!t) continue;
        t->pos = movedAnchor(orig.anchor());
        t->rot = orig.rot + d;
        t->layoutDirty = true;
    }
    for (const ImageItem& orig : beforeImages_) {
        ImageItem* im = doc.imageById(orig.id);
        if (!im) continue;
        im->pos = movedAnchor(orig.anchor());
        im->rot = orig.rot + d;
    }
    for (const ShapeItem& orig : beforeShapes_) {
        ShapeItem* sh = doc.shapeById(orig.id);
        if (!sh) continue;
        sh->rect = QRectF(movedAnchor(orig.anchor()), orig.localRect().size());
        sh->rot = orig.rot + d;
    }
    boxValid_ = false;
    ctx.invalidate();
    ctx.repaint();
}

void SelectTool::scale(const QPointF& w, ToolContext& ctx)
{
    const QRectF b = dragStartWorld_;
    if (b.width() < 1e-9 || b.height() < 1e-9) return;
    const QPointF a = dragAnchorWorld_;
    const QPointF g = handlePos(b, handle_, boxRot_);


    const QPointF pivot = a;
    const QPointF wl = toLocal(w, pivot, boxRot_);
    const QPointF gl = toLocal(g, pivot, boxRot_);
    const QPointF al = toLocal(a, pivot, boxRot_);

    auto safe = [](double denom) { return std::abs(denom) < 1e-6 ? 1.0 : denom; };
    double sx = 1.0, sy = 1.0;
    switch (handle_) {
    case Handle::E: case Handle::W:
        sx = (wl.x() - al.x()) / safe(gl.x() - al.x()); break;
    case Handle::N: case Handle::S:
        sy = (wl.y() - al.y()) / safe(gl.y() - al.y()); break;
    default:
        sx = (wl.x() - al.x()) / safe(gl.x() - al.x());
        sy = (wl.y() - al.y()) / safe(gl.y() - al.y()); break;
    }
    constexpr double kMin = 0.02;
    if (sx < kMin) sx = kMin;
    if (sy < kMin) sy = kMin;

    double k;
    switch (handle_) {
    case Handle::E: case Handle::W: k = std::fabs(sx); break;
    case Handle::N: case Handle::S: k = std::fabs(sy); break;
    default: k = 0.5 * (std::fabs(sx) + std::fabs(sy)); break;
    }
    if (k < kMin) k = kMin;


    const auto applyScale = [pivot, sx, sy, rot = boxRot_](const QPointF& world) {
        const QPointF local = toLocal(world, pivot, rot);
        return toWorld(QPointF(pivot.x() + (local.x() - pivot.x()) * sx,
                               pivot.y() + (local.y() - pivot.y()) * sy), pivot, rot);
    };

    Document& doc = ctx.doc;
    for (const Stroke& orig : beforeStrokes_) {
        Stroke* s = doc.strokeById(orig.id);
        if (!s) continue;
        for (size_t i = 0; i < s->pts.size() && i < orig.pts.size(); ++i) {
            const QPointF np = applyScale(QPointF(orig.pts[i].x, orig.pts[i].y));
            s->pts[i].x = float(np.x());
            s->pts[i].y = float(np.y());
        }
        s->size = float(orig.size * k);
        retessellate(*s);
    }


    for (const TextBox& orig : beforeTexts_) {
        TextBox* t = doc.textById(orig.id);
        if (!t) continue;
        t->pos = applyScale(orig.anchor());
        t->width = orig.width * std::fabs(sx);
        t->fontPx = orig.fontPx * std::fabs(sy);
        t->rot = orig.rot;
        t->color = orig.color;
        t->layoutDirty = true;
    }
    for (const ImageItem& orig : beforeImages_) {
        ImageItem* im = doc.imageById(orig.id);
        if (!im) continue;
        im->pos = applyScale(orig.anchor());
        im->width = orig.width * std::fabs(sx);
        im->height = orig.height * std::fabs(sy);
        im->rot = orig.rot;
    }
    for (const ShapeItem& orig : beforeShapes_) {
        ShapeItem* sh = doc.shapeById(orig.id);
        if (!sh) continue;
        const QSizeF os = orig.localRect().size();
        sh->rect = QRectF(applyScale(orig.anchor()),
                          QSizeF(os.width() * sx, os.height() * sy));
        sh->penWidth = orig.penWidth * k;
        sh->rot = orig.rot;
    }
    boxValid_ = false;
    ctx.invalidate();
    ctx.repaint();
}

}
