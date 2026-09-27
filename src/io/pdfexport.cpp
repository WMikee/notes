#include "io/pdfexport.h"

#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QTransform>
#include <algorithm>

#include "scene/image.h"
#include "scene/page.h"
#include "scene/shape.h"
#include "scene/stroke.h"
#include "scene/text.h"

namespace notes {

namespace {

constexpr qreal kPtPerUnit = 0.75;

QPainterPath strokePath(const StrokeOutline& o)
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

    addPolygon(o.body);
    addPolygon(o.startCap);
    addPolygon(o.endCap);
    return path;
}

void drawDocument(QPainter& p, const DocumentData& data)
{
    for (const Stroke& st : data.strokes) {
        const QPainterPath path = strokePath(strokeOutline(st));
        if (path.isEmpty()) continue;
        p.fillPath(path, st.color);
    }
    for (const ShapeItem& sh : data.shapes)
        drawShapeItem(p, sh, 1.0f, QPointF());
    for (const ImageItem& im : data.images)
        drawImageItem(p, im, 1.0f, QPointF());
    for (const TextBox& t : data.texts)
        drawTextBox(p, t, 1.0f, QPointF(), false, -1, QColor());
}

}

bool exportPdf(const QString& path, const QVector<PdfPage>& pages, QString* error)
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
        p.setClipRect(QRectF(0.0, 0.0, kPageWidth, kPageHeight), Qt::IntersectClip);
        drawDocument(p, pages[i].data);
        p.restore();
    }

    p.end();
    return true;
}

}
