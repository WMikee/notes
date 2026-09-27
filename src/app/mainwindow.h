#pragma once
#include <QColor>
#include <QHash>
#include <QIcon>
#include <QMainWindow>
#include <QString>
#include <vector>
#include "scene/shape.h"

class QAction;
class QActionGroup;
class QCheckBox;
class QCloseEvent;
class QComboBox;

namespace notes {
class SizeSlider;
class SizeNumberField;
}
class QEvent;
class QMenu;
class QPropertyAnimation;
class QResizeEvent;
class QSlider;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace notes {

class Canvas;
class Library;
class SidePanel;
class ToolButton;

class NotesWindow : public QMainWindow
{
    Q_OBJECT
public:
    NotesWindow();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private:
    static constexpr int kSidePanelMargin = 14;

    ShapeKind shapeKindForIcon(const QIcon& icon) const;
    QToolButton* makeSwatch(const QColor& c);
    void refreshSwatches();
    void saveCurrentPage();
    void syncFromPanel();
    bool importPage();
    bool exportPage();
    bool exportPdf();
    void updateTitle();
    void buildNavPanel();
    void syncToolOptions();
    void applyToolSizeToCanvas();
    void commitToolSize(int v);
    void onSizeSliderChanged(int v);
    void onSizeNumberChanged(int v);
    void onSizeChoiceChanged(int v);
    void positionToolPanel();
    void positionNavPanel();
    void positionSidePanel();
    void positionCollapseTab();
    int navBarBottomOffset() const;
    void setPanelCollapsed(bool collapsed);

    Canvas* canvas_ = nullptr;
    QWidget* toolPanel_ = nullptr;
    QWidget* navPanel_ = nullptr;
    QMenu* fileMenu_ = nullptr;
    QMenu* editMenu_ = nullptr;
    QMenu* viewMenu_ = nullptr;
    notes::SizeSlider* sizeSlider_ = nullptr;
    notes::SizeNumberField* sizeNumber_ = nullptr;
    QCheckBox* pressureBox_ = nullptr;
    notes::SizeNumberField* sizeChoice_ = nullptr;
    QWidget* toolOptions_ = nullptr;
    QWidget* sizeChoiceOptions_ = nullptr;
    QString currentToolId_ = QStringLiteral("pointer");
    QString choiceToolId_;
    int currentSize_ = 0;
    QHash<QString, int> toolSizes_;
    SidePanel* sidePanel_ = nullptr;
    Library* library_ = nullptr;
    ToolButton* eraserButton_ = nullptr;
    QIcon eraserPartialIcon_;
    ToolButton* shapeButton_ = nullptr;
    QIcon shapeRectIcon_;
    QIcon shapeTriangleIcon_;
    QIcon shapeEllipseIcon_;
    QToolButton* collapseTab_ = nullptr;
    QColor selectedColor_ = QColor(0x00, 0x00, 0x00);
    std::vector<QToolButton*> swatches_;
    QAction* panelAct_ = nullptr;
    int currentNb_ = -1;
    int currentPageId_ = -1;
    bool panelCollapsed_ = false;
    QPropertyAnimation* panelAnim_ = nullptr;
    QPropertyAnimation* tabFade_ = nullptr;
};

} // namespace notes
