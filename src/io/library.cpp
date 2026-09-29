#include "io/library.h"
#include <QDateTime>
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
constexpr int kLibraryVersion = 2;
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
    if (!loadDocument(path_)) {
        if (loadDocument(path_ + QStringLiteral(".bak")))
            loadedFromBackup_ = true;
    }
    saveTimer_->stop();
}

bool Library::loadDocument(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODeviceBase::ReadOnly)) return false;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject root = doc.object();
    const QJsonObject ui = root.value("uiState").toObject();

    savedTool_ = ui.value("tool").toString(savedTool_);
    savedColor_ = ui.value("color").toString(savedColor_);
    toolColors_.clear();
    const QJsonObject toolColors = ui.value("toolColors").toObject();
    for (auto it = toolColors.constBegin(); it != toolColors.constEnd(); ++it) {
        const QString color = it.value().toString();
        if (!color.isEmpty()) toolColors_.insert(it.key(), color);
    }
    const QJsonArray colorPresets = ui.value("colorPresets").toArray();
    if (colorPresets.size() == 3) {
        QStringList loadedPresets;
        bool validPresets = true;
        for (const QJsonValue& value : colorPresets) {
            const QString color = value.toString();
            if (color.isEmpty()) validPresets = false;
            loadedPresets.append(color);
        }
        if (validPresets) savedColorPresets_ = loadedPresets;
    }
    activeNotebookName_ = ui.value("activeNotebook").toString();
    savedSize_ = qBound(kMinSize, ui.value("size").toInt(savedSize_), kMaxSize);
    savedPressure_ = ui.value("pressure").toBool(savedPressure_);
    savedStabilizer_ = ui.value("stabilizer").toBool(savedStabilizer_);
    savedFixedGrid_ = ui.value("fixedGrid").toBool(savedFixedGrid_);
    savedHighlightBelow_ = ui.value("highlightBelow").toBool(savedHighlightBelow_);
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
    trash_.clear();
    const QJsonArray trashArr = root.value("trash").toArray();
    for (const QJsonValue& tv : trashArr) {
        const QJsonObject to = tv.toObject();
        TrashItem item;
        item.isPage = to.value("isPage").toBool(true);
        item.name = to.value("name").toString();
        item.notebookName = to.value("notebookName").toString();
        item.pageId = to.value("pageId").toInt();
        item.doc = QByteArray::fromBase64(to.value("doc").toString().toLatin1());
        item.lastPageId = to.value("lastPageId").toInt(-1);
        item.deletedAt = static_cast<qint64>(to.value("deletedAt").toDouble());
        const QJsonArray ps = to.value("pages").toArray();
        for (const QJsonValue& pv : ps) {
            const QJsonObject po = pv.toObject();
            Page p;
            p.id = po.value("id").toInt();
            p.name = po.value("name").toString();
            p.doc = QByteArray::fromBase64(po.value("doc").toString().toLatin1());
            if (p.id >= nextPageId_) nextPageId_ = p.id + 1;
            item.pages.append(p);
        }
        if (item.pageId >= nextPageId_) nextPageId_ = item.pageId + 1;
        trash_.append(item);
    }
    return true;
}

void Library::saveNow()
{
    saveTimer_->stop();

    if (QFileInfo::exists(path_)) {
        const QString backup = path_ + QStringLiteral(".bak");
        if (loadedFromBackup_) {
            loadedFromBackup_ = false;
        } else {
            QFile::remove(backup);
            QFile::copy(path_, backup);
        }
    }

    QJsonObject root;
    root["version"] = kLibraryVersion;
    root["nextPageId"] = nextPageId_;
    QJsonObject ui;
    ui["tool"] = savedTool_;
    ui["color"] = savedColor_;
    QJsonObject toolColors;
    for (auto it = toolColors_.constBegin(); it != toolColors_.constEnd(); ++it)
        toolColors.insert(it.key(), it.value());
    ui["toolColors"] = toolColors;
    QJsonArray colorPresets;
    for (const QString& color : savedColorPresets_)
        colorPresets.append(color);
    ui["colorPresets"] = colorPresets;
    ui["activeNotebook"] = activeNotebookName_;
    ui["size"] = savedSize(QStringLiteral("pencil"));
    QJsonObject sizes;
    for (auto it = toolSizes_.constBegin(); it != toolSizes_.constEnd(); ++it)
        sizes.insert(it.key(), it.value());
    ui["sizes"] = sizes;
    ui["pressure"] = savedPressure_;
    ui["stabilizer"] = savedStabilizer_;
    ui["fixedGrid"] = savedFixedGrid_;
    ui["highlightBelow"] = savedHighlightBelow_;
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

    QJsonArray trashArr;
    for (const TrashItem& item : trash_) {
        QJsonObject to;
        to["isPage"] = item.isPage;
        to["name"] = item.name;
        to["notebookName"] = item.notebookName;
        to["pageId"] = item.pageId;
        to["doc"] = QString::fromLatin1(item.doc.toBase64());
        to["lastPageId"] = item.lastPageId;
        to["deletedAt"] = static_cast<double>(item.deletedAt);
        QJsonArray ps;
        for (const Page& p : item.pages) {
            QJsonObject po;
            po["id"] = p.id;
            po["name"] = p.name;
            po["doc"] = QString::fromLatin1(p.doc.toBase64());
            ps.append(po);
        }
        to["pages"] = ps;
        trashArr.append(to);
    }
    root["trash"] = trashArr;

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

QString Library::savedColorForTool(const QString& tool) const
{
    return toolColors_.value(tool);
}

void Library::setSavedColorForTool(const QString& tool, const QString& color)
{
    if (tool.isEmpty() || color.isEmpty()) return;
    const auto it = toolColors_.constFind(tool);
    if (it != toolColors_.constEnd() && it.value() == color) return;
    toolColors_.insert(tool, color);
    scheduleSave();
}

void Library::setSavedColorPresets(const QStringList& colors)
{
    if (colors.size() != 3 || colors.contains(QString())) return;
    if (colors == savedColorPresets_) return;
    savedColorPresets_ = colors;
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

void Library::setSavedStabilizer(bool on)
{
    if (on == savedStabilizer_) return;
    savedStabilizer_ = on;
    scheduleSave();
}

void Library::setSavedFixedGrid(bool on)
{
    if (on == savedFixedGrid_) return;
    savedFixedGrid_ = on;
    scheduleSave();
}

void Library::setSavedHighlightBelow(bool on)
{
    if (on == savedHighlightBelow_) return;
    savedHighlightBelow_ = on;
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
    TrashItem item;
    item.isPage = false;
    item.name = notebooks_[index].name;
    item.pages = notebooks_[index].pages;
    item.lastPageId = notebooks_[index].lastPageId;
    item.deletedAt = QDateTime::currentMSecsSinceEpoch();
    trash_.prepend(item);
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
    const Page& page = notebooks_[notebookIndex].pages[pos];
    TrashItem item;
    item.isPage = true;
    item.name = page.name;
    item.notebookName = notebooks_[notebookIndex].name;
    item.pageId = page.id;
    item.doc = page.doc;
    item.deletedAt = QDateTime::currentMSecsSinceEpoch();
    trash_.prepend(item);
    notebooks_[notebookIndex].pages.removeAt(pos);
    if (notebooks_[notebookIndex].lastPageId == pageId) {
        notebooks_[notebookIndex].lastPageId = notebooks_[notebookIndex].pages.isEmpty()
            ? -1 : notebooks_[notebookIndex].pages.first().id;
    }
    emit changed();
    scheduleSave();
}

const TrashItem* Library::trashItemAt(int index) const
{
    if (index < 0 || index >= trash_.size()) return nullptr;
    return &trash_[index];
}

void Library::restoreTrashItem(int index)
{
    if (index < 0 || index >= trash_.size()) return;
    const TrashItem item = trash_[index];
    if (item.isPage) {
        int nb = -1;
        for (int i = 0; i < notebooks_.size(); ++i) {
            if (notebooks_[i].name == item.notebookName) {
                nb = i;
                break;
            }
        }
        if (nb < 0) {
            Notebook fresh;
            fresh.name = item.notebookName.isEmpty()
                ? QStringLiteral("NoteBook") : item.notebookName;
            if (notebookNameExists(fresh.name)) {
                int n = 2;
                while (notebookNameExists(QStringLiteral("%1 %2").arg(fresh.name).arg(n)))
                    ++n;
                fresh.name = QStringLiteral("%1 %2").arg(fresh.name).arg(n);
            }
            notebooks_.append(fresh);
            nb = notebooks_.size() - 1;
        }
        Page page;
        page.id = item.pageId;
        page.name = item.name;
        page.doc = item.doc;
        if (pageNameExists(nb, page.name)) {
            int n = 2;
            while (pageNameExists(nb, QStringLiteral("%1 %2").arg(page.name).arg(n)))
                ++n;
            page.name = QStringLiteral("%1 %2").arg(page.name).arg(n);
        }
        notebooks_[nb].pages.append(page);
        if (notebooks_[nb].lastPageId < 0)
            notebooks_[nb].lastPageId = page.id;
    } else {
        Notebook nb;
        nb.name = item.name;
        if (notebookNameExists(nb.name)) {
            int n = 2;
            while (notebookNameExists(QStringLiteral("%1 %2").arg(nb.name).arg(n)))
                ++n;
            nb.name = QStringLiteral("%1 %2").arg(nb.name).arg(n);
        }
        nb.pages = item.pages;
        nb.lastPageId = item.lastPageId;
        notebooks_.append(nb);
    }
    trash_.removeAt(index);
    emit changed();
    scheduleSave();
}

void Library::removeTrashItem(int index)
{
    if (index < 0 || index >= trash_.size()) return;
    trash_.removeAt(index);
    emit changed();
    scheduleSave();
}

void Library::emptyTrash()
{
    if (trash_.isEmpty()) return;
    trash_.clear();
    emit changed();
    scheduleSave();
}

void Library::renameNotebook(int index, const QString& newName)
{
    if (index < 0 || index >= notebooks_.size()) return;
    if (notebooks_[index].name == newName) return;
    notebooks_[index].name = newName;
    emit changed();
    scheduleSave();
}

void Library::renamePage(int notebookIndex, int pageId, const QString& newName)
{
    if (notebookIndex < 0 || notebookIndex >= notebooks_.size()) return;
    for (int i = 0; i < notebooks_[notebookIndex].pages.size(); ++i) {
        if (notebooks_[notebookIndex].pages[i].id == pageId) {
            if (notebooks_[notebookIndex].pages[i].name == newName) return;
            notebooks_[notebookIndex].pages[i].name = newName;
            emit changed();
            scheduleSave();
            return;
        }
    }
}

} // namespace notes
