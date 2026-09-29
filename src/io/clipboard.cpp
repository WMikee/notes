#include "io/clipboard.h"
#include <QBuffer>
#include <QColor>
#include <QDataStream>
#include <QIODevice>
#include <QImage>
#include <algorithm>

namespace notes {

namespace {
constexpr quint32 kClipMagic = 0x4E4F5445;
constexpr quint32 kClipVersion = 6;
constexpr quint32 kClipVersion5 = 5;
constexpr quint32 kClipVersion4 = 4;
constexpr quint32 kClipVersion3 = 3;
constexpr quint32 kClipVersion2 = 2;
constexpr quint32 kClipVersion1 = 1;

QDataStream& serializeShape(QDataStream& ds, const ShapeItem& sh)
{
    ds << sh.rect;
    ds << sh.color;
    ds << sh.penWidth;
    ds << quint8(sh.kind);
    ds << sh.rot;
    ds << quint32(sh.nodes.size());
    for (const CurveNode& n : sh.nodes)
        ds << n.pos << n.in << n.out;
    return ds;
}

QDataStream& deserializeShape(QDataStream& ds, ShapeItem& sh, quint32 version)
{
    quint8 kind = 0;
    ds >> sh.rect;
    ds >> sh.color;
    ds >> sh.penWidth;
    ds >> kind;
    sh.rot = 0.0;
    if (version >= 4) ds >> sh.rot;
    if (version >= kClipVersion) {
        quint32 nn = 0;
        ds >> nn;
        sh.nodes.resize(nn);
        for (quint32 i = 0; i < nn; ++i)
            ds >> sh.nodes[i].pos >> sh.nodes[i].in >> sh.nodes[i].out;
    }
    switch (ShapeKind(kind)) {
    case ShapeKind::Rectangle:
    case ShapeKind::Triangle:
    case ShapeKind::Ellipse:
    case ShapeKind::Curve:
        sh.kind = ShapeKind(kind);
        break;
    default:
        sh.kind = ShapeKind::Rectangle;
        break;
    }
    return ds;
}

QDataStream& serializeStroke(QDataStream& ds, const Stroke& s)
{
    ds << s.color;
    ds << s.size;
    ds << quint32(s.pts.size());
    for (const Pt& p : s.pts)
        ds << p.x << p.y << p.p;
    return ds;
}

QDataStream& deserializeStroke(QDataStream& ds, Stroke& s, quint32 version)
{
    quint32 n = 0;
    ds >> s.color;
    s.size = kDefaultStrokeSize;
    if (version >= kClipVersion5) ds >> s.size;
    ds >> n;
    s.pts.clear();
    s.pts.reserve(n);
    for (quint32 i = 0; i < n; ++i) {
        Pt p{};
        ds >> p.x >> p.y >> p.p;
        s.pts.push_back(p);
    }
    float pmin = 1.0f, pmax = 0.0f;
    for (const Pt& p : s.pts) {
        pmin = std::min(pmin, p.p);
        pmax = std::max(pmax, p.p);
    }
    s.complete = true;
    s.verts.clear();
    return ds;
}

QDataStream& serializeTextBox(QDataStream& ds, const TextBox& t)
{
    ds << t.pos << t.text << t.color << t.width << t.fontPx;
    ds << t.rot;
    return ds;
}

QDataStream& deserializeTextBox(QDataStream& ds, TextBox& t, quint32 version)
{
    ds >> t.pos >> t.text >> t.color >> t.width >> t.fontPx;
    t.rot = 0.0;
    if (version >= 4) ds >> t.rot;
    t.layoutDirty = true;
    return ds;
}

QDataStream& serializeImage(QDataStream& ds, const ImageItem& im)
{
    ds << im.pos << im.width << im.height << im.rot;
    QByteArray img;
    if (!im.source.isNull()) {
        QBuffer buf(&img);
        buf.open(QIODeviceBase::WriteOnly);
        im.source.save(&buf, "PNG");
    }
    ds << img;
    return ds;
}

QDataStream& deserializeImage(QDataStream& ds, ImageItem& im, quint32 version)
{
    QByteArray img;
    ds >> im.pos >> im.width >> im.height;
    im.rot = 0.0;
    if (version >= 4) ds >> im.rot;
    ds >> img;
    im.source = QImage::fromData(img);
    return ds;
}
}

QByteArray serializeClipboard(const ClipboardData& data)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODeviceBase::WriteOnly);
    ds.setVersion(QDataStream::Qt_6_2);
    ds << kClipMagic << kClipVersion;
    ds << quint32(data.strokes.size());
    for (const Stroke& s : data.strokes)
        serializeStroke(ds, s);
    ds << quint32(data.texts.size());
    for (const TextBox& t : data.texts)
        serializeTextBox(ds, t);
    ds << quint32(data.images.size());
    for (const ImageItem& im : data.images)
        serializeImage(ds, im);
    ds << quint32(data.shapes.size());
    for (const ShapeItem& sh : data.shapes)
        serializeShape(ds, sh);
    return bytes;
}

bool deserializeClipboard(const QByteArray& bytes, ClipboardData& out)
{
    QDataStream ds(bytes);
    ds.setVersion(QDataStream::Qt_6_2);
    quint32 magic = 0, version = 0;
    ds >> magic >> version;
    if (magic != kClipMagic || (version != kClipVersion && version != kClipVersion5
        && version != kClipVersion4
        && version != kClipVersion3 && version != kClipVersion2 && version != kClipVersion1)
        || ds.status() != QDataStream::Ok)
        return false;

    out = ClipboardData{};
    quint32 ns = 0;
    ds >> ns;
    out.strokes.reserve(ns);
    for (quint32 i = 0; i < ns; ++i) {
        Stroke s;
        deserializeStroke(ds, s, version);
        out.strokes.push_back(std::move(s));
    }
    quint32 nt = 0;
    ds >> nt;
    out.texts.reserve(nt);
    for (quint32 i = 0; i < nt; ++i) {
        TextBox t;
        deserializeTextBox(ds, t, version);
        out.texts.push_back(std::move(t));
    }
    if (version >= 2) {
        quint32 ni = 0;
        ds >> ni;
        out.images.reserve(ni);
        for (quint32 i = 0; i < ni; ++i) {
            ImageItem im;
            deserializeImage(ds, im, version);
            out.images.push_back(std::move(im));
        }
    }
    if (version >= 3) {
        quint32 nsh = 0;
        ds >> nsh;
        out.shapes.reserve(nsh);
        for (quint32 i = 0; i < nsh; ++i) {
            ShapeItem sh;
            deserializeShape(ds, sh, version);
            out.shapes.push_back(std::move(sh));
        }
    }
    return ds.status() == QDataStream::Ok;
}

}
