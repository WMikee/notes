#pragma once
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

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
    QString activeNotebookName() const { return activeNotebookName_; }
    void setActiveNotebookName(const QString& name);
    int savedSize(const QString& tool) const;
    void setSavedSize(const QString& tool, int size);
    bool savedPressure() const { return savedPressure_; }
    void setSavedPressure(bool on);

    bool notebookNameExists(const QString& name) const;
    bool pageNameExists(int notebookIndex, const QString& name) const;

    int addNotebook(const QString& name);
    int duplicateNotebook(int index, const QString& newName);
    void removeNotebook(int index);

    int addPage(int notebookIndex, const QString& name);
    int duplicatePage(int notebookIndex, int pageId, const QString& newName);
    void removePage(int notebookIndex, int pageId);

signals:
    void changed();

private:
    int allocPageId();
    void scheduleSave();

    QList<Notebook> notebooks_;
    int nextPageId_ = 1;
    QString savedTool_ = QStringLiteral("pointer");
    QString savedColor_ = QStringLiteral("#000000");
    QString activeNotebookName_;
    int savedSize_ = 12;
    QHash<QString, int> toolSizes_;
    bool savedPressure_ = true;
    QString path_;
    QTimer* saveTimer_ = nullptr;
};

} // namespace notes