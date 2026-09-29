#include "ui/sidepanel.h"
#include "ui/anim.h"
#include "ui/trashdialog.h"
#include "io/library.h"
#include "theme.h"
#include <QEasingCurve>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace notes {

ItemRow::ItemRow(const QIcon& icon, const QString& text, bool isPage, int index, QWidget* parent)
    : QWidget(parent)
    , icon_(icon)
    , text_(text)
    , isPage_(isPage)
    , index_(index)
{
    setFixedHeight(32);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);

    auto* layout = new QHBoxLayout(this);
    constexpr int kIcon = 20;
    layout->setContentsMargins(8 + kIcon + 8, 0, 4, 0);
    layout->addStretch(1);

    more_ = new QToolButton(this);
    more_->setIcon(QIcon(":/assets/more.png"));
    more_->setIconSize(QSize(16, 16));
    more_->setFixedSize(24, 24);
    more_->setCursor(Qt::PointingHandCursor);
    more_->setFocusPolicy(Qt::NoFocus);
    more_->setToolTip(tr("Opciones"));
    layout->addWidget(more_);

    connect(more_, &QToolButton::clicked, this, &ItemRow::moreClicked);
}

void ItemRow::setSelected(bool on)
{
    if (selected_ == on) return;
    selected_ = on;
    update();
}

void ItemRow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect rc = rect().adjusted(2, 2, -2, -2);
    if (selected_ || hoverT_ > 0.0) {
        p.setPen(Qt::NoPen);
        p.setBrush(selected_
            ? QColor(255, 255, 255, 32)
            : QColor(255, 255, 255, int(14 * hoverT_)));
        p.drawRoundedRect(rc, 8, 8);
    }

    constexpr int kIcon = 20;
    const QRect iconRect(rc.left() + 6, (height() - kIcon) / 2, kIcon, kIcon);
    icon_.paint(&p, iconRect);

    const int rightEdge = more_ && more_->isVisible() ? more_->geometry().left() - 6 : rc.right() - 4;
    const int left = rc.left() + 6 + kIcon + 8;
    const QRect textRect(left, 0, qMax(1, rightEdge - left), height());

    p.setFont(font());
    p.setPen(selected_ ? QColor(0xff, 0xff, 0xff) : notes::theme::kTextDim);
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(text_, Qt::ElideRight, textRect.width()));
}

void ItemRow::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        emit clicked();
    QWidget::mousePressEvent(e);
}

void ItemRow::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        emit doubleClicked();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void ItemRow::startHoverFade(bool on)
{
    if (hoverAnim_) {
        hoverAnim_->stop();
        hoverAnim_->deleteLater();
        hoverAnim_ = nullptr;
    }
    if (!notes::anim::enabled()) {
        hoverT_ = on ? 1.0 : 0.0;
        update();
        return;
    }
    QVariantAnimation* anim = new QVariantAnimation(this);
    anim->setDuration(120);
    anim->setStartValue(hoverT_);
    anim->setEndValue(on ? 1.0 : 0.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
        hoverT_ = val.toDouble();
        update();
    });
    hoverAnim_ = anim;
    anim->start();
}

void ItemRow::enterEvent(QEnterEvent*)
{
    startHoverFade(true);
}

void ItemRow::leaveEvent(QEvent*)
{
    startHoverFade(false);
}

ItemMenu::ItemMenu(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setObjectName("itemMenu");
    setMinimumWidth(170);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(2);

    QPushButton* duplicateBtn = new QPushButton(QIcon(":/assets/duplicate.png"), tr("Duplicar"), this);
    duplicateBtn->setIconSize(QSize(16, 16));
    duplicateBtn->setMinimumHeight(30);
    duplicateBtn->setCursor(Qt::PointingHandCursor);
    duplicateBtn->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(duplicateBtn);

    QPushButton* deleteBtn = new QPushButton(QIcon(":/assets/delete.png"), tr("Eliminar"), this);
    deleteBtn->setObjectName("deleteBtn");
    deleteBtn->setIconSize(QSize(16, 16));
    deleteBtn->setMinimumHeight(30);
    deleteBtn->setCursor(Qt::PointingHandCursor);
    deleteBtn->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(deleteBtn);

    setStyleSheet(
        QStringLiteral("QPushButton { border: none; border-radius: 6px; padding: 0 10px; "
                       "text-align: left; color: %1; background: transparent; font-size: 13px; }"
                       "QPushButton:hover { background: %2; }"
                       "QPushButton#deleteBtn { color: #ff6b6b; }"
                       "QPushButton#deleteBtn:hover { background: rgba(255,110,110,18%); }")
            .arg(notes::theme::kTextPrimary.name(),
                 notes::theme::alphaCss(QColor(Qt::white), 10)));

    connect(duplicateBtn, &QPushButton::clicked, this,
        [this] { emit duplicateRequested(index_, isPage_); close(); });
    connect(deleteBtn, &QPushButton::clicked, this,
        [this] { emit deleteRequested(index_, isPage_); close(); });

    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, this, [this] { close(); });
}

void ItemMenu::popupAt(const QPoint& globalPos, int index, bool isPage)
{
    index_ = index;
    isPage_ = isPage;
    adjustSize();
    QPoint pos = globalPos;
    if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
        const QRect sg = screen->availableGeometry();
        pos.setX(qBound(sg.left(), pos.x(), sg.right() - width() + 1));
        pos.setY(qBound(sg.top(), pos.y(), sg.bottom() - height() + 1));
    }
    move(pos);
    show();
    raise();

    if (menuFade_) {
        menuFade_->stop();
        menuFade_->deleteLater();
        menuFade_ = nullptr;
    }
    if (menuRise_) {
        menuRise_->stop();
        menuRise_->deleteLater();
        menuRise_ = nullptr;
    }
    if (notes::anim::enabled()) {
        auto* eff = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(eff);
        eff->setOpacity(0.0);
        menuFade_ = new QPropertyAnimation(eff, "opacity", this);
        menuFade_->setDuration(140);
        menuFade_->setStartValue(0.0);
        menuFade_->setEndValue(1.0);
        menuFade_->setEasingCurve(QEasingCurve::OutCubic);
        menuFade_->start();

        const QPoint target = pos;
        move(pos.x(), pos.y() + 6);
        menuRise_ = new QPropertyAnimation(this, "pos", this);
        menuRise_->setDuration(140);
        menuRise_->setStartValue(QPoint(pos.x(), pos.y() + 6));
        menuRise_->setEndValue(target);
        menuRise_->setEasingCurve(QEasingCurve::OutCubic);
        menuRise_->start();
    }
}

void ItemMenu::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);
    p.setPen(QPen(notes::theme::kOutline, 1));
    p.setBrush(notes::theme::kPanelRaised);
    p.drawPath(path);
}

SidePanel::SidePanel(Library* library, QWidget* parent)
    : QWidget(parent)
    , lib_(library)
{
    setObjectName("sidePanel");
    setFixedWidth(280);

    setStyleSheet(
        QStringLiteral("QToolButton { border: none; border-radius: 6px; background: transparent; }"
                       "QToolButton:hover { background: %1; }"
                       "QLabel#headerTitle { color: %2; font-size: 13px; font-weight: 600; "
                       "background: transparent; }"
                       "QFrame#sideDivider { background-color: rgba(255,255,255,14%); border: none; }"
                       "QScrollArea { background: transparent; border: none; }"
                       "QScrollBar:vertical { background: transparent; width: 4px; margin: 0; }"
                       "QScrollBar::handle:vertical { background: rgba(255,255,255,25%); "
                       "border-radius: 2px; min-height: 20px; }"
                       "QScrollBar::handle:vertical:hover { background: rgba(255,255,255,40%); }"
                       "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
                       "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical "
                       "{ background: transparent; }")
            .arg(notes::theme::alphaCss(QColor(Qt::white), 10),
                 notes::theme::kTextPrimary.name()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 10, 16);
    root->setSpacing(6);

    QWidget* nbHeader = new QWidget(this);
    auto* nbHeaderLayout = new QHBoxLayout(nbHeader);
    nbHeaderLayout->setContentsMargins(0, 0, 0, 4);
    nbHeaderLayout->setSpacing(4);

    QLabel* nbTitle = new QLabel("NoteBooks", nbHeader);
    nbTitle->setObjectName("headerTitle");
    nbHeaderLayout->addWidget(nbTitle);

    QToolButton* nbPlus = plusButton(nbHeader);
    connect(nbPlus, &QToolButton::clicked, this, &SidePanel::handleAddNotebook);
    nbHeaderLayout->addWidget(nbPlus);

    nbHeaderLayout->addStretch(1);

    QToolButton* trashBtn = new QToolButton(nbHeader);
    trashBtn->setIcon(QIcon(":/assets/trash.png"));
    trashBtn->setIconSize(QSize(15, 15));
    trashBtn->setFixedSize(26, 26);
    trashBtn->setFocusPolicy(Qt::NoFocus);
    trashBtn->setToolTip(tr("Papelera de reciclaje"));
    connect(trashBtn, &QToolButton::clicked, this, &SidePanel::openTrash);
    nbHeaderLayout->addWidget(trashBtn);

    forwardBtn_ = new QToolButton(nbHeader);
    forwardBtn_->setIcon(QIcon(":/assets/forward.png"));
    forwardBtn_->setIconSize(QSize(14, 14));
    forwardBtn_->setFixedSize(26, 26);
    forwardBtn_->setFocusPolicy(Qt::NoFocus);
    forwardBtn_->setToolTip(tr("Ocultar panel"));
    connect(forwardBtn_, &QToolButton::clicked, this, &SidePanel::collapseRequested);
    nbHeaderLayout->addWidget(forwardBtn_);

    root->addWidget(nbHeader);

    QWidget* nbScrollContent = new QWidget(this);
    bookLayout_ = new QVBoxLayout(nbScrollContent);
    bookLayout_->setContentsMargins(0, 0, 0, 0);
    bookLayout_->setSpacing(1);
    QScrollArea* nbScroll = makeScrollArea(nbScrollContent);
    root->addWidget(nbScroll, 1);

    root->addSpacing(8);

    QFrame* divider = new QFrame(this);
    divider->setObjectName("sideDivider");
    divider->setFrameShape(QFrame::HLine);
    divider->setFixedHeight(1);
    root->addWidget(divider);

    root->addSpacing(8);

    QWidget* pgHeader = new QWidget(this);
    auto* pgHeaderLayout = new QHBoxLayout(pgHeader);
    pgHeaderLayout->setContentsMargins(0, 0, 0, 4);
    pgHeaderLayout->setSpacing(4);

    QLabel* pgTitle = new QLabel("Pages", pgHeader);
    pgTitle->setObjectName("headerTitle");
    pgHeaderLayout->addWidget(pgTitle);

    QToolButton* pgPlus = plusButton(pgHeader);
    connect(pgPlus, &QToolButton::clicked, this, &SidePanel::handleAddPage);
    pgHeaderLayout->addWidget(pgPlus);
    pgHeaderLayout->addStretch(1);

    root->addWidget(pgHeader);

    QWidget* pgScrollContent = new QWidget(this);
    pageLayout_ = new QVBoxLayout(pgScrollContent);
    pageLayout_->setContentsMargins(0, 0, 0, 0);
    pageLayout_->setSpacing(1);
    QScrollArea* pgScroll = makeScrollArea(pgScrollContent);
    root->addWidget(pgScroll, 1);

    menu_ = new ItemMenu(this);
    connect(menu_, &ItemMenu::duplicateRequested, this,
        [this](int i, bool p) { duplicateItemById(p, i); });
    connect(menu_, &ItemMenu::deleteRequested, this,
        [this](int i, bool p) { removeItemById(p, i); });

    connect(lib_, &Library::changed, this, &SidePanel::onLibraryChanged);

    rebuildNotebooks();
    rebuildPages();
}

QToolButton* SidePanel::plusButton(QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setIcon(QIcon(":/assets/plus-thin.png"));
    button->setIconSize(QSize(14, 14));
    button->setFixedSize(24, 24);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tr("Añadir"));
    return button;
}

ItemRow* SidePanel::makeRow(bool isPage, int index, const QString& text)
{
    const QIcon icon = isPage ? QIcon(":/assets/page.png") : QIcon(":/assets/book.png");
    auto* row = new ItemRow(icon, text, isPage, index, this);
    row->setObjectName("itemRow");

    const bool selected = isPage
        ? (lib_->pageAt(selectedBook_, index) != nullptr
           && lib_->pageAt(selectedBook_, index)->id == selectedPageId_)
        : (index == selectedBook_);
    row->setSelected(selected);

    connect(row, &ItemRow::clicked, this, [this, isPage, index] { onRowClicked(isPage, index); });
    connect(row, &ItemRow::doubleClicked, this,
        [this, isPage, index] { onRowDoubleClicked(isPage, index); });
    connect(row, &ItemRow::moreClicked, this, [this, row, isPage, index] {
        menu_->popupAt(row->mapToGlobal(QPoint(row->width(), row->height() + 2)), index, isPage);
    });
    return row;
}

void SidePanel::rebuildNotebooks()
{
    clearLayout(bookLayout_);
    for (int i = 0; i < lib_->notebookCount(); ++i)
        bookLayout_->addWidget(makeRow(false, i, lib_->notebookName(i)));
    bookLayout_->addStretch(1);
}

void SidePanel::rebuildPages()
{
    clearLayout(pageLayout_);
    if (selectedBook_ < 0) return;
    for (int i = 0; i < lib_->pageCount(selectedBook_); ++i)
        pageLayout_->addWidget(makeRow(true, i, lib_->pageAt(selectedBook_, i)->name));
    pageLayout_->addStretch(1);
}

QScrollArea* SidePanel::makeScrollArea(QWidget* content)
{
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    scroll->setWidget(content);
    return scroll;
}

void SidePanel::clearLayout(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

void SidePanel::rememberSelection()
{
    if (!lib_ || selectedBook_ < 0 || selectedBook_ >= lib_->notebookCount()) {
        if (lib_) lib_->setActiveNotebookName(QString());
        return;
    }
    lib_->setActiveNotebookName(lib_->notebookName(selectedBook_));
    if (selectedPageId_ >= 0 && lib_->pageById(selectedBook_, selectedPageId_))
        lib_->setLastPageId(selectedBook_, selectedPageId_);
    else
        lib_->setLastPageId(selectedBook_, -1);
}

void SidePanel::selectDefault()
{
    selectNotebook(lib_->notebookCount() > 0 ? 0 : -1);
}

void SidePanel::activatePage(int notebookIndex, int pageId)
{
    if (notebookIndex < 0 || notebookIndex >= lib_->notebookCount()) return;
    if (lib_->pageById(notebookIndex, pageId) == nullptr) return;
    selectedBook_ = notebookIndex;
    selectedPageId_ = pageId;
    rememberSelection();
    rebuildNotebooks();
    rebuildPages();
    emit selectionChanged();
}

void SidePanel::openTrash()
{
    TrashDialog dialog(lib_, this);
    dialog.exec();
}

void SidePanel::selectNotebook(int index)
{
    if (index < -1) return;
    if (index >= lib_->notebookCount())
        index = lib_->notebookCount() > 0 ? lib_->notebookCount() - 1 : -1;
    selectedBook_ = index;
    selectedPageId_ = index >= 0 ? lib_->lastPageId(index) : -1;
    if (index >= 0 && (selectedPageId_ < 0
            || lib_->pageById(index, selectedPageId_) == nullptr)) {
        selectedPageId_ = lib_->pageCount(index) > 0
            ? lib_->pageAt(index, 0)->id : -1;
    }
    rememberSelection();
    rebuildPages();
    updateRowSelection();
    emit selectionChanged();
}

void SidePanel::selectPage(int indexInList)
{
    if (selectedBook_ < 0) return;
    const Page* p = lib_->pageAt(selectedBook_, indexInList);
    if (!p) return;
    selectedPageId_ = p->id;
    rememberSelection();
    updateRowSelection();
    emit selectionChanged();
}

void SidePanel::updateRowSelection()
{
    for (int i = 0; i < lib_->notebookCount(); ++i) {
        QLayoutItem* item = bookLayout_->itemAt(i);
        if (item) {
            auto* row = qobject_cast<ItemRow*>(item->widget());
            if (!row) continue;
            row->setSelected(i == selectedBook_);
        }
    }
    for (int i = 0; i < lib_->pageCount(selectedBook_); ++i) {
        QLayoutItem* item = pageLayout_->itemAt(i);
        if (item) {
            auto* row = qobject_cast<ItemRow*>(item->widget());
            if (!row) continue;
            const Page* page = lib_->pageAt(selectedBook_, i);
            row->setSelected(page && page->id == selectedPageId_);
        }
    }
}

void SidePanel::onRowClicked(bool isPage, int index)
{
    if (isPage) selectPage(index);
    else selectNotebook(index);
}

void SidePanel::onRowDoubleClicked(bool isPage, int index)
{
    int notebook = selectedBook_;
    int pageId = -1;
    QString oldName;
    if (isPage) {
        if (notebook < 0) return;
        const Page* page = lib_->pageAt(notebook, index);
        if (!page) return;
        pageId = page->id;
        oldName = page->name;
    } else {
        if (index < 0 || index >= lib_->notebookCount()) return;
        oldName = lib_->notebookName(index);
    }

    bool accepted = false;
    const QString title = isPage ? tr("Renombrar page") : tr("Renombrar notebook");
    const QString name = QInputDialog::getText(this, title, tr("Nombre:"),
                                                QLineEdit::Normal, oldName, &accepted).trimmed();
    if (!accepted || name.isEmpty() || name == oldName) return;

    const bool nameTaken = isPage
        ? lib_->pageNameExists(notebook, name)
        : lib_->notebookNameExists(name);
    if (nameTaken) {
        QMessageBox::warning(this, tr("Nombre en uso"),
                             tr("Ya existe un elemento con ese nombre."));
        return;
    }

    if (isPage) lib_->renamePage(notebook, pageId, name);
    else lib_->renameNotebook(index, name);
}

void SidePanel::onLibraryChanged()
{
    const int nc = lib_->notebookCount();
    int selected = selectedBook_;
    const QString activeName = lib_->activeNotebookName();
    if (!activeName.isEmpty()) {
        selected = -1;
        for (int i = 0; i < nc; ++i) {
            if (lib_->notebookName(i) == activeName) {
                selected = i;
                break;
            }
        }
    }
    if (selected < 0 || selected >= nc)
        selected = nc > 0 ? 0 : -1;
    selectedBook_ = selected;

    if (selectedBook_ >= 0) {
        if (lib_->pageById(selectedBook_, selectedPageId_) == nullptr) {
            selectedPageId_ = lib_->lastPageId(selectedBook_);
            if (selectedPageId_ < 0
                || lib_->pageById(selectedBook_, selectedPageId_) == nullptr) {
                selectedPageId_ = lib_->pageCount(selectedBook_) > 0
                    ? lib_->pageAt(selectedBook_, 0)->id : -1;
            }
        }
    } else {
        selectedPageId_ = -1;
    }

    rememberSelection();
    rebuildNotebooks();
    rebuildPages();
    emit selectionChanged();
}

void SidePanel::duplicateItemById(bool isPage, int index)
{
    if (!isPage) {
        if (index < 0 || index >= lib_->notebookCount()) return;
        const QString name = uniqueName(lib_->notebookName(index) + " (copia)", false);
        const int newIndex = lib_->duplicateNotebook(index, name);
        if (newIndex >= 0) selectNotebook(newIndex);
        return;
    }
    if (selectedBook_ < 0) return;
    const Page* p = lib_->pageAt(selectedBook_, index);
    if (!p) return;
    const QString name = uniqueName(p->name + " (copia)", true);
    const int newId = lib_->duplicatePage(selectedBook_, p->id, name);
    if (newId >= 0) {
        selectedPageId_ = newId;
        rememberSelection();
        rebuildPages();
        emit selectionChanged();
    }
}

void SidePanel::removeItemById(bool isPage, int index)
{
    if (!isPage) {
        if (index < 0 || index >= lib_->notebookCount()) return;
        if (lib_->pageCount(index) > 0) {
            const auto r = QMessageBox::question(this, tr("Notas"),
                tr("Enviar \"%1\" y sus paginas a la papelera?")
                    .arg(lib_->notebookName(index)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (r != QMessageBox::Yes) return;
        }
        lib_->removeNotebook(index);
        return;
    }
    if (selectedBook_ < 0) return;
    const Page* p = lib_->pageAt(selectedBook_, index);
    if (!p) return;
    lib_->removePage(selectedBook_, p->id);
}

void SidePanel::handleAddNotebook()
{
    const QString name = freshNotebookName();
    const int index = lib_->addNotebook(name);
    if (index >= 0) selectNotebook(index);
}

void SidePanel::handleAddPage()
{
    if (lib_->notebookCount() == 0) {
        handleAddNotebook();
        return;
    }
    int nb = selectedBook_;
    if (nb < 0 || nb >= lib_->notebookCount())
        nb = lib_->notebookCount() - 1;
    selectedBook_ = nb;
    selectedPageId_ = -1;
    const QString name = freshPageName();
    const int newId = lib_->addPage(nb, name);
    if (newId >= 0) {
        selectedPageId_ = newId;
        rememberSelection();
        rebuildPages();
        emit selectionChanged();
    }
}

QString SidePanel::uniqueName(const QString& base, bool isPage) const
{
    QString candidate = base;
    int n = 2;
    while (isPage ? lib_->pageNameExists(selectedBook_, candidate)
                  : lib_->notebookNameExists(candidate)) {
        candidate = QString("%1 %2").arg(base).arg(n++);
    }
    return candidate;
}

QString SidePanel::freshNotebookName() const
{
    int n = lib_->notebookCount() + 1;
    QString name;
    do {
        name = QString("NoteBook %1").arg(n++);
    } while (lib_->notebookNameExists(name));
    return name;
}

QString SidePanel::freshPageName() const
{
    int n = lib_->pageCount(selectedBook_) + 1;
    QString name;
    do {
        name = QString("Page %1").arg(n++);
    } while (lib_->pageNameExists(selectedBook_, name));
    return name;
}

void SidePanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal r = 18.0;
    QPainterPath path;
    path.addRoundedRect(QRectF(0.0, 0.0, width(), height()), r, r);
    p.fillPath(path, notes::theme::kPanel);
}

} // namespace notes
