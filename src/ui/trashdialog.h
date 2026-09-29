#pragma once
#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;

namespace notes {

class Library;

class TrashDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TrashDialog(Library* library, QWidget* parent = nullptr);

private:
    void reload();
    int selectedIndex() const;
    void restoreSelected();
    void deleteSelected();
    void emptyAll();

    Library* lib_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QPushButton* restoreBtn_ = nullptr;
    QPushButton* deleteBtn_ = nullptr;
    QPushButton* emptyBtn_ = nullptr;
};

}
