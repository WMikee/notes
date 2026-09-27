#pragma once
#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QListWidget;
class QPushButton;

namespace notes {

struct PageRef
{
    int notebook = 0;
    int pageId = 0;
    QString notebookName;
    QString name;
    bool isCurrent = false;
};

class PagePickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PagePickerDialog(const QVector<PageRef>& pages, QWidget* parent = nullptr);

    QVector<int> selectedRows() const;

    void setNotebookChecked(int notebook, bool on);

private:
    void setAllChecked(bool on);
    void invertSelection();
    void updateSummary();
    void updateNotebookStates();
    int checkedCount() const;

    QVector<PageRef> pages_;
    QVector<int> headerRows_;
    QVector<int> notebookOfHeader_;
    QVector<int> pageRows_;
    QListWidget* list_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* confirm_ = nullptr;
    bool updating_ = false;
};

}
