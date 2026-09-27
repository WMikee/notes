#pragma once
#include <QIcon>
#include <QString>
#include <QWidget>

class QEvent;
class QLabel;
class QLayout;
class QMouseEvent;
class QPaintEvent;
class QPropertyAnimation;
class QScrollArea;
class QToolButton;
class QVariantAnimation;
class QVBoxLayout;

namespace notes {

class Library;
class Page;

class ItemRow : public QWidget
{
    Q_OBJECT
public:
    ItemRow(const QIcon& icon, const QString& text, bool isPage, int index, QWidget* parent = nullptr);

    void setSelected(bool on);
    bool isSelected() const { return selected_; }

signals:
    void clicked();
    void moreClicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void startHoverFade(bool on);

    QIcon icon_;
    QString text_;
    bool isPage_;
    int index_;
    bool selected_ = false;
    qreal hoverT_ = 0.0;
    QVariantAnimation* hoverAnim_ = nullptr;
    QToolButton* more_ = nullptr;
};

class ItemMenu : public QWidget
{
    Q_OBJECT
public:
    explicit ItemMenu(QWidget* parent = nullptr);

    void popupAt(const QPoint& globalPos, int index, bool isPage);

signals:
    void duplicateRequested(int index, bool isPage);
    void deleteRequested(int index, bool isPage);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    int index_ = -1;
    bool isPage_ = false;
    QPropertyAnimation* menuFade_ = nullptr;
    QPropertyAnimation* menuRise_ = nullptr;
};

class SidePanel : public QWidget
{
    Q_OBJECT
public:
    explicit SidePanel(Library* library, QWidget* parent = nullptr);

    Library* library() const { return lib_; }
    int selectedNotebook() const { return selectedBook_; }
    int selectedPageId() const { return selectedPageId_; }

    void selectDefault();
    void selectNotebook(int index);
    void activatePage(int notebookIndex, int pageId);

signals:
    void selectionChanged();
    void collapseRequested();

public slots:
    void handleAddNotebook();
    void handleAddPage();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    ItemRow* makeRow(bool isPage, int index, const QString& text);
    void rebuildNotebooks();
    void rebuildPages();
    void clearLayout(QLayout* layout);
    QScrollArea* makeScrollArea(QWidget* content);
    void rememberSelection();
    void selectPage(int indexInList);
    void onRowClicked(bool isPage, int index);
    void onLibraryChanged();
    void duplicateItemById(bool isPage, int index);
    void removeItemById(bool isPage, int index);
    QString freshNotebookName() const;
    QString freshPageName() const;
    QString uniqueName(const QString& base, bool isPage) const;
    static QToolButton* plusButton(QWidget* parent);

    Library* lib_ = nullptr;
    int selectedBook_ = -1;
    int selectedPageId_ = -1;
    QVBoxLayout* bookLayout_ = nullptr;
    QVBoxLayout* pageLayout_ = nullptr;
    QToolButton* forwardBtn_ = nullptr;
    ItemMenu* menu_ = nullptr;
};

} // namespace notes
