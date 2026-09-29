#include "io/pdfexport.h"

#include <QColor>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPen>
#include <QSize>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "scene/image.h"
#include "scene/page.h"
#include "scene/shape.h"
#include "scene/stroke.h"
#include "scene/text.h"

namespace notes {

namespace {

constexpr qreal kPtPerUnit = 0.75;

constexpr int kPdfCapSegments = 16;
constexpr int kPdfCornerSegments = 8;
constexpr double kImageDpi = 200.0;

std::vector<QPointF> simplifyPolyline(const std::vector<QPointF>& in, double eps)
{
    if (in.size() < 3 || eps <= 0.0) return in;

    std::vector<char> keep(in.size(), 0);
    keep.front() = 1;
    keep.back() = 1;
    std::vector<std::pair<int, int>> stack{{0, int(in.size()) - 1}};

    while (!stack.empty()) {
        const auto [i0, i1] = stack.back();
        stack.pop_back();
        if (i1 <= i0 + 1) continue;

        const QPointF& a = in[size_t(i0)];
        const QPointF& b = in[size_t(i1)];
        const double dx = b.x() - a.x(), dy = b.y() - a.y();
        const double len = std::hypot(dx, dy);
        double best = -1.0;
        int bestI = -1;
        for (int i = i0 + 1; i < i1; ++i) {
            const QPointF& c = in[size_t(i)];
            const double d = len < 1e-9
                ? std::hypot(c.x() - a.x(), c.y() - a.y())
                : std::abs((c.x() - a.x()) * dy - (c.y() - a.y()) * dx) / len;
            if (d > best) {
                best = d;
                bestI = i;
            }
        }
        if (best > eps && bestI > i0 && bestI < i1) {
            keep[size_t(bestI)] = 1;
            stack.push_back({i0, bestI});
            stack.push_back({bestI, i1});
        }
    }

    std::vector<QPointF> out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i)
        if (keep[i]) out.push_back(in[i]);
    return out;
}

QImage downscaleImage(const QImage& src, const QSizeF& worldSize, double pageScale)
{
    if (src.isNull() || worldSize.isEmpty()) return src;
    const double factor = pageScale * kImageDpi / 72.0;
    const int tw = std::max(1, int(std::lround(worldSize.width() * factor)));
    const int th = std::max(1, int(std::lround(worldSize.height() * factor)));
    if (tw >= src.width() && th >= src.height()) return src;
    return src.scaled(QSize(std::min(tw, src.width()), std::min(th, src.height())),
                      Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QPainterPath strokePath(const StrokeOutline& o, double tol)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);

    const auto addPolygon = [&path](const std::vector<QPointF>& poly) {
        if (poly.size() < 2) return;
        std::vector<QPointF> pts = poly;
        double area = 0.0;
        for (size_t i = 0; i < pts.size(); ++i) {
            const QPointF& a = pts[i];
            const QPointF& b = pts[(i + 1) % pts.size()];
            area += a.x() * b.y() - b.x() * a.y();
        }
        if (area < 0.0) std::reverse(pts.begin(), pts.end());
        path.moveTo(pts.front());
        for (size_t i = 1; i < pts.size(); ++i) path.lineTo(pts[i]);
        path.closeSubpath();
    };

    if (o.body.size() >= 4 && o.body.size() % 2 == 0) {
        const size_t half = o.body.size() / 2;
        std::vector<QPointF> body(o.body.begin(), o.body.begin() + ptrdiff_t(half));
        const std::vector<QPointF> right(o.body.begin() + ptrdiff_t(half), o.body.end());
        body = simplifyPolyline(body, tol);
        const std::vector<QPointF> rs = simplifyPolyline(right, tol);
        body.insert(body.end(), rs.begin(), rs.end());
        addPolygon(body);
    } else {
        addPolygon(o.body);
    }
    addPolygon(o.startCap);
    addPolygon(o.endCap);
    return path;
}

void drawDocument(QPainter& p, const DocumentData& data, bool highlightBelow)
{
    const auto drawStroke = [&](const Stroke& st) {
        const double tol = std::clamp(double(st.size) * 0.02, 0.1, 0.4);
        const QPainterPath path =
            strokePath(strokeOutline(st, kPdfCapSegments, kPdfCornerSegments), tol);
        if (path.isEmpty()) return;
        p.fillPath(path, st.color);
    };
    if (highlightBelow) {
        for (const Stroke& st : data.strokes)
            if (st.color.alpha() < 255) drawStroke(st);
        for (const Stroke& st : data.strokes)
            if (st.color.alpha() >= 255) drawStroke(st);
    } else {
        for (const Stroke& st : data.strokes)
            drawStroke(st);
    }
    for (const ShapeItem& sh : data.shapes)
        drawShapeItem(p, sh, 1.0f, QPointF());
    const double pageScale = p.viewport().width() / double(kPageWidth);
    for (const ImageItem& im : data.images) {
        ImageItem scaled = im;
        scaled.source = downscaleImage(im.source, im.localRect().size(), pageScale);
        drawImageItem(p, scaled, 1.0f, QPointF());
    }
    for (const TextBox& t : data.texts)
        drawTextBox(p, t, 1.0f, QPointF(), false, -1, QColor());
}

void drawStaticGrid(QPainter& p)
{
    const QRectF page(kPageMargin, kPageMargin, kPageWidth, kPageHeight);
    p.save();
    p.setClipRect(page, Qt::IntersectClip);
    p.setPen(QPen(QColor(222, 227, 237), 1.0));

    for (qreal x = page.left(); x <= page.right(); x += kGridMinSpacing)
        p.drawLine(QPointF(x, page.top()), QPointF(x, page.bottom()));
    for (qreal y = page.top(); y <= page.bottom(); y += kGridMinSpacing)
        p.drawLine(QPointF(page.left(), y), QPointF(page.right(), y));

    p.restore();
}

}

bool exportPdf(const QString& path, const QVector<PdfPage>& pages,
               QString* error, bool includeStaticGrid, bool highlightBelow)
{
    const auto fail = [error](const QString& msg) {
        if (error) *error = msg;
        return false;
    };

    if (pages.isEmpty()) return fail(QStringLiteral("No hay paginas que exportar."));

    QPdfWriter writer(path);
    writer.setCreator(QStringLiteral("Notas"));
    writer.setTitle(pages.size() == 1 ? pages.front().name
                                      : QStringLiteral("%1 paginas").arg(pages.size()));
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QSizeF(kPageWidth * kPtPerUnit, kPageHeight * kPtPerUnit),
                                 QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter p(&writer);
    if (!p.isActive()) return fail(QStringLiteral("No se pudo iniciar el escritor de PDF."));

    const qreal scale = p.viewport().width() / qreal(kPageWidth);

    for (int i = 0; i < pages.size(); ++i) {
        if (i > 0 && !writer.newPage()) return fail(QStringLiteral("No se pudo anadir la pagina."));
        p.save();
        p.setTransform(QTransform().scale(scale, scale).translate(-kPageMargin, -kPageMargin));
        p.setClipRect(QRectF(kPageMargin, kPageMargin, kPageWidth, kPageHeight),
                      Qt::IntersectClip);
        if (includeStaticGrid) drawStaticGrid(p);
        drawDocument(p, pages[i].data, highlightBelow);
        p.restore();
    }

    p.end();
    return true;
}

}
