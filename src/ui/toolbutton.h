#pragma once
#include <QIcon>
#include <QList>
#include <QToolButton>
#include <QWidget>

class QAction;
class QGraphicsOpacityEffect;
class QLayoutItem;
class QPaintEvent;
class QPropertyAnimation;
class QResizeEvent;
class QVBoxLayout;

namespace notes {

class DotIndicator : public QToolButton
{
    Q_OBJECT
public:
    explicit DotIndicator(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
};

class ToolPopup : public QWidget
{
    Q_OBJECT
public:
    explicit ToolPopup(QWidget* parent = nullptr);

    void popupFor(const QList<QIcon>& icons, int activeIndex, const QPoint& anchor);

signals:
    void variationChosen(int index);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void rebuildButtons();

    QVBoxLayout* layout_ = nullptr;
    QList<QIcon> icons_;
    int activeIndex_ = -1;
    QGraphicsOpacityEffect* popupEffect_ = nullptr;
    QPropertyAnimation* popupFade_ = nullptr;
};

class ToolButton : public QWidget
{
    Q_OBJECT
public:
    explicit ToolButton(QAction* action, QWidget* parent = nullptr);

    void setVariationIcons(const QList<QIcon>& icons);
    void addVariationIcon(const QIcon& icon);
    int activeIndex() const { return activeIndex_; }
    QIcon activeIcon() const { return variations_.value(activeIndex_); }
    bool hasVariations() const { return variations_.size() > 1; }
    QToolButton* button() const { return mainButton_; }

signals:
    void activeChanged(const QIcon& active);

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    void togglePopup();
    void applySwap(int chosenIndex);
    void updateButtonIcon();

    QAction* action_ = nullptr;
    QToolButton* mainButton_ = nullptr;
    DotIndicator* dot_ = nullptr;
    ToolPopup* popup_ = nullptr;
    QList<QIcon> variations_;
    int activeIndex_ = 0;
};

} // namespace notes
