#include "text.h"
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <algorithm>

namespace notes {

QRectF TextBox::rect() const
{
    return QRectF(pos, QSizeF(width, height));
}

void relayoutTextBox(const TextBox& tb)
{
    if (!tb.layoutDirty) return;
    QFont f;
    f.setPixelSize(std::max(1, int(std::lround(tb.fontPx))));
    const QFontMetricsF fm(f);
    const double wrap = tb.width;
    const double lh = fm.lineSpacing();

    tb.lines.clear();
    const QStringList paras = tb.text.split(QLatin1Char('\n'));
    for (const QString& para : paras) {
        if (para.isEmpty()) {
            tb.lines.push_back(QString());
            continue;
        }
        QString cur;
        const QStringList words = para.split(QLatin1Char(' '));
        for (const QString& w : words) {
            QString trial = cur;
            if (!cur.isEmpty()) trial += QLatin1Char(' ');
            trial += w;
            if (cur.isEmpty() || fm.horizontalAdvance(trial) <= wrap)
                cur = trial;
            else {
                tb.lines.push_back(cur);
                cur = w;
            }
        }
        tb.lines.push_back(cur);
    }
    if (tb.lines.empty()) tb.lines.push_back(QString());
    tb.height = double(tb.lines.size()) * lh;
    tb.layoutDirty = false;
}

bool textHitTest(const TextBox& tb, const QPointF& world, double tol)
{
    return tb.rect().adjusted(-tol, -tol, tol, tol).contains(world);
}

void drawTextBox(QPainter& p, const TextBox& tb,
                 float zoom, const QPointF& offset,
                 bool focused, int cursor, const QColor& accent,
                 int preeditStart, int preeditLen)
{
    relayoutTextBox(tb);

    const QPointF tl(tb.pos.x() * zoom + offset.x(),
                     tb.pos.y() * zoom + offset.y());

    QFont f;
    f.setPixelSize(std::max(1, int(std::lround(tb.fontPx * zoom))));
    const QFontMetricsF fm(f);
    const double lh = fm.lineSpacing();
    const double ascent = fm.ascent();
    p.setFont(f);

    if (focused) {
        const QRectF r = QRectF(tl, QSizeF(tb.width * zoom, tb.height * zoom))
                             .adjusted(-3, -3, 3, 3);
        p.setPen(QPen(accent, 1.0, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
    }

    p.setPen(QColor(tb.color));
    const double baseY = tl.y() + ascent;
    for (size_t i = 0; i < tb.lines.size(); ++i) {
        const double y = baseY + double(i) * lh;
        p.drawText(QPointF(tl.x(), y), tb.lines[i]);
    }

    if (focused) {
        const QString before = tb.text.left(std::max(0, cursor));
        const int nls = int(before.count(QLatin1Char('\n')));
        const int ls = before.lastIndexOf(QLatin1Char('\n'));
        const QString partial = ls >= 0 ? before.mid(ls + 1) : before;
        const double caretX = tl.x() + fm.horizontalAdvance(partial);
        const double top = tl.y() + double(nls) * lh;
        p.setPen(QPen(accent, 1.2));
        p.drawLine(QPointF(caretX, top), QPointF(caretX, top + lh));
    }

    if (focused && preeditStart >= 0 && preeditLen > 0) {
        const QString head = tb.text.left(std::min(preeditStart, int(tb.text.size())));
        const int nls = int(head.count(QLatin1Char('\n')));
        const int ls = head.lastIndexOf(QLatin1Char('\n'));
        const QString partial = ls >= 0 ? head.mid(ls + 1) : head;
        const QString pre = tb.text.mid(preeditStart, preeditLen);
        const double x0 = tl.x() + fm.horizontalAdvance(partial);
        const double x1 = x0 + fm.horizontalAdvance(pre);
        const double top = tl.y() + double(nls) * lh;
        p.setPen(QPen(accent, 1.0));
        p.drawLine(QPointF(x0, top + lh - 1.0), QPointF(x1, top + lh - 1.0));
    }
}

}