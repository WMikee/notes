#include "storage.h"
#include <QBuffer>
#include <QDataStream>
#include <QIODevice>
#include <QImage>
#include <algorithm>

namespace notes {

namespace {
constexpr quint32 kMagic = 0x4E4F4454; // "NODT"
constexpr quint32 kVersion = 4;
constexpr quint32 kVersion3 = 3;
constexpr quint32 kVersion2 = 2;
constexpr quint32 kVersion1 = 1;

void serializeShape(QDataStream& ds, const ShapeItem& sh)
{
    ds << sh.rect;
    ds << sh.color;
    ds << sh.penWidth;
    ds << quint8(sh.kind);
}

bool deserializeShape(QDataStream& ds, ShapeItem& sh)
{
    quint8 kind = 0;
    ds >> sh.rect;
    ds >> sh.color;
    ds >> sh.penWidth;
    ds >> kind;
    switch (ShapeKind(kind)) {
    case ShapeKind::Rectangle:
    case ShapeKind::Triangle:
    case ShapeKind::Ellipse:
        sh.kind = ShapeKind(kind);
        break;
    default:
        sh.kind = ShapeKind::Rectangle;
        break;
    }
    return ds.status() == QDataStream::Ok;
}

void serializeStroke(QDataStream& ds, const Stroke& s)
{
    ds << s.color;
    ds << s.size;
    ds << quint32(s.pts.size());
    for (const Pt& p : s.pts)
        ds << p.x << p.y << p.p;
}

bool deserializeStroke(QDataStream& ds, Stroke& s, quint32 version)
{
    quint32 n = 0;
    ds >> s.color;
    s.size = kDefaultStrokeSize;
    if (version >= 4) ds >> s.size;
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
    return ds.status() == QDataStream::Ok;
}

void serializeTextBox(QDataStream& ds, const TextBox& t)
{
    ds << t.pos << t.text << t.color << t.width << t.fontPx;
}

bool deserializeTextBox(QDataStream& ds, TextBox& t)
{
    ds >> t.pos >> t.text >> t.color >> t.width >> t.fontPx;
    t.layoutDirty = true;
    return ds.status() == QDataStream::Ok;
}

void serializeImage(QDataStream& ds, const ImageItem& im)
{
    ds << im.pos << im.width << im.height;
    QByteArray img;
    if (!im.source.isNull()) {
        QBuffer buf(&img);
        buf.open(QIODeviceBase::WriteOnly);
        im.source.save(&buf, "PNG");
    }
    ds << img;
}

bool deserializeImage(QDataStream& ds, ImageItem& im)
{
    QByteArray img;
    ds >> im.pos >> im.width >> im.height;
    ds >> img;
    im.source = QImage::fromData(img);
    return ds.status() == QDataStream::Ok;
}
}

QByteArray serializeDocument(const DocumentData& data)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODeviceBase::WriteOnly);
    ds.setVersion(QDataStream::Qt_6_2);
    ds << kMagic << kVersion;
    ds << data.camera.zoom << data.camera.offset;
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
    ds << data.nextId;
    return bytes;
}

bool deserializeDocument(const QByteArray& bytes, DocumentData& out)
{
    QDataStream ds(bytes);
    ds.setVersion(QDataStream::Qt_6_2);
    quint32 magic = 0, version = 0;
    ds >> magic >> version;
    if (magic != kMagic || (version != kVersion && version != kVersion3
        && version != kVersion2 && version != kVersion1)
        || ds.status() != QDataStream::Ok)
        return false;

    out = DocumentData{};
    ds >> out.camera.zoom >> out.camera.offset;
    quint32 ns = 0;
    ds >> ns;
    out.strokes.reserve(ns);
    for (quint32 i = 0; i < ns; ++i) {
        Stroke s;
        if (!deserializeStroke(ds, s, version)) return false;
        out.strokes.push_back(std::move(s));
    }
    quint32 nt = 0;
    ds >> nt;
    out.texts.reserve(nt);
    for (quint32 i = 0; i < nt; ++i) {
        TextBox t;
        if (!deserializeTextBox(ds, t)) return false;
        out.texts.push_back(std::move(t));
    }
    if (version >= 2) {
        quint32 ni = 0;
        ds >> ni;
        out.images.reserve(ni);
        for (quint32 i = 0; i < ni; ++i) {
            ImageItem im;
            if (!deserializeImage(ds, im)) return false;
            out.images.push_back(std::move(im));
        }
    }
    if (version >= 3) {
        quint32 nsh = 0;
        ds >> nsh;
        out.shapes.reserve(nsh);
        for (quint32 i = 0; i < nsh; ++i) {
            ShapeItem sh;
            if (!deserializeShape(ds, sh)) return false;
            out.shapes.push_back(std::move(sh));
        }
    }
    if (version < 4) {
        for (Stroke& s : out.strokes) s.size = kDefaultStrokeSize;
    }
    ds >> out.nextId;
    return ds.status() == QDataStream::Ok;
}

}