#include "ui/toolbutton.h"
#include "ui/anim.h"
#include "theme.h"
#include <QAction>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

namespace notes {

DotIndicator::DotIndicator(QWidget* parent)
    : QToolButton(parent)
{
    setFixedSize(16, 16);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QObject::tr("Variaciones"));
}

void DotIndicator::paintEvent(QPaintEvent* e)
{
    QToolButton::paintEvent(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xff, 0xff, 0xff));
    const QPointF c(rect().center().x() + 1.0, rect().center().y() + 1.0);
    p.drawEllipse(c, 3.0, 3.0);
}

ToolPopup::ToolPopup(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setObjectName("toolPopup");
    setStyleSheet(
        "QToolButton { border: none; border-radius: 8px; background: transparent; }"
        "QToolButton:hover { background-color: rgba(255,255,255,12%); }");

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(6, 6, 6, 6);
    layout_->setSpacing(4);

    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, this, [this] { hide(); });
}

void ToolPopup::popupFor(const QList<QIcon>& icons, int activeIndex, const QPoint& anchor)
{
    icons_ = icons;
    activeIndex_ = activeIndex;
    rebuildButtons();
    adjustSize();
    QPoint pos = anchor;
    if (QScreen* screen = QGuiApplication::screenAt(anchor)) {
        const QRect sg = screen->availableGeometry();
        pos.setX(qBound(sg.left(), pos.x(), sg.right() - width() + 1));
        pos.setY(qBound(sg.top(), pos.y(), sg.bottom() - height() + 1));
    }
    move(pos);
    show();
    raise();

    if (popupFade_) {
        popupFade_->stop();
        popupFade_->deleteLater();
        popupFade_ = nullptr;
    }
    if (popupEffect_) {
        popupEffect_->deleteLater();
        popupEffect_ = nullptr;
    }
    if (notes::anim::enabled()) {
        auto* eff = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(eff);
        eff->setOpacity(0.0);
        popupEffect_ = eff;
        popupFade_ = new QPropertyAnimation(eff, "opacity", this);
        popupFade_->setDuration(120);
        popupFade_->setStartValue(0.0);
        popupFade_->setEndValue(1.0);
        popupFade_->setEasingCurve(QEasingCurve::OutCubic);
        popupFade_->start();
    }
}

void ToolPopup::rebuildButtons()
{
    while (QLayoutItem* item = layout_->takeAt(0)) {
        if (QWidget* widget = item->widget())
            delete widget;
        delete item;
    }
    for (int i = 0; i < icons_.size(); ++i) {
        if (i == activeIndex_) continue;
        auto* btn = new QToolButton(this);
        btn->setIcon(icons_.at(i));
        btn->setIconSize(QSize(22, 22));
        btn->setFixedSize(36, 36);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QToolButton::clicked, this, [this, i] {
            hide();
            emit variationChosen(i);
        });
        layout_->addWidget(btn);
    }
}

void ToolPopup::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 12, 12);
    p.setPen(QPen(notes::theme::kOutline, 1));
    p.setBrush(notes::theme::kPanel);
    p.drawPath(path);
}

ToolButton::ToolButton(QAction* action, QWidget* parent)
    : QWidget(parent)
    , action_(action)
{
    setFixedSize(40, 40);

    mainButton_ = new QToolButton(this);
    mainButton_->setDefaultAction(action_);
    mainButton_->setFocusPolicy(Qt::NoFocus);
    mainButton_->setIconSize(QSize(24, 24));

    popup_ = new ToolPopup(this);
    connect(popup_, &ToolPopup::variationChosen, this, &ToolButton::applySwap);

    dot_ = new DotIndicator(this);
    connect(dot_, &QToolButton::clicked, this, &ToolButton::togglePopup);

    QList<QIcon> initial;
    if (action_) initial << action_->icon();
    setVariationIcons(initial);
}

void ToolButton::setVariationIcons(const QList<QIcon>& icons)
{
    variations_ = icons;
    if (variations_.isEmpty() && action_)
        variations_ << action_->icon();
    activeIndex_ = 0;
    updateButtonIcon();
}

void ToolButton::addVariationIcon(const QIcon& icon)
{
    for (const QIcon& existing : variations_)
        if (existing.cacheKey() == icon.cacheKey())
            return;
    variations_.append(icon);
    updateButtonIcon();
}

void ToolButton::togglePopup()
{
    if (popup_->isVisible()) {
        popup_->hide();
        return;
    }
    popup_->popupFor(variations_, activeIndex_,
        mainButton_->mapToGlobal(QPoint(mainButton_->width() + 8, 0)));
}

void ToolButton::applySwap(int chosenIndex)
{
    if (chosenIndex < 0 || chosenIndex >= variations_.size()) return;
    if (chosenIndex == activeIndex_) return;

    variations_.swapItemsAt(activeIndex_, chosenIndex);

    updateButtonIcon();
    emit activeChanged(variations_.at(activeIndex_));
}

void ToolButton::updateButtonIcon()
{
    if (variations_.isEmpty()) return;
    const QIcon icon = variations_.at(activeIndex_);
    mainButton_->setIcon(icon);
    if (action_) action_->setIcon(icon);
    dot_->setVisible(hasVariations());
}

void ToolButton::resizeEvent(QResizeEvent* e)
{
    mainButton_->setGeometry(rect());
    dot_->move(rect().right() - dot_->width(), rect().bottom() - dot_->height());
    QWidget::resizeEvent(e);
}

} // namespace notes
