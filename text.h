#pragma once
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <vector>

class QPainter;

namespace notes {

struct TextBox {
    int id = 0;
    QPointF pos;
    QString text;
    QColor color = QColor(0x14, 0x14, 0x1a);
    double width = 240.0;
    double fontPx = 16.0;

    mutable std::vector<QString> lines;
    mutable double height = 0.0;
    mutable bool layoutDirty = true;

    QRectF rect() const;
};

void relayoutTextBox(const TextBox& tb);

bool textHitTest(const TextBox& tb, const QPointF& world, double tol);

void drawTextBox(QPainter& p, const TextBox& tb,
                 float zoom, const QPointF& offset,
                 bool focused, int cursor, const QColor& accent,
                 int preeditStart = -1, int preeditLen = 0);

}