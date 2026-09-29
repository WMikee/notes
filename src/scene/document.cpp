#include "scene/document.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace notes {

namespace {

float distSqToSegment(float px, float py, float ax, float ay, float bx, float by)
{
    const float ex = bx - ax;
    const float ey = by - ay;
    const float len2 = ex * ex + ey * ey;
    float t = len2 < 1e-12f ? 0.0f : ((px - ax) * ex + (py - ay) * ey) / len2;
    t = std::clamp(t, 0.0f, 1.0f);
    const float dx = px - (ax + t * ex);
    const float dy = py - (ay + t * ey);
    return dx * dx + dy * dy;
}

float cross2(float ox, float oy, float ax, float ay, float bx, float by)
{
    return (ax - ox) * (by - oy) - (ay - oy) * (bx - ox);
}

bool segmentsCross(float ax, float ay, float bx, float by,
                   float cx, float cy, float dx, float dy)
{
    const float o1 = cross2(ax, ay, bx, by, cx, cy);
    const float o2 = cross2(ax, ay, bx, by, dx, dy);
    const float o3 = cross2(cx, cy, dx, dy, ax, ay);
    const float o4 = cross2(cx, cy, dx, dy, bx, by);
    return ((o1 > 0.0f) != (o2 > 0.0f)) && ((o3 > 0.0f) != (o4 > 0.0f));
}

float distSqSegSeg(float ax, float ay, float bx, float by,
                   float cx, float cy, float dx, float dy)
{
    if (segmentsCross(ax, ay, bx, by, cx, cy, dx, dy))
        return 0.0f;
    float best = distSqToSegment(ax, ay, cx, cy, dx, dy);
    best = std::min(best, distSqToSegment(bx, by, cx, cy, dx, dy));
    best = std::min(best, distSqToSegment(cx, cy, ax, ay, bx, by));
    best = std::min(best, distSqToSegment(dx, dy, ax, ay, bx, by));
    return best;
}

int segmentSamples(const QPointF& a, const QPointF& b, float radius)
{
    const double d = std::hypot(b.x() - a.x(), b.y() - a.y());
    const double step = std::max(1e-3, double(radius) * 0.5);
    return std::clamp(int(std::ceil(d / step)), 1, 32);
}

QPointF lerpPoint(const QPointF& a, const QPointF& b, double t)
{
    return QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t);
}

} // namespace

Stroke* Document::strokeById(int id)
{
    for (Stroke& s : strokes_) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

const Stroke* Document::strokeById(int id) const
{
    for (const Stroke& s : strokes_) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

TextBox* Document::textById(int id)
{
    for (TextBox& t : texts_) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

const TextBox* Document::textById(int id) const
{
    for (const TextBox& t : texts_) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

ImageItem* Document::imageById(int id)
{
    for (ImageItem& im : images_) {
        if (im.id == id) return &im;
    }
    return nullptr;
}

const ImageItem* Document::imageById(int id) const
{
    for (const ImageItem& im : images_) {
        if (im.id == id) return &im;
    }
    return nullptr;
}

ShapeItem* Document::shapeById(int id)
{
    for (ShapeItem& sh : shapes_) {
        if (sh.id == id) return &sh;
    }
    return nullptr;
}

const ShapeItem* Document::shapeById(int id) const
{
    for (const ShapeItem& sh : shapes_) {
        if (sh.id == id) return &sh;
    }
    return nullptr;
}

void Document::add(const Stroke& s)
{
    undo_.push_back(Action{Action::Kind::Add, {s}, {}, {}, {}, {}, {}});
    strokes_.push_back(s);
    redo_.clear();
    dirty_ = true;
}

void Document::addText(const TextBox& t)
{
    undo_.push_back(Action{Action::Kind::Add, {}, {}, {t}, {}, {}, {}});
    texts_.push_back(t);
    redo_.clear();
    dirty_ = true;
}

void Document::addImage(const ImageItem& im)
{
    undo_.push_back(Action{Action::Kind::Add, {}, {}, {}, {}, {im}, {}, {}, {}});
    images_.push_back(im);
    redo_.clear();
    dirty_ = true;
}

void Document::addShape(const ShapeItem& sh)
{
    undo_.push_back(Action{Action::Kind::Add, {}, {}, {}, {}, {}, {}, {sh}, {}});
    shapes_.push_back(sh);
    redo_.clear();
    dirty_ = true;
}

void Document::replaceAll(std::vector<Stroke>&& strokes, std::vector<TextBox>&& texts,
                          std::vector<ImageItem>&& images, std::vector<ShapeItem>&& shapes,
                          int nextId)
{
    undo_.clear();
    redo_.clear();
    for (Stroke& s : strokes) {
        s.verts.clear();
        retessellate(s);
    }
    for (TextBox& t : texts)
        t.layoutDirty = true;
    strokes_ = std::move(strokes);
    texts_ = std::move(texts);
    images_ = std::move(images);
    shapes_ = std::move(shapes);
    nextId_ = nextId;
    dirty_ = false;
}

void Document::undo()
{
    if (undo_.empty()) return;
    Action a = undo_.back();
    undo_.pop_back();
    switch (a.kind) {
    case Action::Kind::Add:
        for (const auto& s : a.added) removeById(s.id);
        for (const auto& t : a.addedTexts) removeTextById(t.id);
        for (const auto& im : a.addedImages) removeImageById(im.id);
        for (const auto& sh : a.addedShapes) removeShapeById(sh.id);
        break;
    case Action::Kind::Remove:
        for (const auto& s : a.removed) strokes_.push_back(s);
        for (const auto& t : a.removedTexts) texts_.push_back(t);
        for (const auto& im : a.removedImages) images_.push_back(im);
        for (const auto& sh : a.removedShapes) shapes_.push_back(sh);
        for (const auto& s : a.added) removeById(s.id);
        break;
    case Action::Kind::Edit:
        apply(a.removed);
        applyText(a.removedTexts);
        applyImage(a.removedImages);
        applyShape(a.removedShapes);
        break;
    }
    redo_.push_back(std::move(a));
    dirty_ = true;
}

void Document::redo()
{
    if (redo_.empty()) return;
    Action a = redo_.back();
    redo_.pop_back();
    switch (a.kind) {
    case Action::Kind::Add:
        for (const auto& s : a.added) strokes_.push_back(s);
        for (const auto& t : a.addedTexts) texts_.push_back(t);
        for (const auto& im : a.addedImages) images_.push_back(im);
        for (const auto& sh : a.addedShapes) shapes_.push_back(sh);
        break;
    case Action::Kind::Remove:
        for (const auto& s : a.removed) removeById(s.id);
        for (const auto& t : a.removedTexts) removeTextById(t.id);
        for (const auto& im : a.removedImages) removeImageById(im.id);
        for (const auto& sh : a.removedShapes) removeShapeById(sh.id);
        for (const auto& s : a.added) strokes_.push_back(s);
        break;
    case Action::Kind::Edit:
        apply(a.added);
        applyText(a.addedTexts);
        applyImage(a.addedImages);
        applyShape(a.addedShapes);
        break;
    }
    undo_.push_back(std::move(a));
    dirty_ = true;
}

Document::EraseResult Document::eraseNear(const QPointF& from, const QPointF& to, float radius)
{
    EraseResult res;
    const float ax = float(from.x()), ay = float(from.y());
    const float bx = float(to.x()), by = float(to.y());
    const float rr = radius * radius;

    const auto strokeHit = [&](const Stroke& s) {
        if (s.pts.empty()) return false;
        if (s.pts.size() == 1)
            return distSqToSegment(s.pts[0].x, s.pts[0].y, ax, ay, bx, by) <= rr;
        for (size_t i = 0; i + 1 < s.pts.size(); ++i) {
            if (distSqSegSeg(s.pts[i].x, s.pts[i].y, s.pts[i + 1].x, s.pts[i + 1].y,
                             ax, ay, bx, by) <= rr)
                return true;
        }
        return false;
    };

    for (auto it = strokes_.begin(); it != strokes_.end();) {
        if (strokeHit(*it)) {
            res.strokes.push_back(std::move(*it));
            it = strokes_.erase(it);
        } else {
            ++it;
        }
    }

    const int n = segmentSamples(from, to, radius);
    std::vector<QPointF> samples;
    samples.reserve(size_t(n));
    for (int i = 0; i < n; ++i)
        samples.push_back(lerpPoint(from, to, double(i + 1) / n));

    const auto hitSamples = [&samples](const auto& test) {
        for (const QPointF& q : samples)
            if (test(q)) return true;
        return false;
    };

    for (auto it = texts_.begin(); it != texts_.end();) {
        if (hitSamples([&](const QPointF& q) { return textHitTest(*it, q, radius); })) {
            res.texts.push_back(std::move(*it));
            it = texts_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = images_.begin(); it != images_.end();) {
        if (hitSamples([&](const QPointF& q) { return imageHitTest(*it, q, radius); })) {
            res.images.push_back(std::move(*it));
            it = images_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = shapes_.begin(); it != shapes_.end();) {
        if (hitSamples([&](const QPointF& q) { return shapeHitTest(*it, q, radius); })) {
            res.shapes.push_back(std::move(*it));
            it = shapes_.erase(it);
        } else {
            ++it;
        }
    }
    return res;
}

Document::EraseResult Document::erasePartial(const QPointF& from, const QPointF& to, float radius)
{
    EraseResult res;
    const float ax = float(from.x()), ay = float(from.y());
    const float bx = float(to.x()), by = float(to.y());
    const float rr = radius * radius;
    for (auto it = strokes_.begin(); it != strokes_.end();) {
        Stroke& s = *it;

        std::vector<Pt> pts = s.pts;
        {
            std::vector<Pt> dense;
            dense.reserve(pts.size() * 2);
            for (size_t i = 0; i < pts.size(); ++i) {
                dense.push_back(pts[i]);
                if (i + 1 >= pts.size()) continue;
                const Pt& a = pts[i];
                const Pt& b = pts[i + 1];
                if (distSqSegSeg(a.x, a.y, b.x, b.y, ax, ay, bx, by) > rr) continue;
                const float len = std::hypot(b.x - a.x, b.y - a.y);
                const int k = std::clamp(
                    int(std::ceil(len / std::max(1e-3f, radius))), 1, 4096);
                for (int j = 1; j < k; ++j) {
                    const float t = float(j) / float(k);
                    dense.push_back(Pt{a.x + (b.x - a.x) * t,
                                       a.y + (b.y - a.y) * t,
                                       a.p + (b.p - a.p) * t});
                }
            }
            pts.swap(dense);
        }

        bool anyErased = false;
        std::vector<char> keep(pts.size(), 1);
        for (size_t i = 0; i < pts.size(); ++i) {
            if (distSqToSegment(pts[i].x, pts[i].y, ax, ay, bx, by) <= rr) {
                keep[i] = 0;
                anyErased = true;
            }
        }
        if (!anyErased) {
            ++it;
            continue;
        }

        std::vector<Stroke> segments;
        size_t segStart = SIZE_MAX;
        const auto flush = [&](size_t end) {
            if (segStart == SIZE_MAX) return;
            const size_t count = end - segStart;
            if (count >= 2) {
                Stroke seg;
                seg.id = nextId_++;
                seg.color = s.color;
                seg.size = s.size;
                seg.complete = s.complete;
                seg.stabilized = s.stabilized;
                seg.pts.assign(pts.begin() + ptrdiff_t(segStart),
                               pts.begin() + ptrdiff_t(end));
                segments.push_back(std::move(seg));
            }
            segStart = SIZE_MAX;
        };
        for (size_t i = 0; i < pts.size(); ++i) {
            if (keep[i]) {
                if (segStart == SIZE_MAX)
                    segStart = i;
            } else {
                flush(i);
            }
        }
        flush(pts.size());

        if (segments.empty()) {
            res.strokes.push_back(std::move(s));
            it = strokes_.erase(it);
            continue;
        }
        for (Stroke& seg : segments)
            retessellate(seg);
        res.strokes.push_back(std::move(s));
        it = strokes_.erase(it);
        it = strokes_.insert(it, segments.begin(), segments.end());
        it += ptrdiff_t(segments.size());
        for (Stroke& seg : segments)
            res.added.push_back(std::move(seg));
    }
    return res;
}

void Document::commitErase(EraseResult&& res)
{
    if (res.strokes.empty() && res.texts.empty() && res.images.empty() && res.added.empty()
        && res.shapes.empty()) return;
    Action a;
    a.kind = Action::Kind::Remove;
    a.removed = std::move(res.strokes);
    a.removedTexts = std::move(res.texts);
    a.removedImages = std::move(res.images);
    a.added = std::move(res.added);
    a.removedShapes = std::move(res.shapes);
    undo_.push_back(std::move(a));
    redo_.clear();
    dirty_ = true;
}

void Document::commitAdd(std::vector<Stroke>&& added, std::vector<TextBox>&& addedTexts,
                         std::vector<ImageItem>&& addedImages,
                         std::vector<ShapeItem>&& addedShapes)
{
    if (added.empty() && addedTexts.empty() && addedImages.empty() && addedShapes.empty()) return;
    for (const Stroke& s : added)
        strokes_.push_back(s);
    for (const TextBox& t : addedTexts)
        texts_.push_back(t);
    for (const ImageItem& im : addedImages)
        images_.push_back(im);
    for (const ShapeItem& sh : addedShapes)
        shapes_.push_back(sh);
    undo_.push_back(Action{Action::Kind::Add, std::move(added), {}, std::move(addedTexts),
                           {}, std::move(addedImages), {}, std::move(addedShapes), {}});
    redo_.clear();
    dirty_ = true;
}

void Document::commitRemove(const std::vector<int>& ids)
{
    std::vector<Stroke> removed;
    std::vector<TextBox> textRemoved;
    std::vector<ImageItem> imageRemoved;
    std::vector<ShapeItem> shapeRemoved;
    for (int id : ids) {
        Stroke* s = strokeById(id);
        if (s) {
            removed.push_back(*s);
            removeById(id);
        }
    }
    for (int id : ids) {
        TextBox* t = textById(id);
        if (t) {
            textRemoved.push_back(*t);
            removeTextById(id);
        }
    }
    for (int id : ids) {
        ImageItem* im = imageById(id);
        if (im) {
            imageRemoved.push_back(*im);
            removeImageById(id);
        }
    }
    for (int id : ids) {
        ShapeItem* sh = shapeById(id);
        if (sh) {
            shapeRemoved.push_back(*sh);
            removeShapeById(id);
        }
    }
    EraseResult res;
    res.strokes = std::move(removed);
    res.texts = std::move(textRemoved);
    res.images = std::move(imageRemoved);
    res.shapes = std::move(shapeRemoved);
    commitErase(std::move(res));
}

void Document::discardText(int id, bool dirtyBefore)
{
    if (!textById(id)) return;
    if (!undo_.empty() && undo_.back().kind == Action::Kind::Add
        && undo_.back().addedTexts.size() == 1
        && undo_.back().addedTexts.front().id == id) {
        undo_.pop_back();
        removeTextById(id);
        dirty_ = dirtyBefore;
        return;
    }
    commitRemove({id});
}

std::vector<Stroke> Document::snapshot(const std::vector<int>& ids) const
{
    std::vector<Stroke> out;
    out.reserve(ids.size());
    for (int id : ids) {
        const Stroke* s = strokeById(id);
        if (!s) continue;
        out.push_back(*s);
        out.back().verts.clear();
    }
    return out;
}

std::vector<TextBox> Document::textSnapshot(const std::vector<int>& ids) const
{
    std::vector<TextBox> out;
    out.reserve(ids.size());
    for (int id : ids) {
        const TextBox* t = textById(id);
        if (!t) continue;
        out.push_back(*t);
    }
    return out;
}

std::vector<ImageItem> Document::imageSnapshot(const std::vector<int>& ids) const
{
    std::vector<ImageItem> out;
    out.reserve(ids.size());
    for (int id : ids) {
        const ImageItem* im = imageById(id);
        if (!im) continue;
        out.push_back(*im);
    }
    return out;
}

std::vector<ShapeItem> Document::shapeSnapshot(const std::vector<int>& ids) const
{
    std::vector<ShapeItem> out;
    out.reserve(ids.size());
    for (int id : ids) {
        const ShapeItem* sh = shapeById(id);
        if (!sh) continue;
        out.push_back(*sh);
    }
    return out;
}

void Document::commitTransform(const std::vector<Stroke>& before, const std::vector<Stroke>& after)
{
    if (before.empty() || after.empty()) return;
    apply(after);
    undo_.push_back(Action{Action::Kind::Edit, after, before, {}, {}, {}, {}, {}, {}});
    redo_.clear();
    dirty_ = true;
}

void Document::commitTextTransform(const std::vector<TextBox>& before, const std::vector<TextBox>& after)
{
    if (before.empty() || after.empty()) return;
    applyText(after);
    undo_.push_back(Action{Action::Kind::Edit, {}, {}, after, before, {}, {}, {}, {}});
    redo_.clear();
    dirty_ = true;
}

void Document::commitImageTransform(const std::vector<ImageItem>& before, const std::vector<ImageItem>& after)
{
    if (before.empty() || after.empty()) return;
    applyImage(after);
    undo_.push_back(Action{Action::Kind::Edit, {}, {}, {}, {}, after, before, {}, {}});
    redo_.clear();
    dirty_ = true;
}

void Document::commitShapeTransform(const std::vector<ShapeItem>& before, const std::vector<ShapeItem>& after)
{
    if (before.empty() || after.empty()) return;
    applyShape(after);
    undo_.push_back(Action{Action::Kind::Edit, {}, {}, {}, {}, {}, {}, after, before});
    redo_.clear();
    dirty_ = true;
}

void Document::commitTextEdit(const std::vector<TextBox>& before, const std::vector<TextBox>& after)
{
    if (before.empty() || after.empty()) return;
    applyText(after);
    undo_.push_back(Action{Action::Kind::Edit, {}, {}, after, before, {}, {}, {}, {}});
    redo_.clear();
    dirty_ = true;
}

void Document::apply(const std::vector<Stroke>& list)
{
    for (const Stroke& s : list) {
        if (Stroke* d = strokeById(s.id)) {
            d->pts = s.pts;
            d->color = s.color;
            d->size = s.size;
            d->complete = s.complete;
            d->stabilized = s.stabilized;
            retessellate(*d);
        }
    }
}

void Document::applyText(const std::vector<TextBox>& list)
{
    for (const TextBox& t : list) {
        TextBox* d = textById(t.id);
        if (!d) continue;
        d->pos = t.pos;
        d->text = t.text;
        d->width = t.width;
        d->fontPx = t.fontPx;
        d->color = t.color;
        d->rot = t.rot;
        d->layoutDirty = true;
    }
}

void Document::applyImage(const std::vector<ImageItem>& list)
{
    for (const ImageItem& im : list) {
        ImageItem* d = imageById(im.id);
        if (!d) continue;
        d->source = im.source;
        d->pos = im.pos;
        d->width = im.width;
        d->height = im.height;
        d->rot = im.rot;
    }
}

void Document::applyShape(const std::vector<ShapeItem>& list)
{
    for (const ShapeItem& sh : list) {
        ShapeItem* d = shapeById(sh.id);
        if (!d) continue;
        d->kind = sh.kind;
        d->rect = sh.rect;
        d->color = sh.color;
        d->penWidth = sh.penWidth;
        d->rot = sh.rot;
    }
}

void Document::removeById(int id)
{
    for (auto it = strokes_.begin(); it != strokes_.end(); ++it) {
        if (it->id == id) {
            strokes_.erase(it);
            return;
        }
    }
}

void Document::removeTextById(int id)
{
    for (auto it = texts_.begin(); it != texts_.end(); ++it) {
        if (it->id == id) {
            texts_.erase(it);
            return;
        }
    }
}

void Document::removeImageById(int id)
{
    for (auto it = images_.begin(); it != images_.end(); ++it) {
        if (it->id == id) {
            images_.erase(it);
            return;
        }
    }
}

void Document::removeShapeById(int id)
{
    for (auto it = shapes_.begin(); it != shapes_.end(); ++it) {
        if (it->id == id) {
            shapes_.erase(it);
            return;
        }
    }
}

}
