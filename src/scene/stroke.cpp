#include "scene/stroke.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace notes {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kFixedPi = kPi + 0.0001f;
constexpr float kEndNoiseThreshold = 3.0f;
constexpr float kMinStreamlineT = 0.15f;
constexpr float kStreamlineTRange = 0.85f;
constexpr float kMinRadius = 0.01f;
constexpr float kDotThreshold = 3.0f;
constexpr float kSmoothPerSize = 0.12f;
constexpr float kSmoothMin = 0.8f;
constexpr float kSmoothMax = 3.0f;
constexpr float kSmoothTail = 2.5f;
constexpr int kCircleSegments = 64;
constexpr int kCornerCapSegments = 32;
constexpr float kDefaultFirstPressure = 0.25f;
constexpr float kDefaultPressure = 0.5f;

constexpr float kSize = kDefaultStrokeSize;

struct Vec2 { float x, y; };

struct StrokeOptions {
    float size = kSize;
    float thinning = 0.5f;
    float smoothing = 0.3f;
    float streamline = 0.75f;
    bool last = false;
    float taperStart = 0.0f;
    float taperEnd = 0.0f;
    bool capStart = true;
    bool capEnd = true;
};

StrokeOptions optionsFor(const QColor& color, bool complete, float size)
{
    StrokeOptions opt;
    opt.last = complete;
    opt.size = size;
    if (color.alpha() < 255) {
        opt.size = size * 2.4f;
        opt.thinning = 0.0f;
        opt.smoothing = 0.5f;
        opt.streamline = 0.8f;
        opt.capStart = false;
        opt.capEnd = false;
    }
    return opt;
}

struct StrokePoint {
    Vec2 point;
    float pressure;
    Vec2 vector;
    float distance;
    float runningLength;
};

struct GP { Vec2 point; float pressure; };

Vec2 vAdd(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 vSub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 vMul(Vec2 a, float n) { return {a.x * n, a.y * n}; }
Vec2 vPer(Vec2 a) { return {a.y, -a.x}; }
Vec2 vNeg(Vec2 a) { return {-a.x, -a.y}; }
float vDot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
float vLen(Vec2 a) { return std::hypot(a.x, a.y); }
float vDist2(Vec2 a, Vec2 b) { const float dx = a.x - b.x, dy = a.y - b.y; return dx * dx + dy * dy; }
float vDist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
Vec2 vUni(Vec2 a) { const float l = vLen(a); return l < 1e-8f ? Vec2{0, 0} : Vec2{a.x / l, a.y / l}; }
bool vEqual(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
Vec2 vLrp(Vec2 a, Vec2 b, float t) { return vAdd(a, vMul(vSub(b, a), t)); }
Vec2 vPrj(Vec2 a, Vec2 b, float c) { return vAdd(a, vMul(b, c)); }
Vec2 vRotAround(Vec2 a, Vec2 c, float r) {
    const float s = std::sin(r);
    const float co = std::cos(r);
    const float px = a.x - c.x;
    const float py = a.y - c.y;
    return {px * co - py * s + c.x, px * s + py * co + c.y};
}

float strokeRadius(float size, float thinning, float pressure) {
    return size * (0.5f - thinning * (0.5f - pressure));
}

Vec2 catmullRom(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float a0 = -0.5f * t3 + t2 - 0.5f * t;
    const float a1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
    const float a2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    const float a3 = 0.5f * t3 - 0.5f * t2;
    return {a0 * p0.x + a1 * p1.x + a2 * p2.x + a3 * p3.x,
            a0 * p0.y + a1 * p1.y + a2 * p2.y + a3 * p3.y};
}

float computeTaperDistance(float taper, float size, float totalLength) {
    if (taper <= 0.0f) return 0.0f;
    if (taper == 1.0f) return std::max(size, totalLength);
    return taper;
}

float smoothSigma(float size)
{
    return std::clamp(kSmoothPerSize * size, kSmoothMin, kSmoothMax);
}

void smoothCenterline(std::vector<GP>& pts, float sigma)
{
    if (sigma <= 0.0f || pts.size() < 3) return;
    const int r = std::max(1, int(std::ceil(sigma * kSmoothTail)));
    std::vector<float> k(size_t(2 * r + 1));
    float sum = 0.0f;
    for (int i = -r; i <= r; ++i) {
        k[size_t(i + r)] = std::exp(-0.5f * float(i * i) / (sigma * sigma));
        sum += k[size_t(i + r)];
    }
    for (float& w : k) w /= sum;

    std::vector<GP> out(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        double x = 0.0, y = 0.0, pr = 0.0, w = 0.0;
        for (int j = -r; j <= r; ++j) {
            const long idx = long(i) + j;
            if (idx < 0 || idx >= long(pts.size())) continue;
            const double kw = k[size_t(j + r)];
            x += kw * pts[size_t(idx)].point.x;
            y += kw * pts[size_t(idx)].point.y;
            pr += kw * pts[size_t(idx)].pressure;
            w += kw;
        }
        out[i] = {{float(x / w), float(y / w)}, float(pr / w)};
    }
    pts.swap(out);
}

std::vector<StrokePoint> getStrokePoints(const std::vector<Pt>& rawin, const StrokeOptions& opt) {
    std::vector<StrokePoint> out;
    if (rawin.empty()) return out;

    const float t = kMinStreamlineT + (1.0f - opt.streamline) * kStreamlineTRange;
    const bool isComplete = opt.last;

    std::vector<GP> pts;
    pts.reserve(rawin.size());
    for (const Pt& p : rawin) pts.push_back({{p.x, p.y}, p.p});
    smoothCenterline(pts, smoothSigma(opt.size));

    if (pts.size() == 2) {
        const GP last = pts[1];
        pts.pop_back();
        for (int i = 1; i < 5; ++i)
            pts.push_back({vLrp(pts[0].point, last.point, i / 4.0f), last.pressure});
    }
    if (pts.size() == 1) {
        pts.push_back({vAdd(pts[0].point, {1, 1}), pts[0].pressure});
    }

    out.push_back({pts[0].point,
                   pts[0].pressure >= 0.0f ? pts[0].pressure : kDefaultFirstPressure,
                   {1, 1}, 0.0f, 0.0f});

    bool hasMin = false;
    float runningLength = 0.0f;
    StrokePoint prev = out[0];
    const int max = int(pts.size()) - 1;

    for (int i = 1; i < int(pts.size()); ++i) {
        const Vec2 point = (isComplete && i == max)
            ? pts[i].point
            : vLrp(prev.point, pts[i].point, t);
        if (vEqual(prev.point, point)) continue;

        const float distance = vDist(point, prev.point);
        runningLength += distance;

        if (i < max && !hasMin) {
            if (runningLength < opt.size * 0.5f) continue;
            hasMin = true;
        }

        prev = {point,
                pts[i].pressure >= 0.0f ? pts[i].pressure : kDefaultPressure,
                vUni(vSub(prev.point, point)),
                distance,
                runningLength};
        out.push_back(prev);
    }

    out[0].vector = out.size() > 1 ? out[1].vector : Vec2{0, 0};
    return out;
}

std::vector<Vec2> drawDot(Vec2 center, float radius, int segments) {
    const Vec2 offsetPoint = vAdd(center, {1, 1});
    const Vec2 start = vPrj(center, vUni(vPer(vSub(center, offsetPoint))), -radius);
    std::vector<Vec2> dots;
    dots.reserve(segments + 1);
    for (int k = 0; k <= segments; ++k) {
        const float tt = float(k) / float(segments);
        dots.push_back(vRotAround(start, center, kFixedPi * 2.0f * tt));
    }
    return dots;
}

struct Outline {
    std::vector<Vec2> left;
    std::vector<Vec2> right;
    std::vector<Vec2> dot;
    Vec2 dotCenter;
    float dotRadius = 0.0f;
    std::vector<Vec2> startCap;
    Vec2 startCenter;
    std::vector<Vec2> endCap;
    Vec2 endCenter;
};

Outline getStrokeOutlinePoints(const std::vector<StrokePoint>& points, const StrokeOptions& opt) {
    Outline out;
    if (points.empty() || opt.size <= 0.0f) return out;

    const float totalLength = points.back().runningLength;
    const float taperStart = computeTaperDistance(opt.taperStart, opt.size, totalLength);
    const float taperEnd = computeTaperDistance(opt.taperEnd, opt.size, totalLength);
    const float minDistance = std::pow(opt.size * opt.smoothing, 2.0f);
    const float endNoise = std::min(opt.size * 0.2f, kEndNoiseThreshold);

    std::vector<Vec2> leftPts, rightPts;
    leftPts.reserve(points.size());
    rightPts.reserve(points.size());

    float radius = strokeRadius(opt.size, opt.thinning, points.back().pressure);
    float firstRadius = 0.0f;
    bool hasFirstRadius = false;

    Vec2 prevLeftPoint = points[0].point;
    Vec2 prevRightPoint = prevLeftPoint;
    Vec2 tempLeftPoint = prevLeftPoint;
    Vec2 tempRightPoint = prevRightPoint;
    Vec2 prevVector = points[0].vector;
    bool isPrevPointSharpCorner = false;

    for (int i = 0; i < int(points.size()); ++i) {
        float pressure = points[i].pressure;
        const Vec2 point = points[i].point;
        const Vec2 vector = points[i].vector;
        const float runningLength = points[i].runningLength;
        const bool isLast = i == int(points.size()) - 1;

        if (!isLast && totalLength - runningLength < endNoise) continue;

        if (opt.thinning) {
            radius = strokeRadius(opt.size, opt.thinning, pressure);
        } else {
            radius = opt.size / 2.0f;
        }

        if (!hasFirstRadius) {
            firstRadius = radius;
            hasFirstRadius = true;
        }

        const float taperStartStrength = runningLength < taperStart
            ? (runningLength / taperStart) * (2.0f - runningLength / taperStart)
            : 1.0f;
        const float taperEndStrength = totalLength - runningLength < taperEnd
            ? std::pow((totalLength - runningLength) / taperEnd - 1.0f, 3.0f) + 1.0f
            : 1.0f;

        radius = std::max(kMinRadius, radius * std::min(taperStartStrength, taperEndStrength));

        const Vec2 nextVector = !isLast ? points[i + 1].vector : points[i].vector;
        const float nextDpr = !isLast ? vDot(vector, nextVector) : 1.0f;
        const float prevDpr = vDot(vector, prevVector);

        const bool isPointSharpCorner = prevDpr < 0.0f && !isPrevPointSharpCorner;
        const bool isNextPointSharpCorner = nextDpr < 0.0f;

        if (isPointSharpCorner || isNextPointSharpCorner) {
            const Vec2 offset = vMul(vPer(prevVector), radius);
            for (int k = 0; k <= kCornerCapSegments; ++k) {
                const float tt = float(k) / float(kCornerCapSegments);
                tempLeftPoint = vRotAround(vSub(point, offset), point, kFixedPi * tt);
                leftPts.push_back(tempLeftPoint);
                tempRightPoint = vRotAround(vAdd(point, offset), point, -kFixedPi * tt);
                rightPts.push_back(tempRightPoint);
            }
            prevLeftPoint = tempLeftPoint;
            prevRightPoint = tempRightPoint;
            if (isNextPointSharpCorner) isPrevPointSharpCorner = true;
            continue;
        }

        isPrevPointSharpCorner = false;

        if (isLast) {
            const Vec2 offset = vMul(vPer(vector), radius);
            leftPts.push_back(vSub(point, offset));
            rightPts.push_back(vAdd(point, offset));
            continue;
        }

        const Vec2 offsetDir = vLrp(nextVector, vector, nextDpr);
        const Vec2 offset = vMul(vPer(offsetDir), radius);

        if (i >= 1 && radius > kMinRadius) {
            const Vec2 fwd = vUni(vSub(points[i + 1].point, point));
            const float gp0 = vDist(vAdd(point, vMul(vPer(vector), radius)),
                                    vAdd(point, vMul(vPer(fwd), radius)));
            const float gp1 = vDist(vSub(point, vMul(vPer(vector), radius)),
                                    vSub(point, vMul(vPer(fwd), radius)));
            const float gmin = std::min(gp0, gp1);
            const bool interiorRight = gp0 < gp1;
            if (gmin < radius * 0.85f) {
                const Vec2 pinRaw = interiorRight
                    ? vAdd(point, vMul(vPer(vector), radius))
                    : vSub(point, vMul(vPer(vector), radius));
                const Vec2 pouRaw = interiorRight
                    ? vAdd(point, vMul(vPer(fwd), radius))
                    : vSub(point, vMul(vPer(fwd), radius));
                const Vec2 pin = vSub(pinRaw, point);
                const Vec2 pou = vSub(pouRaw, point);
                const float a0 = std::atan2(pin.y, pin.x);
                const float a1 = std::atan2(pou.y, pou.x);
                const float da = std::atan2(std::sin(a1 - a0), std::cos(a1 - a0));
                const int nseg = std::clamp(int(std::abs(da) * 8.0f / kPi) + 2, 2, 12);
                const Vec2 miterL = vSub(point, offset);
                const Vec2 miterR = vAdd(point, offset);
                for (int k = 0; k <= nseg; ++k) {
                    const float t = float(k) / float(nseg);
                    const Vec2 arc = vRotAround(pinRaw, point, da * t);
                    if (interiorRight) {
                        leftPts.push_back(miterL);
                        rightPts.push_back(arc);
                    } else {
                        leftPts.push_back(arc);
                        rightPts.push_back(miterR);
                    }
                }
                prevLeftPoint = leftPts.back();
                prevRightPoint = rightPts.back();
                prevVector = vector;
                continue;
            }
        }

        tempLeftPoint = vSub(point, offset);
        tempRightPoint = vAdd(point, offset);
        const bool addPoint = i <= 1
            || vDist2(prevLeftPoint, tempLeftPoint) > minDistance
            || vDist2(prevRightPoint, tempRightPoint) > minDistance;
        if (addPoint) {
            leftPts.push_back(tempLeftPoint);
            prevLeftPoint = tempLeftPoint;
            rightPts.push_back(tempRightPoint);
            prevRightPoint = tempRightPoint;
        }

        prevVector = vector;
    }

    const Vec2 firstPoint = {points[0].point.x, points[0].point.y};
    const Vec2 lastPoint = points.size() > 1
        ? Vec2{points.back().point.x, points.back().point.y}
        : vAdd(points[0].point, {1, 1});

    if (points.size() == 1) {
        if (!(taperStart || taperEnd) || opt.last) {
            const float dotRadius = hasFirstRadius ? firstRadius : radius;
            out.dot = drawDot(firstPoint, dotRadius, kCircleSegments);
            out.dotCenter = firstPoint;
            out.dotRadius = dotRadius;
        }
    } else {
        if (opt.capStart && !(taperStart > 0.0f)) {
            const float startRadius = hasFirstRadius ? firstRadius : radius;
            out.startCap = drawDot(firstPoint, startRadius, kCircleSegments);
            out.startCenter = firstPoint;
        }

        if (opt.capEnd && !(taperEnd > 0.0f)) {
            out.endCap = drawDot(lastPoint, radius, kCircleSegments);
            out.endCenter = lastPoint;
        }
    }

    out.left = std::move(leftPts);
    out.right = std::move(rightPts);
    return out;
}

void pushVertex(std::vector<float>& out, Vec2 p, const QColor& c) {
    out.insert(out.end(), {p.x, p.y, c.redF(), c.greenF(), c.blueF(), c.alphaF()});
}

void pushTri(std::vector<float>& out, Vec2 a, Vec2 b, Vec2 c, const QColor& color) {
    pushVertex(out, a, color);
    pushVertex(out, b, color);
    pushVertex(out, c, color);
}

void pushQuad(std::vector<float>& out, Vec2 a, Vec2 b, Vec2 c, Vec2 d, const QColor& color) {
    pushTri(out, a, b, c, color);
    pushTri(out, a, c, d, color);
}

void densifySides(const std::vector<Vec2>& left, const std::vector<Vec2>& right,
                  float target, std::vector<Vec2>& outL, std::vector<Vec2>& outR) {
    outL.clear();
    outR.clear();
    const int n = int(left.size());
    if (int(right.size()) != n || n < 2) {
        outL = left;
        outR = right;
        return;
    }
    outL.reserve(size_t(n) * 5);
    outR.reserve(size_t(n) * 5);
    for (int i = 0; i + 1 < n; ++i) {
        const Vec2 l0 = i > 0 ? left[i - 1] : left[i];
        const Vec2 l1 = left[i];
        const Vec2 l2 = left[i + 1];
        const Vec2 l3 = i + 2 < n ? left[i + 2] : left[i + 1];
        const Vec2 r0 = i > 0 ? right[i - 1] : right[i];
        const Vec2 r1 = right[i];
        const Vec2 r2 = right[i + 1];
        const Vec2 r3 = i + 2 < n ? right[i + 2] : right[i + 1];
        const float segLen = vDist(l1, l2);
        const int per = std::clamp(int(std::ceil(segLen / target)), 1, 8);
        const int kStart = i == 0 ? 0 : 1;
        for (int k = kStart; k <= per; ++k) {
            const float t = float(k) / float(per);
            outL.push_back(catmullRom(l0, l1, l2, l3, t));
            outR.push_back(catmullRom(r0, r1, r2, r3, t));
        }
    }
}

void pushFan(std::vector<float>& out, Vec2 center, const std::vector<Vec2>& pts, const QColor& color) {
    if (pts.size() < 2) return;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        pushTri(out, center, pts[i], pts[i + 1], color);
}

bool isDotStroke(const std::vector<Pt>& pts, const StrokeOptions& opt) {
    if (pts.empty()) return false;
    if (pts.size() == 1) return true;
    float maxDist2 = 0.0f;
    for (size_t i = 1; i < pts.size(); ++i) {
        const float dx = pts[i].x - pts[0].x, dy = pts[i].y - pts[0].y;
        maxDist2 = std::max(maxDist2, dx * dx + dy * dy);
    }
    const float dotThresholdWorld = std::min(opt.size * 0.5f, kDotThreshold);
    return maxDist2 < dotThresholdWorld * dotThresholdWorld;
}

void tessellateImpl(const std::vector<Pt>& pts, const QColor& color,
                    const StrokeOptions& opt, std::vector<float>& out) {
    if (pts.empty()) return;

    if (isDotStroke(pts, opt)) {
        const float pressure = pts[0].p >= 0.0f ? pts[0].p : kDefaultPressure;
        const float radius = opt.thinning
            ? std::max(kMinRadius, strokeRadius(opt.size, opt.thinning, pressure))
            : opt.size / 2.0f;
        const Vec2 center{pts[0].x, pts[0].y};
        pushFan(out, center,
                drawDot(center, radius, kCircleSegments),
                color);
        return;
    }

    const std::vector<StrokePoint> sp = getStrokePoints(pts, opt);
    const Outline o = getStrokeOutlinePoints(sp, opt);

    if (!o.dot.empty()) {
        pushFan(out, o.dotCenter, o.dot, color);
        return;
    }

    const float densifyTarget = std::max(0.3f, opt.size * 0.1f);
    std::vector<Vec2> dl, dr;
    densifySides(o.left, o.right, densifyTarget, dl, dr);
    const size_t n = std::min(dl.size(), dr.size());
    for (size_t i = 0; i + 1 < n; ++i) {
        pushQuad(out, dl[i], dl[i + 1], dr[i + 1], dr[i], color);
    }

    if (!o.startCap.empty() && n > 0) {
        pushFan(out, o.startCenter, o.startCap, color);
    }

    if (!o.endCap.empty() && n > 0) {
        pushFan(out, o.endCenter, o.endCap, color);
    }
}

} // namespace

void retessellate(Stroke& st)
{
    StrokeOptions opt = optionsFor(st.color, st.complete, st.size);
    st.verts.clear();
    tessellateImpl(st.pts, st.color, opt, st.verts);
}

namespace {

std::vector<QPointF> toPoints(const std::vector<Vec2>& v)
{
    std::vector<QPointF> out;
    out.reserve(v.size());
    for (const Vec2& p : v) out.push_back(QPointF(p.x, p.y));
    return out;
}

}

StrokeOutline strokeOutline(const Stroke& st)
{
    StrokeOutline out;
    if (st.pts.empty()) return out;

    const StrokeOptions opt = optionsFor(st.color, st.complete, st.size);

    if (isDotStroke(st.pts, opt)) {
        const float pressure = st.pts[0].p >= 0.0f ? st.pts[0].p : kDefaultPressure;
        const float radius = opt.thinning
            ? std::max(kMinRadius, strokeRadius(opt.size, opt.thinning, pressure))
            : opt.size / 2.0f;
        out.body = toPoints(drawDot({st.pts[0].x, st.pts[0].y}, radius, kCircleSegments));
        return out;
    }

    const std::vector<StrokePoint> sp = getStrokePoints(st.pts, opt);
    const Outline o = getStrokeOutlinePoints(sp, opt);

    if (!o.dot.empty()) {
        out.body = toPoints(o.dot);
        return out;
    }

    out.body.reserve(o.left.size() + o.right.size());
    for (const Vec2& p : o.left) out.body.push_back(QPointF(p.x, p.y));
    for (size_t i = o.right.size(); i-- > 0;)
        out.body.push_back(QPointF(o.right[i].x, o.right[i].y));
    out.startCap = toPoints(o.startCap);
    out.endCap = toPoints(o.endCap);
    return out;
}

void tessellate(const std::vector<Pt>& pts, const QColor& color, std::vector<float>& out)
{
    StrokeOptions opt = optionsFor(color, false, kSize);
    tessellateImpl(pts, color, opt, out);
}

float distanceToSquared(const Stroke& s, float x, float y)
{
    if (s.pts.empty()) return std::numeric_limits<float>::max();
    float best = std::numeric_limits<float>::max();
    const auto dist2 = [x, y](float px, float py) {
        const float dx = px - x, dy = py - y;
        return dx * dx + dy * dy;
    };
    for (size_t i = 0; i < s.pts.size(); ++i) {
        const Pt& c = s.pts[i];
        best = std::min(best, dist2(c.x, c.y));
        if (i + 1 < s.pts.size()) {
            const Pt& n = s.pts[i + 1];
            const float ex = n.x - c.x, ey = n.y - c.y;
            const float len2 = ex * ex + ey * ey;
            float t = len2 < 1e-12f ? 0.0f : ((x - c.x) * ex + (y - c.y) * ey) / len2;
            t = std::clamp(t, 0.0f, 1.0f);
            best = std::min(best, dist2(c.x + t * ex, c.y + t * ey));
        }
    }
    return best;
}

float distanceTo(const Stroke& s, float x, float y)
{
    if (s.pts.empty()) return std::numeric_limits<float>::max();
    return std::sqrt(distanceToSquared(s, x, y));
}

bool hitTest(const Stroke& s, float x, float y, float threshold)
{
    if (s.pts.empty()) return false;
    return distanceToSquared(s, x, y) <= threshold * threshold;
}

} // namespace notes
