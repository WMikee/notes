#include "ui/pagepicker.h"

#include <QCoreApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QVBoxLayout>

#include "scene/page.h"
#include "theme.h"

namespace notes {

namespace {

constexpr int kRowData = Qt::UserRole;
constexpr int kPageIndexData = Qt::UserRole + 2;
constexpr int kNotebookData = Qt::UserRole + 3;
constexpr int kCollapsedData = Qt::UserRole + 4;
constexpr int kArrowWidth = 22;

class RowDelegate : public QStyledItemDelegate
{
public:
    explicit RowDelegate(PagePickerDialog* owner, QObject* parent)
        : QStyledItemDelegate(parent), owner_(owner) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.font.setBold(false);

        const QRect box = opt.rect;
        const bool isRow = index.data(kPageIndexData).isValid();
        const bool isHeader = index.data(kNotebookData).isValid();

        if (option.state & QStyle::State_MouseOver)
            painter->fillRect(box.adjusted(1, 0, -1, 0), theme::kPanelRaised);

        if (isHeader) {
            const bool collapsed = index.data(kCollapsedData).toBool();
            painter->save();

            const QRect arrow(box.left() + 2, box.top(), kArrowWidth, box.height());
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme::kTextDim);
            const QPointF ac = arrow.center();
            QPolygonF tri;
            if (collapsed)
                tri << QPointF(ac.x() - 3.0, ac.y() - 5.0)
                    << QPointF(ac.x() + 4.0, ac.y())
                    << QPointF(ac.x() - 3.0, ac.y() + 5.0);
            else
                tri << QPointF(ac.x() - 5.0, ac.y() - 3.0)
                    << QPointF(ac.x() + 5.0, ac.y() - 3.0)
                    << QPointF(ac.x(), ac.y() + 4.0);
            painter->drawPolygon(tri);

            const QRect mark(arrow.right() + 1, box.top(), 22, box.height());
            QPen pen(theme::kGrooveFill);
            pen.setWidthF(1.7);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            const QPointF c = mark.center();
            if (index.data(Qt::CheckStateRole).toInt() == Qt::Checked) {
                painter->drawPolyline(QPolygonF({QPointF(c.x() - 5.0, c.y()),
                                                  QPointF(c.x() - 1.5, c.y() + 3.5),
                                                  QPointF(c.x() + 5.5, c.y() - 4.0)}));
            } else if (index.data(Qt::CheckStateRole).toInt() == Qt::PartiallyChecked) {
                painter->drawLine(QPointF(c.x() - 5.0, c.y()), QPointF(c.x() + 5.0, c.y()));
            } else {
                painter->drawRoundedRect(QRectF(c.x() - 5.5, c.y() - 5.5, 11.0, 11.0), 2.5, 2.5);
            }
            QFont f = painter->font();
            f.setBold(true);
            f.setPointSizeF(f.pointSizeF() - 0.5);
            painter->setFont(f);
            painter->setPen(theme::kTextDim);
            painter->drawText(box.adjusted(mark.right() + 8, 0, -6, 2), Qt::AlignVCenter | Qt::AlignLeft,
                              index.data(Qt::DisplayRole).toString());
            painter->setPen(theme::kOutline);
            painter->drawLine(box.left() + 4, box.bottom() - 1, box.right() - 4, box.bottom() - 1);
            painter->restore();
            return;
        }

        const int markW = 22;
        const QRect mark(box.left() + 4, box.top(), markW, box.height());
        painter->save();
        QPen pen(theme::kGrooveFill);
        pen.setWidthF(1.7);
        painter->setPen(pen);
        const QPointF c = mark.center();
        if (index.data(Qt::CheckStateRole).toInt() == Qt::Checked) {
            painter->drawPolyline(QPolygonF({QPointF(c.x() - 5.0, c.y()),
                                              QPointF(c.x() - 1.5, c.y() + 3.5),
                                              QPointF(c.x() + 5.5, c.y() - 4.0)}));
        } else {
            painter->drawRoundedRect(QRectF(c.x() - 5.5, c.y() - 5.5, 11.0, 11.0), 2.5, 2.5);
        }
        painter->restore();

        const bool current = index.data(kRowData).toBool();
        QRect textRect = box;
        textRect.setLeft(mark.right() + 8);
        painter->save();
        QFont f = painter->font();
        f.setBold(current);
        painter->setFont(f);
        painter->setPen(current ? theme::kGrooveFill : theme::kTextPrimary);
        painter->drawText(textRect.adjusted(0, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft,
                          index.data(Qt::DisplayRole).toString());
        if (current) {
            painter->setPen(theme::kTextDim);
            painter->drawText(textRect.adjusted(0, 0, -16, 0), Qt::AlignVCenter | Qt::AlignRight,
                              QCoreApplication::translate("PagePickerDialog", "actual"));
        }
        painter->restore();
    }

    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option, const QModelIndex& index) override
    {
        if (event->type() != QEvent::MouseButtonRelease || !owner_) return false;
        const QVariant nb = index.data(kNotebookData);
        if (nb.isValid()) {
            auto* me = static_cast<QMouseEvent*>(event);
            const int lx = int(me->position().x()) - option.rect.left();
            if (lx < kArrowWidth) {
                owner_->toggleNotebookCollapsed(nb.toInt());
                return true;
            }
            const bool on = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
            owner_->setNotebookChecked(nb.toInt(), !on);
            return true;
        }
        if (!index.data(kPageIndexData).isValid()) return false;
        const bool on = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        model->setData(index, on ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
        return true;
    }

private:
    PagePickerDialog* owner_ = nullptr;
};

}

PagePickerDialog::PagePickerDialog(const QVector<PageRef>& pages, QWidget* parent)
    : QDialog(parent), pages_(pages)
{
    setWindowTitle(tr("Exportar a PDF"));
    setModal(true);
    setMinimumSize(420, 460);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 16);
    root->setSpacing(12);

    auto* title = new QLabel(tr("Paginas a exportar"), this);
    title->setObjectName("dialogTitle");
    root->addWidget(title);

    list_ = new QListWidget(this);
    list_->setObjectName("pageList");
    list_->setItemDelegate(new RowDelegate(this, list_));
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setUniformItemSizes(false);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setSpacing(1);
    list_->setMinimumHeight(320);
    root->addWidget(list_, 1);

    int number = 0;
    QString lastNotebook;
    for (int i = 0; i < pages_.size(); ++i) {
        const PageRef& p = pages_.at(i);
        if (i == 0 || p.notebookName != lastNotebook) {
            lastNotebook = p.notebookName;
            number = 0;
            const bool showHeader = pages_.size() > 1 && !p.notebookName.isEmpty();
            if (showHeader) {
                auto* header = new QListWidgetItem(p.notebookName);
                header->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                header->setCheckState(Qt::Checked);
                header->setData(kNotebookData, p.notebook);
                header->setData(kCollapsedData, true);
                header->setSizeHint(QSize(0, 28));
                headerRows_.push_back(list_->count());
                notebookOfHeader_.push_back(p.notebook);
                list_->addItem(header);
            }
        }
        ++number;
        auto* item = new QListWidgetItem(QStringLiteral("%1.  %2").arg(number).arg(p.name));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(kRowData, p.isCurrent);
        item->setData(kPageIndexData, i);
        item->setSizeHint(QSize(0, 30));
        pageRows_.push_back(list_->count());
        list_->addItem(item);
    }

    for (int nb : notebookOfHeader_)
        for (int i = 0; i < pages_.size(); ++i)
            if (pages_.at(i).notebook == nb)
                list_->item(pageRows_.at(i))->setHidden(true);

    gridOption_ = new QCheckBox(tr("Incluir cuadrícula estática (estilo cuaderno)"), this);
    gridOption_->setObjectName("gridOption");
    gridOption_->setCursor(Qt::PointingHandCursor);
    gridOption_->setToolTip(
        tr("Exporta una cuadrícula fija de %1 columnas por página").arg(kGridCellsAcross));
    root->addWidget(gridOption_);

    auto* quick = new QHBoxLayout;
    quick->setSpacing(8);
    auto* allBtn = new QPushButton(tr("Todas"), this);
    auto* noneBtn = new QPushButton(tr("Ninguna"), this);
    auto* invBtn = new QPushButton(tr("Invertir"), this);
    for (QPushButton* b : {allBtn, noneBtn, invBtn}) {
        b->setObjectName("quietButton");
        b->setCursor(Qt::PointingHandCursor);
        quick->addWidget(b);
    }
    quick->addStretch(1);
    summary_ = new QLabel(this);
    summary_->setObjectName("dialogSummary");
    quick->addWidget(summary_);
    root->addLayout(quick);

    connect(allBtn, &QPushButton::clicked, this, [this] { setAllChecked(true); });
    connect(noneBtn, &QPushButton::clicked, this, [this] { setAllChecked(false); });
    connect(invBtn, &QPushButton::clicked, this, &PagePickerDialog::invertSelection);
    connect(list_, &QListWidget::itemChanged, this, &PagePickerDialog::updateSummary);

    auto* box = new QDialogButtonBox(this);
    confirm_ = box->addButton(tr("Exportar"), QDialogButtonBox::AcceptRole);
    confirm_->setObjectName("primaryButton");
    confirm_->setDefault(true);
    confirm_->setCursor(Qt::PointingHandCursor);
    auto* cancel = box->addButton(tr("Cancelar"), QDialogButtonBox::RejectRole);
    cancel->setObjectName("quietButton");
    cancel->setCursor(Qt::PointingHandCursor);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);

    setStyleSheet(
        QStringLiteral("QDialog { background-color: %1; }"
        "QLabel#dialogTitle { color: %2; font-size: 15px; font-weight: 600; }"
        "QLabel#dialogSummary { color: %3; font-size: 12px; }"
        "QListWidget#pageList { background-color: %4; border: 1px solid %5; border-radius: 8px; }"
        "QListWidget#pageList::item { border-radius: 4px; }"
        "QListWidget#pageList::item:selected { background-color: %6; }"
        "QCheckBox#gridOption { color: %2; spacing: 8px; }"
        "QCheckBox#gridOption::indicator { width: 15px; height: 15px; border: 1px solid %5;"
        " border-radius: 4px; background-color: transparent; }"
        "QCheckBox#gridOption::indicator:checked { image: url(:/assets/check.png);"
        " border: 1px solid %7; background-color: transparent; }"
        "QPushButton { background-color: %6; color: %2; border: 1px solid %5; border-radius: 7px;"
        " padding: 6px 16px; }"
        "QPushButton:hover { border-color: %7; }"
        "QPushButton:disabled { color: %3; border-color: %5; }"
        "QPushButton#quietButton { background-color: transparent; }"
        "QPushButton#primaryButton { border-color: %7; }")
            .arg(theme::kPanel.name(),
             theme::kTextPrimary.name(),
             theme::kTextDim.name(),
             theme::kCanvasBackground.name(),
             theme::kOutline.name(),
             theme::kPanelRaised.name(),
             theme::kGrooveFill.name()));

    updateSummary();
}

QVector<int> PagePickerDialog::selectedRows() const
{
    QVector<int> out;
    for (int i = 0; i < list_->count(); ++i) {
        const QListWidgetItem* it = list_->item(i);
        if (it->data(kPageIndexData).isValid() && it->checkState() == Qt::Checked)
            out.push_back(it->data(kPageIndexData).toInt());
    }
    return out;
}

bool PagePickerDialog::includeStaticGrid() const
{
    return gridOption_ && gridOption_->isChecked();
}

int PagePickerDialog::checkedCount() const
{
    return selectedRows().size();
}

void PagePickerDialog::setNotebookChecked(int notebook, bool on)
{
    const QSignalBlocker blocker(list_);
    for (int i = 0; i < pages_.size(); ++i) {
        if (pages_.at(i).notebook != notebook) continue;
        QListWidgetItem* it = list_->item(pageRows_.at(i));
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    updating_ = true;
    updateNotebookStates();
    updating_ = false;
    updateSummary();
}

void PagePickerDialog::toggleNotebookCollapsed(int notebook)
{
    const int h = notebookOfHeader_.indexOf(notebook);
    if (h < 0) return;
    QListWidgetItem* header = list_->item(headerRows_.at(h));
    const bool collapsed = !header->data(kCollapsedData).toBool();
    header->setData(kCollapsedData, collapsed);
    for (int i = 0; i < pages_.size(); ++i) {
        if (pages_.at(i).notebook != notebook) continue;
        list_->item(pageRows_.at(i))->setHidden(collapsed);
    }
    list_->viewport()->update();
}

void PagePickerDialog::updateNotebookStates()
{
    for (int h = 0; h < headerRows_.size(); ++h) {
        const int nb = notebookOfHeader_.at(h);
        int total = 0, on = 0;
        for (int i = 0; i < pages_.size(); ++i) {
            if (pages_.at(i).notebook != nb) continue;
            ++total;
            if (list_->item(pageRows_.at(i))->checkState() == Qt::Checked) ++on;
        }
        const Qt::CheckState state = (on == 0) ? Qt::Unchecked
                                    : (on == total) ? Qt::Checked : Qt::PartiallyChecked;
        list_->item(headerRows_.at(h))->setCheckState(state);
    }
}

void PagePickerDialog::setAllChecked(bool on)
{
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        if (it->data(kPageIndexData).isValid() || it->data(kNotebookData).isValid())
            it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    updateSummary();
}

void PagePickerDialog::invertSelection()
{
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        if (!it->data(kPageIndexData).isValid()) continue;
        it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    }
    updateSummary();
}

void PagePickerDialog::updateSummary()
{
    if (updating_) return;
    updating_ = true;
    updateNotebookStates();
    updating_ = false;
    const int n = checkedCount();
    summary_->setText(tr("%1 de %2").arg(n).arg(pages_.size()));
    if (confirm_) confirm_->setEnabled(n > 0);
}

}
