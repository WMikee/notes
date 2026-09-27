#include "library.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

namespace notes {

namespace {
constexpr int kLibraryVersion = 1;
constexpr int kMinSize = 1;
constexpr int kMaxSize = 48;

int defaultSizeForTool(const QString& tool)
{
    if (tool == QLatin1String("eraser")) return 7;
    return 12;
}
}

Library::Library(QObject* parent)
    : QObject(parent)
{
    path_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/library.json");
    saveTimer_ = new QTimer(this);
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(400);
    connect(saveTimer_, &QTimer::timeout, this, &Library::saveNow);
}

void Library::load()
{
    QFile f(path_);
    if (!f.open(QIODeviceBase::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject root = doc.object();
    const QJsonObject ui = root.value("uiState").toObject();

    savedTool_ = ui.value("tool").toString(savedTool_);
    savedColor_ = ui.value("color").toString(savedColor_);
    activeNotebookName_ = ui.value("activeNotebook").toString();
    savedSize_ = qBound(kMinSize, ui.value("size").toInt(savedSize_), kMaxSize);
    savedPressure_ = ui.value("pressure").toBool(savedPressure_);
    const QJsonObject sizes = ui.value("sizes").toObject();
    for (auto it = sizes.constBegin(); it != sizes.constEnd(); ++it)
        toolSizes_.insert(it.key(), qBound(kMinSize, it.value().toInt(kMinSize), kMaxSize));
    nextPageId_ = qMax(1, root.value("nextPageId").toInt(1));
    notebooks_.clear();
    const QJsonArray books = root.value("notebooks").toArray();
    for (const QJsonValue& bv : books) {
        const QJsonObject bo = bv.toObject();
        Notebook nb;
        nb.name = bo.value("name").toString();
        nb.lastPageId = bo.value("lastPageId").toInt(-1);
        const QJsonArray ps = bo.value("pages").toArray();
        for (const QJsonValue& pv : ps) {
            const QJsonObject po = pv.toObject();
            Page p;
            p.id = po.value("id").toInt();
            p.name = po.value("name").toString();
            p.doc = QByteArray::fromBase64(po.value("doc").toString().toLatin1());
            if (p.id >= nextPageId_) nextPageId_ = p.id + 1;
            nb.pages.append(p);
        }
        bool hasLastPage = false;
        for (const Page& p : nb.pages) {
            if (p.id == nb.lastPageId) {
                hasLastPage = true;
                break;
            }
        }
        if (!hasLastPage)
            nb.lastPageId = nb.pages.isEmpty() ? -1 : nb.pages.first().id;
        notebooks_.append(nb);
    }
    saveTimer_->stop();
}

void Library::saveNow()
{
    saveTimer_->stop();
    QJsonObject root;
    root["version"] = kLibraryVersion;
    root["nextPageId"] = nextPageId_;
    QJsonObject ui;
    ui["tool"] = savedTool_;
    ui["color"] = savedColor_;
    ui["activeNotebook"] = activeNotebookName_;
    ui["size"] = savedSize(QStringLiteral("pencil"));
    QJsonObject sizes;
    for (auto it = toolSizes_.constBegin(); it != toolSizes_.constEnd(); ++it)
        sizes.insert(it.key(), it.value());
    ui["sizes"] = sizes;
    ui["pressure"] = savedPressure_;
    root["uiState"] = ui;
    QJsonArray books;
    for (const Notebook& nb : notebooks_) {
        QJsonObject bo;
        bo["name"] = nb.name;
        bo["lastPageId"] = nb.lastPageId;
        QJsonArray ps;
        for (const Page& p : nb.pages) {
            QJsonObject po;
            po["id"] = p.id;
            po["name"] = p.name;
            po["doc"] = QString::fromLatin1(p.doc.toBase64());
            ps.append(po);
        }
        bo["pages"] = ps;
        books.append(bo);
    }
    root["notebooks"] = books;

    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSaveFile f(path_);
    if (!f.open(QIODeviceBase::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

void Library::scheduleSave()
{
    saveTimer_->start();
}

int Library::allocPageId()
{
    return nextPageId_++;
}

QString Library::notebookName(int index) const
{
    if (index < 0 || index >= notebooks_.size()) return {};
    return notebooks_[index].name;
}

int Library::pageCount(int notebookIndex) const
{
    if (notebookIndex < 0 || notebookIndex >= notebooks_.size()) return 0;
    return notebooks_[notebookIndex].pages.size();
}

const Page* Library::pageAt(int notebookIndex, int pos) const
{
    if (notebookIndex < 0 || notebookIndex >= notebooks_.size()) return nullptr;
    const QList<Page>& pages = notebooks_[notebookIndex].pages;
    if (pos < 0 || pos >= pages.size()) return nullptr;
    return &pages[pos];
}

const Page* Library::pageById(int notebookIndex, int pageId) const
{
    if (notebookIndex < 0 || notebookIndex >= notebooks_.size()) return nullptr;
    for (const Page& p : notebooks_[notebookIndex].pages)
        if (p.id == pageId)
            return &p;
    return nullptr;
}

int Library::pageIndexById(int notebookIndex, int pageId) const
{
    if (notebookIndex < 0 || notebookIndex >= notebooks_.size()) return -1;
    const QList<Page>& pages = notebooks_[notebookIndex].pages;
    for (int i = 0; i < pages.size(); ++i)
        if (pages[i].id == pageId)
            return i;
    return -1;
}

QByteArray Library::pageDocumentById(int notebookIndex, int pageId) const
{
    const Page* p = pageById(notebookIndex, pageId);
    return p ? p->doc : QByteArray();
}

void Library::setPageDocumentById(int notebookIndex, int pageId, const QByteArray& bytes)
{
    Page* p = const_cast<Page*>(pageById(notebookIndex, pageId));
    if (!p) return;
    p->doc = bytes;
    scheduleSave();
}

int Library::lastPageId(int notebookIndex) const
{
    if (notebookIndex < 0 || notebookIndex >= notebooks_.size()) return -1;
    return notebooks_[notebookIndex].lastPageId;
}

void Library::setLastPageId(int notebookIndex, int pageId)
{
    if (notebookIndex < 0 || notebookIndex >= notebooks_.size()) return;
    if (pageId >= 0 && pageById(notebookIndex, pageId) == nullptr) return;
    if (notebooks_[notebookIndex].lastPageId == pageId) return;
    notebooks_[notebookIndex].lastPageId = pageId;
    scheduleSave();
}

void Library::setSavedTool(const QString& tool)
{
    if (tool.isEmpty() || tool == savedTool_) return;
    savedTool_ = tool;
    scheduleSave();
}

void Library::setSavedColor(const QString& color)
{
    if (color.isEmpty() || color == savedColor_) return;
    savedColor_ = color;
    scheduleSave();
}

void Library::setActiveNotebookName(const QString& name)
{
    if (name == activeNotebookName_) return;
    activeNotebookName_ = name;
    scheduleSave();
}

int Library::savedSize(const QString& tool) const
{
    const auto it = toolSizes_.constFind(tool);
    if (it != toolSizes_.constEnd()) return it.value();
    if (tool == QLatin1String("pencil")) return savedSize_;
    return defaultSizeForTool(tool);
}

void Library::setSavedSize(const QString& tool, int size)
{
    if (tool.isEmpty()) return;
    size = qBound(kMinSize, size, kMaxSize);
    if (savedSize(tool) == size && toolSizes_.contains(tool)) return;
    toolSizes_.insert(tool, size);
    scheduleSave();
}

void Library::setSavedPressure(bool on)
{
    if (on == savedPressure_) return;
    savedPressure_ = on;
    scheduleSave();
}

bool Library::notebookNameExists(const QString& name) const
{
    for (const Notebook& nb : notebooks_)
        if (nb.name == name)
            return true;
    return false;
}

bool Library::pageNameExists(int notebookIndex, const QString& name) const
{
    if (notebookIndex < 0 || notebookIndex >= notebooks_.size()) return false;
    for (const Page& p : notebooks_[notebookIndex].pages)
        if (p.name == name)
            return true;
    return false;
}

int Library::addNotebook(const QString& name)
{
    Notebook nb;
    nb.name = name;
    notebooks_.append(nb);
    emit changed();
    scheduleSave();
    return notebooks_.size() - 1;
}

int Library::duplicateNotebook(int index, const QString& newName)
{
    if (index < 0 || index >= notebooks_.size()) return -1;
    Notebook copy = notebooks_[index];
    copy.name = newName;
    for (Page& p : copy.pages)
        p.id = allocPageId();
    copy.lastPageId = copy.pages.isEmpty() ? -1 : copy.pages.first().id;
    notebooks_.insert(index + 1, copy);
    emit changed();
    scheduleSave();
    return index + 1;
}

void Library::removeNotebook(int index)
{
    if (index < 0 || index >= notebooks_.size()) return;
    notebooks_.removeAt(index);
    emit changed();
    scheduleSave();
}

int Library::addPage(int notebookIndex, const QString& name)
{
    if (notebookIndex < 0 || notebookIndex >= notebooks_.size()) return -1;
    Page p;
    p.id = allocPageId();
    p.name = name;
    notebooks_[notebookIndex].pages.append(p);
    if (notebooks_[notebookIndex].lastPageId < 0)
        notebooks_[notebookIndex].lastPageId = p.id;
    emit changed();
    scheduleSave();
    return p.id;
}

int Library::duplicatePage(int notebookIndex, int pageId, const QString& newName)
{
    const int pos = pageIndexById(notebookIndex, pageId);
    if (pos < 0) return -1;
    Page copy = notebooks_[notebookIndex].pages[pos];
    copy.id = allocPageId();
    copy.name = newName;
    notebooks_[notebookIndex].pages.insert(pos + 1, copy);
    if (notebooks_[notebookIndex].lastPageId < 0)
        notebooks_[notebookIndex].lastPageId = copy.id;
    emit changed();
    scheduleSave();
    return copy.id;
}

void Library::removePage(int notebookIndex, int pageId)
{
    const int pos = pageIndexById(notebookIndex, pageId);
    if (pos < 0) return;
    notebooks_[notebookIndex].pages.removeAt(pos);
    if (notebooks_[notebookIndex].lastPageId == pageId) {
        notebooks_[notebookIndex].lastPageId = notebooks_[notebookIndex].pages.isEmpty()
            ? -1 : notebooks_[notebookIndex].pages.first().id;
    }
    emit changed();
    scheduleSave();
}

} // namespace notes