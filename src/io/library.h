#pragma once
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace notes {

struct Page
{
    int id = 0;
    QString name;
    QByteArray doc;
};

struct Notebook
{
    QString name;
    QList<Page> pages;
    int lastPageId = -1;
};

struct TrashItem
{
    bool isPage = true;
    QString name;
    QString notebookName;
    int pageId = 0;
    QByteArray doc;
    QList<Page> pages;
    int lastPageId = -1;
    qint64 deletedAt = 0;
};

class Library : public QObject
{
    Q_OBJECT
public:
    explicit Library(QObject* parent = nullptr);

    void load();
    void saveNow();

    bool isEmpty() const { return notebooks_.isEmpty(); }
    int notebookCount() const { return notebooks_.size(); }
    QString notebookName(int index) const;

    int pageCount(int notebookIndex) const;
    const Page* pageAt(int notebookIndex, int pos) const;
    const Page* pageById(int notebookIndex, int pageId) const;
    int pageIndexById(int notebookIndex, int pageId) const;
    QByteArray pageDocumentById(int notebookIndex, int pageId) const;
    void setPageDocumentById(int notebookIndex, int pageId, const QByteArray& bytes);
    int lastPageId(int notebookIndex) const;
    void setLastPageId(int notebookIndex, int pageId);

    QString savedTool() const { return savedTool_; }
    void setSavedTool(const QString& tool);
    QString savedColor() const { return savedColor_; }
    void setSavedColor(const QString& color);
    QString savedColorForTool(const QString& tool) const;
    void setSavedColorForTool(const QString& tool, const QString& color);
    QStringList savedColorPresets() const { return savedColorPresets_; }
    void setSavedColorPresets(const QStringList& colors);
    QString activeNotebookName() const { return activeNotebookName_; }
    void setActiveNotebookName(const QString& name);
    int savedSize(const QString& tool) const;
    void setSavedSize(const QString& tool, int size);
    bool savedPressure() const { return savedPressure_; }
    void setSavedPressure(bool on);
    bool savedStabilizer() const { return savedStabilizer_; }
    void setSavedStabilizer(bool on);
    bool savedPostSmooth() const { return savedPostSmooth_; }
    void setSavedPostSmooth(bool on);
    bool savedFixedGrid() const { return savedFixedGrid_; }
    void setSavedFixedGrid(bool on);
    bool savedHighlightBelow() const { return savedHighlightBelow_; }
    void setSavedHighlightBelow(bool on);

    bool notebookNameExists(const QString& name) const;
    bool pageNameExists(int notebookIndex, const QString& name) const;

    int addNotebook(const QString& name);
    int duplicateNotebook(int index, const QString& newName);
    void removeNotebook(int index);

    int addPage(int notebookIndex, const QString& name);
    int duplicatePage(int notebookIndex, int pageId, const QString& newName);
    void removePage(int notebookIndex, int pageId);
    void renameNotebook(int index, const QString& newName);
    void renamePage(int notebookIndex, int pageId, const QString& newName);

    int trashCount() const { return trash_.size(); }
    const TrashItem* trashItemAt(int index) const;
    void restoreTrashItem(int index);
    void removeTrashItem(int index);
    void emptyTrash();

signals:
    void changed();

private:
    bool loadDocument(const QString& path);
    int allocPageId();
    void scheduleSave();

    QList<Notebook> notebooks_;
    QList<TrashItem> trash_;
    int nextPageId_ = 1;
    QString savedTool_ = QStringLiteral("pointer");
    QString savedColor_ = QStringLiteral("#000000");
    QHash<QString, QString> toolColors_;
    QStringList savedColorPresets_ = {
        QStringLiteral("#000000"), QStringLiteral("#ff0000"), QStringLiteral("#ccff00")
    };
    QString activeNotebookName_;
    int savedSize_ = 12;
    QHash<QString, int> toolSizes_;
    bool savedPressure_ = true;
    bool savedStabilizer_ = true;
    bool savedPostSmooth_ = false;
    bool savedFixedGrid_ = false;
    bool savedHighlightBelow_ = true;
    QString path_;
    bool loadedFromBackup_ = false;
    QTimer* saveTimer_ = nullptr;
};

} // namespace notes
