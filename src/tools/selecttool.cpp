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
    boxValid_ = false;
}

void SelectTool::forgetSnapshots()
{
    beforeStrokes_.clear();
    beforeTexts_.clear();
    beforeImages_.clear();
    beforeShapes_.clear();
    dragStrokes_.clear();
    dragTexts_.clear();
    dragImages_.clear();
    dragShapes_.clear();
}

void SelectTool::collectSnapshots(Document& doc)
{
    forgetSnapshots();
    for (int id : selection_) {
        if (Stroke* s = doc.strokeById(id)) {
            beforeStrokes_.push_back(*s);
            beforeStrokes_.back().verts.clear();
            dragStrokes_.push_back(s);
            continue;
        }
        if (TextBox* t = doc.textById(id)) {
            beforeTexts_.push_back(*t);
            dragTexts_.push_back(t);
            continue;
        }
        if (ImageItem* im = doc.imageById(id)) {
            beforeImages_.push_back(*im);
            dragImages_.push_back(im);
            continue;
        }
        if (ShapeItem* sh = doc.shapeById(id)) {
            beforeShapes_.push_back(*sh);
            dragShapes_.push_back(sh);
        }
    }
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
    if (selection_.empty()) return boxWorld_;

    scratchPts_.clear();

    bool pFirst = true, fFirst = true;
    double pMinX = 0, pMinY = 0, pMaxX = 0, pMaxY = 0;
    double fMinX = 0, fMinY = 0, fMaxX = 0, fMaxY = 0;
    const auto growP = [&](double x, double y) {
        if (pFirst) {
            pMinX = pMaxX = x;
            pMinY = pMaxY = y;
            pFirst = false;
        } else {
            pMinX = std::min(pMinX, x); pMaxX = std::max(pMaxX, x);
            pMinY = std::min(pMinY, y); pMaxY = std::max(pMaxY, y);
        }
    };
    const auto growF = [&](double x, double y) {
        if (fFirst) {
            fMinX = fMaxX = x;
            fMinY = fMaxY = y;
            fFirst = false;
        } else {
            fMinX = std::min(fMinX, x); fMaxX = std::max(fMaxX, x);
            fMinY = std::min(fMinY, y); fMaxY = std::max(fMaxY, y);
        }
    };
    const auto growRot = [&](const QRectF& r, const QPointF& anchor, double rot) {
        const QPointF c[4] = { r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft() };
        for (const QPointF& p : c) {
            const QPointF w = rotateAbout(p, anchor, rot);
            scratchPts_.push_back(w);
            growP(w.x(), w.y());
            growF(p.x(), p.y());
        }
    };
    for (int id : selection_) {
        if (const Stroke* s = doc.strokeById(id)) {
            for (const Pt& p : s->pts) {
                scratchPts_.emplace_back(p.x, p.y);
                growP(p.x, p.y);
                growF(p.x, p.y);
            }
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
    if (pFirst || scratchPts_.empty()) return boxWorld_;

    const QPointF pivot((fMinX + fMaxX) * 0.5, (fMinY + fMaxY) * 0.5);
    const QPointF frameCenter((pMinX + pMaxX) * 0.5, (pMinY + pMaxY) * 0.5);

    bool lFirst = true;
    double lMinX = 0, lMinY = 0, lMaxX = 0, lMaxY = 0;
    for (const QPointF& wp : scratchPts_) {
        const QPointF lp = toLocal(wp, pivot, boxRot_);
        if (lFirst) {
            lMinX = lMaxX = lp.x();
            lMinY = lMaxY = lp.y();
            lFirst = false;
        } else {
            lMinX = std::min(lMinX, lp.x()); lMaxX = std::max(lMaxX, lp.x());
            lMinY = std::min(lMinY, lp.y()); lMaxY = std::max(lMaxY, lp.y());
        }
    }

    QRectF frame(QPointF(lMinX, lMinY), QPointF(lMaxX, lMaxY));
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
                    appliedRot_ = 0.0;
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
        if (p.shift) {
            const auto found = std::find(selection_.begin(), selection_.end(), hit);
            if (found != selection_.end())
                selection_.erase(found);
            else
                selection_.push_back(hit);
            boxValid_ = false;
            boxRot_ = selection_.size() == 1 ? rotationOf(ctx.doc, selection_.front()) : 0.0;
            if (selection_.empty()) emit gizmoCleared();
            else emit gizmoAppeared();
            ctx.repaint();
            return;
        }
        if (std::find(selection_.begin(), selection_.end(), hit) == selection_.end())
            setSelection({ hit }, ctx.doc);
        beginDrag(p.pos, ctx);
        return;
    }

    drag_ = true;
    handle_ = Handle::None;
    marqueeActive_ = true;
    marqueeAdd_ = p.shift;
    marqueeAnchorWorld_ = w;
    marqueeRect_ = QRectF(w, w);
    if (selection_.empty()) emit gizmoAppeared();
}

void SelectTool::beginDrag(const QPointF& screen, ToolContext& ctx)
{
    handle_ = Handle::Move;
    dragLastScreen_ = screen;
    if (!selection_.empty()) gizmoBox(ctx);
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
    scale(ctx.cam.toWorld(p.pos), p.shift, ctx);
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
    const bool add = marqueeAdd_;
    marqueeAdd_ = false;
    const QRectF r = marqueeRect_;
    marqueeRect_ = QRectF();
    if (r.isNull()) {
        if (!add) clear(ctx);
        return;
    }
    const double pw = r.width() * ctx.cam.zoom, ph = r.height() * ctx.cam.zoom;
    if (pw < 5 && ph < 5) {
        if (!add) clear(ctx);
        return;
    }
    std::vector<int> ids;
    collect(ctx.doc, r, ids);
    if (add) {
        ids.insert(ids.end(), selection_.begin(), selection_.end());
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        if (ids.empty()) return;
    } else if (ids.empty()) {
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
    const float dx = float(deltaWorld.x());
    const float dy = float(deltaWorld.y());
    for (Stroke* s : dragStrokes_) {
        for (Pt& p : s->pts) {
            p.x += dx;
            p.y += dy;
        }
        if (s->verts.empty()) {
            retessellate(*s);
        } else {
            for (size_t i = 0; i + 1 < s->verts.size(); i += 6) {
                s->verts[i] += dx;
                s->verts[i + 1] += dy;
            }
        }
    }
    for (TextBox* t : dragTexts_) {
        t->pos += deltaWorld;
        t->layoutDirty = true;
    }
    for (ImageItem* im : dragImages_)
        im->pos += deltaWorld;
    for (ShapeItem* sh : dragShapes_)
        sh->rect.translate(deltaWorld);
    if (boxValid_) boxWorld_.translate(deltaWorld);
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
    const double dd = d - appliedRot_;
    if (std::abs(dd) < 1e-9) return;
    appliedRot_ = d;
    boxRot_ = dragStartRot_ + d;

    const double s = std::sin(dd), co = std::cos(dd);
    const auto rotPt = [c, s, co](const QPointF& p) {
        const double px = p.x() - c.x(), py = p.y() - c.y();
        return QPointF(c.x() + px * co - py * s, c.y() + px * s + py * co);
    };

    for (Stroke* st : dragStrokes_) {
        for (Pt& p : st->pts) {
            const QPointF np = rotPt(QPointF(p.x, p.y));
            p.x = float(np.x());
            p.y = float(np.y());
        }
        if (st->verts.empty()) {
            retessellate(*st);
        } else {
            for (size_t i = 0; i + 1 < st->verts.size(); i += 6) {
                const QPointF np = rotPt(QPointF(st->verts[i], st->verts[i + 1]));
                st->verts[i] = float(np.x());
                st->verts[i + 1] = float(np.y());
            }
        }
    }
    for (TextBox* t : dragTexts_) {
        t->pos = rotPt(t->pos);
        t->rot += dd;
        t->layoutDirty = true;
    }
    for (ImageItem* im : dragImages_) {
        im->pos = rotPt(im->pos);
        im->rot += dd;
    }
    for (ShapeItem* sh : dragShapes_) {
        sh->rect = QRectF(rotPt(sh->anchor()), sh->localRect().size());
        sh->rot += dd;
    }
    boxValid_ = false;
    ctx.invalidate();
    ctx.repaint();
}

void SelectTool::scale(const QPointF& w, bool shift, ToolContext& ctx)
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

    if (shift) {
        switch (handle_) {
        case Handle::E: case Handle::W: sy = sx; break;
        case Handle::N: case Handle::S: sx = sy; break;
        default: sx = sy = std::max(sx, sy); break;
        }
    }

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

    for (size_t i = 0; i < dragStrokes_.size(); ++i) {
        Stroke* s = dragStrokes_[i];
        const Stroke& orig = beforeStrokes_[i];
        for (size_t j = 0; j < s->pts.size() && j < orig.pts.size(); ++j) {
            const QPointF np = applyScale(QPointF(orig.pts[j].x, orig.pts[j].y));
            s->pts[j].x = float(np.x());
            s->pts[j].y = float(np.y());
        }
        s->size = float(orig.size * k);
        retessellate(*s);
    }

    for (size_t i = 0; i < dragTexts_.size(); ++i) {
        TextBox* t = dragTexts_[i];
        const TextBox& orig = beforeTexts_[i];
        t->pos = applyScale(orig.anchor());
        t->width = orig.width * std::fabs(sx);
        t->fontPx = orig.fontPx * std::fabs(sy);
        t->rot = orig.rot;
        t->color = orig.color;
        t->layoutDirty = true;
    }
    for (size_t i = 0; i < dragImages_.size(); ++i) {
        ImageItem* im = dragImages_[i];
        const ImageItem& orig = beforeImages_[i];
        im->pos = applyScale(orig.anchor());
        im->width = orig.width * std::fabs(sx);
        im->height = orig.height * std::fabs(sy);
        im->rot = orig.rot;
    }
    for (size_t i = 0; i < dragShapes_.size(); ++i) {
        ShapeItem* sh = dragShapes_[i];
        const ShapeItem& orig = beforeShapes_[i];
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
