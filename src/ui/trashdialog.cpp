#include "ui/trashdialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "io/library.h"
#include "theme.h"

namespace notes {

TrashDialog::TrashDialog(Library* library, QWidget* parent)
    : QDialog(parent)
    , lib_(library)
{
    setWindowTitle(tr("Papelera de reciclaje"));
    setModal(true);
    setMinimumSize(420, 440);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 16);
    root->setSpacing(12);

    auto* title = new QLabel(tr("Papelera de reciclaje"), this);
    title->setObjectName("dialogTitle");
    root->addWidget(title);

    list_ = new QListWidget(this);
    list_->setObjectName("trashList");
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setIconSize(QSize(18, 18));
    list_->setSpacing(1);
    root->addWidget(list_, 1);

    emptyLabel_ = new QLabel(tr("La papelera esta vacia."), this);
    emptyLabel_->setObjectName("trashEmpty");
    emptyLabel_->setAlignment(Qt::AlignCenter);
    root->addWidget(emptyLabel_, 1);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    restoreBtn_ = new QPushButton(tr("Restaurar"), this);
    restoreBtn_->setObjectName("primaryButton");
    deleteBtn_ = new QPushButton(tr("Eliminar definitivamente"), this);
    deleteBtn_->setObjectName("dangerButton");
    emptyBtn_ = new QPushButton(tr("Vaciar papelera"), this);
    for (QPushButton* b : {restoreBtn_, deleteBtn_, emptyBtn_}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
    }
    actions->addWidget(restoreBtn_);
    actions->addWidget(deleteBtn_);
    actions->addStretch(1);
    actions->addWidget(emptyBtn_);
    root->addLayout(actions);

    auto* box = new QDialogButtonBox(this);
    auto* close = box->addButton(tr("Cerrar"), QDialogButtonBox::RejectRole);
    close->setObjectName("quietButton");
    close->setCursor(Qt::PointingHandCursor);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);

    connect(list_, &QListWidget::itemSelectionChanged, this, [this] {
        const bool has = selectedIndex() >= 0;
        restoreBtn_->setEnabled(has);
        deleteBtn_->setEnabled(has);
    });
    connect(list_, &QListWidget::itemDoubleClicked, this,
        [this] { restoreSelected(); });
    connect(restoreBtn_, &QPushButton::clicked, this, &TrashDialog::restoreSelected);
    connect(deleteBtn_, &QPushButton::clicked, this, &TrashDialog::deleteSelected);
    connect(emptyBtn_, &QPushButton::clicked, this, &TrashDialog::emptyAll);

    setStyleSheet(
        QStringLiteral("QDialog { background-color: %1; }"
        "QLabel#dialogTitle { color: %2; font-size: 15px; font-weight: 600; }"
        "QLabel#trashEmpty { color: %3; font-size: 13px; }"
        "QListWidget#trashList { background-color: %4; border: 1px solid %5; border-radius: 8px; }"
        "QListWidget#trashList::item { border-radius: 4px; color: %2; padding: 0 6px; }"
        "QListWidget#trashList::item:selected { background-color: %6; }"
        "QPushButton { background-color: %6; color: %2; border: 1px solid %5; border-radius: 7px;"
        " padding: 6px 16px; }"
        "QPushButton:hover { border-color: %7; }"
        "QPushButton:disabled { color: %3; border-color: %5; }"
        "QPushButton#quietButton { background-color: transparent; }"
        "QPushButton#primaryButton { border-color: %7; }"
        "QPushButton#dangerButton { color: #ff6b6b; }")
            .arg(theme::kPanel.name(),
             theme::kTextPrimary.name(),
             theme::kTextDim.name(),
             theme::kCanvasBackground.name(),
             theme::kOutline.name(),
             theme::kPanelRaised.name(),
             theme::kGrooveFill.name()));

    reload();
}

void TrashDialog::reload()
{
    list_->clear();
    for (int i = 0; i < lib_->trashCount(); ++i) {
        const TrashItem* item = lib_->trashItemAt(i);
        if (!item) continue;
        const QIcon icon(item->isPage ? ":/assets/page.png" : ":/assets/book.png");
        QString text;
        if (item->isPage) {
            text = item->notebookName.isEmpty()
                ? item->name
                : tr("%1  -  %2").arg(item->name, item->notebookName);
        } else {
            text = tr("%1  -  cuaderno (%2 paginas)")
                .arg(item->name).arg(item->pages.size());
        }
        auto* row = new QListWidgetItem(icon, text, list_);
        row->setData(Qt::UserRole, i);
        row->setSizeHint(QSize(0, 34));
    }

    const bool empty = list_->count() == 0;
    list_->setVisible(!empty);
    emptyLabel_->setVisible(empty);
    emptyBtn_->setEnabled(!empty);
    restoreBtn_->setEnabled(false);
    deleteBtn_->setEnabled(false);
}

int TrashDialog::selectedIndex() const
{
    const QListWidgetItem* item = list_->currentItem();
    if (!item || !item->isSelected()) return -1;
    return item->data(Qt::UserRole).toInt();
}

void TrashDialog::restoreSelected()
{
    const int index = selectedIndex();
    if (index < 0) return;
    lib_->restoreTrashItem(index);
    reload();
}

void TrashDialog::deleteSelected()
{
    const int index = selectedIndex();
    if (index < 0) return;
    const TrashItem* item = lib_->trashItemAt(index);
    if (!item) return;
    const auto r = QMessageBox::question(this, tr("Papelera de reciclaje"),
        tr("Eliminar \"%1\" definitivamente?").arg(item->name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes) return;
    lib_->removeTrashItem(index);
    reload();
}

void TrashDialog::emptyAll()
{
    if (lib_->trashCount() == 0) return;
    const auto r = QMessageBox::question(this, tr("Papelera de reciclaje"),
        tr("Vaciar la papelera? Se borraran %1 elementos definitivamente.")
            .arg(lib_->trashCount()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes) return;
    lib_->emptyTrash();
    reload();
}

} // namespace notes
