#include "app/mainwindow.h"
#include "ui/anim.h"
#include "ui/sizenumberfield.h"
#include "ui/sizeslider.h"
#include "canvas/canvas.h"
#include "io/library.h"
#include "app/shortcuts.h"
#include "ui/pagepicker.h"
#include "ui/sidepanel.h"
#include "io/storage.h"
#include "io/pdfexport.h"
#include "theme.h"
#include "ui/toolbutton.h"
#include <QAbstractAnimation>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>

#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <algorithm>
#include <vector>

namespace notes {

namespace {

constexpr int kNavRowHeight = 26;
constexpr int kMinToolSize = 1;
constexpr int kMaxToolSize = 48;

bool toolUsesSlider(const QString& toolId)
{
    return toolId == QLatin1String("pencil")
        || toolId == QLatin1String("highlighter")
        || toolId == QLatin1String("eraser");
}

bool toolUsesSizeChoice(const QString& toolId)
{
    return toolId == QLatin1String("text") || toolId == QLatin1String("shape");
}

struct SizeChoice
{
    int minimum = 1;
    int maximum = 48;
    int fallback = 1;
};

SizeChoice sizeChoiceForTool(const QString& toolId)
{
    if (toolId == QLatin1String("text"))
        return {6, 96, 12};
    if (toolId == QLatin1String("shape"))
        return {1, 24, 1};
    return {};
}

QLabel* makeNavLabel(QWidget* parent, QHBoxLayout* layout, const QString& text)
{
    auto* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setFixedHeight(kNavRowHeight);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(label, 0, Qt::AlignVCenter);
    return label;
}

}

NotesWindow::NotesWindow()
{
    canvas_ = new Canvas(this);
    setCentralWidget(canvas_);

    library_ = new Library(this);
    library_->load();
    if (library_->notebookCount() == 0) {
        const int nb = library_->addNotebook(QStringLiteral("NoteBook 1"));
        library_->addPage(nb, QStringLiteral("Page 1"));
        library_->saveNow();
    }

    const QString savedTool = library_->savedTool();
    const QColor savedColor(library_->savedColor());
    const QString activeNotebookName = library_->activeNotebookName();

    toolSizes_ = {
        {QStringLiteral("pencil"), library_->savedSize(QStringLiteral("pencil"))},
        {QStringLiteral("highlighter"), library_->savedSize(QStringLiteral("highlighter"))},
        {QStringLiteral("eraser"), library_->savedSize(QStringLiteral("eraser"))},
        {QStringLiteral("text"), library_->savedSize(QStringLiteral("text"))},
        {QStringLiteral("shape"), library_->savedSize(QStringLiteral("shape"))},
    };

    sidePanel_ = new SidePanel(library_, this);
    connect(sidePanel_, &SidePanel::selectionChanged, this, &NotesWindow::syncFromPanel);

    fileMenu_ = new QMenu(QStringLiteral("Archivo"), this);
    QAction* newAct = fileMenu_->addAction("Nueva pagina");
    applyShortcut(newAct, ActionId::NewDoc);
    connect(newAct, &QAction::triggered, this,
        [this] { if (sidePanel_) sidePanel_->handleAddPage(); });
    QAction* openAct = fileMenu_->addAction("Importar pagina...");
    applyShortcut(openAct, ActionId::Open);
    connect(openAct, &QAction::triggered, this, &NotesWindow::importPage);
    fileMenu_->addSeparator();
    QAction* saveAct = fileMenu_->addAction("Guardar");
    applyShortcut(saveAct, ActionId::Save);
    connect(saveAct, &QAction::triggered, this, [this] {
        saveCurrentPage();
        if (library_) library_->saveNow();
        canvas_->markSaved();
        updateTitle();
    });
    QAction* saveAsAct = fileMenu_->addAction("Exportar pagina...");
    applyShortcut(saveAsAct, ActionId::SaveAs);
    connect(saveAsAct, &QAction::triggered, this, &NotesWindow::exportPage);
    QAction* exportPdfAct = fileMenu_->addAction("Exportar a PDF...");
    connect(exportPdfAct, &QAction::triggered, this, &NotesWindow::exportPdf);
    fileMenu_->addSeparator();
    QAction* insertImageAct = fileMenu_->addAction("Insertar imagen...");
    connect(insertImageAct, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Insertar imagen", QString(),
            "Imagenes (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;Todos los archivos (*)");
        if (path.isEmpty()) return;
        canvas_->insertImage(QImage(path));
    });

    editMenu_ = new QMenu(QStringLiteral("Editar"), this);
    QAction* undoAct = editMenu_->addAction("Deshacer");
    applyShortcut(undoAct, ActionId::Undo);
    connect(undoAct, &QAction::triggered, canvas_, &Canvas::undo);
    QAction* redoAct = editMenu_->addAction("Rehacer");
    applyShortcut(redoAct, ActionId::Redo);
    connect(redoAct, &QAction::triggered, canvas_, &Canvas::redo);
    editMenu_->addSeparator();
    QAction* copyAct = editMenu_->addAction("Copiar");
    applyShortcut(copyAct, ActionId::Copy);
    connect(copyAct, &QAction::triggered, canvas_, &Canvas::copy);
    QAction* cutAct = editMenu_->addAction("Cortar");
    applyShortcut(cutAct, ActionId::Cut);
    connect(cutAct, &QAction::triggered, canvas_, &Canvas::cut);
    QAction* pasteAct = editMenu_->addAction("Pegar");
    applyShortcut(pasteAct, ActionId::Paste);
    connect(pasteAct, &QAction::triggered, canvas_, &Canvas::paste);

    viewMenu_ = new QMenu(QStringLiteral("Ver"), this);
    panelAct_ = viewMenu_->addAction("Panel de archivos");
    panelAct_->setCheckable(true);
    panelAct_->setChecked(true);
    connect(panelAct_, &QAction::toggled, this,
        [this](bool on) { setPanelCollapsed(!on); });

    QActionGroup* tools = new QActionGroup(this);
    tools->setExclusive(true);

    QAction* pointerAct = tools->addAction(QIcon(":/assets/pointer.png"), "Puntero");
    pointerAct->setCheckable(true);
    pointerAct->setChecked(true);
    QAction* penAct = tools->addAction(QIcon(":/assets/pen.png"), "Lapiz");
    penAct->setCheckable(true);
    QAction* highlightAct = tools->addAction(QIcon(":/assets/highlighter.png"), "Resaltador");
    highlightAct->setCheckable(true);
    QAction* eraserAct = tools->addAction(QIcon(":/assets/eraser.png"), "Borrador");
    eraserAct->setCheckable(true);
    QAction* textAct = tools->addAction(QIcon(":/assets/text.png"), "Texto");
    textAct->setCheckable(true);
    QAction* shapeAct = tools->addAction(QIcon(":/assets/rectangle.png"), "Figura");
    shapeAct->setCheckable(true);

    const auto applyTool = [this, pointerAct, penAct, highlightAct, eraserAct, textAct, shapeAct](QAction* a) {
        Canvas::ToolId t = Canvas::ToolId::Select;
        QString id = QStringLiteral("pointer");
        if (a == pointerAct) {
            t = Canvas::ToolId::Select;
        } else if (a == penAct) {
            t = Canvas::ToolId::Pencil;
            id = QStringLiteral("pencil");
        } else if (a == highlightAct) {
            t = Canvas::ToolId::Highlighter;
            id = QStringLiteral("highlighter");
        } else if (a == eraserAct) {
            t = Canvas::ToolId::Eraser;
            id = QStringLiteral("eraser");
            if (eraserButton_)
                canvas_->setEraserMode(
                    eraserButton_->activeIcon().cacheKey() == eraserPartialIcon_.cacheKey()
                    ? Canvas::EraserMode::Partial : Canvas::EraserMode::Full);
        } else if (a == textAct) {
            t = Canvas::ToolId::Text;
            id = QStringLiteral("text");
        } else if (a == shapeAct) {
            t = Canvas::ToolId::Shape;
            id = QStringLiteral("shape");
            if (shapeButton_)
                canvas_->setShapeKind(shapeKindForIcon(shapeButton_->activeIcon()));
        }
        canvas_->setTool(t);
        currentToolId_ = id;
        syncToolOptions();
        if (library_) library_->setSavedTool(id);
    };
    connect(tools, &QActionGroup::triggered, this, applyTool);

    toolPanel_ = new QWidget(this);
    toolPanel_->setObjectName("toolPanel");
    toolPanel_->setStyleSheet(
        QStringLiteral("QWidget#toolPanel { background-color: %1; border: none; "
                       "border-radius: 18px; }"
                       "QToolButton { border: none; border-radius: 14px; }"
                       "QToolButton:hover { background-color: %2; }"
                       "QToolButton:checked { background-color: %3; }")
            .arg(theme::kPanel.name(),
                 theme::alphaCss(QColor(Qt::white), 10),
                 theme::alphaCss(QColor(Qt::white), 20)));
    QVBoxLayout* panelLayout = new QVBoxLayout(toolPanel_);
    panelLayout->setContentsMargins(6, 8, 6, 8);
    panelLayout->setSpacing(4);

    const auto makeToolButton = [this](QAction* act) {
        QToolButton* btn = new QToolButton(toolPanel_);
        btn->setDefaultAction(act);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setFixedSize(40, 40);
        btn->setIconSize(QSize(24, 24));
        return btn;
    };
    panelLayout->addWidget(makeToolButton(pointerAct));
    panelLayout->addWidget(makeToolButton(penAct));
    panelLayout->addWidget(makeToolButton(highlightAct));

    eraserPartialIcon_ = QIcon(":/assets/partial-eraser.png");
    eraserButton_ = new ToolButton(eraserAct, toolPanel_);
    eraserButton_->setVariationIcons({
        QIcon(":/assets/eraser.png"),
        eraserPartialIcon_,
    });
    connect(eraserButton_, &ToolButton::activeChanged, this,
        [this](const QIcon& active) {
            const bool partial = active.cacheKey() == eraserPartialIcon_.cacheKey();
            canvas_->setEraserMode(partial
                ? Canvas::EraserMode::Partial : Canvas::EraserMode::Full);
        });
    panelLayout->addWidget(eraserButton_);

    panelLayout->addWidget(makeToolButton(textAct));

    shapeRectIcon_ = QIcon(":/assets/rectangle.png");
    shapeTriangleIcon_ = QIcon(":/assets/triangle.png");
    shapeEllipseIcon_ = QIcon(":/assets/circle.png");
    shapeButton_ = new ToolButton(shapeAct, toolPanel_);
    shapeButton_->setVariationIcons({
        shapeRectIcon_,
        shapeTriangleIcon_,
        shapeEllipseIcon_,
    });
    connect(shapeButton_, &ToolButton::activeChanged, this,
        [this](const QIcon& active) {
            canvas_->setShapeKind(shapeKindForIcon(active));
        });
    panelLayout->addWidget(shapeButton_);

    QAction* initialAction = pointerAct;
    if (savedTool == QStringLiteral("pencil")) initialAction = penAct;
    else if (savedTool == QStringLiteral("highlighter")) initialAction = highlightAct;
    else if (savedTool == QStringLiteral("eraser")) initialAction = eraserAct;
    else if (savedTool == QStringLiteral("text")) initialAction = textAct;
    else if (savedTool == QStringLiteral("shape")) initialAction = shapeAct;
    initialAction->setChecked(true);
    applyTool(initialAction);

    QFrame* separator = new QFrame(toolPanel_);
    separator->setObjectName("colorSeparator");
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setStyleSheet(
        "QFrame#colorSeparator { color: rgba(255,255,255,70); border: none; "
        "max-height: 1px; min-height: 1px; background-color: rgba(255,255,255,70); }");
    panelLayout->addSpacing(6);
    panelLayout->addWidget(separator);
    panelLayout->addSpacing(6);

    QVBoxLayout* colorLayout = new QVBoxLayout;
    colorLayout->setContentsMargins(0, 0, 0, 0);
    colorLayout->setSpacing(6);
    colorLayout->setAlignment(Qt::AlignHCenter);

    const QColor palette[] = {
        QColor(0x00, 0x00, 0x00),
        QColor(0xff, 0x00, 0x00),
        QColor(0xcc, 0xff, 0x00),
    };

    for (const QColor& c : palette) {
        QToolButton* sw = makeSwatch(c);
        colorLayout->addWidget(sw, 0, Qt::AlignHCenter);
        swatches_.push_back(sw);
    }

    panelLayout->addLayout(colorLayout);
    toolPanel_->adjustSize();
    for (const QColor& c : palette) {
        if (c == savedColor) {
            selectedColor_ = c;
            break;
        }
    }
    if (library_) library_->setSavedColor(selectedColor_.name());
    canvas_->setColor(selectedColor_);
    refreshSwatches();

    positionToolPanel();
    buildNavPanel();
    canvas_->installEventFilter(this);

    connect(sidePanel_, &SidePanel::collapseRequested, this,
        [this] { setPanelCollapsed(true); });
    positionSidePanel();

    collapseTab_ = new QToolButton(this);
    collapseTab_->setObjectName("collapseTab");
    const QPixmap fwd = QIcon(":/assets/forward.png").pixmap(QSize(16, 16));
    QTransform rotate;
    rotate.rotate(180);
    collapseTab_->setIcon(QIcon(fwd.transformed(rotate)));
    collapseTab_->setIconSize(QSize(16, 16));
    collapseTab_->setFixedSize(34, 34);
    collapseTab_->setFocusPolicy(Qt::NoFocus);
    collapseTab_->setToolTip("Mostrar panel");
    collapseTab_->setStyleSheet(
        QStringLiteral("QToolButton#collapseTab { background-color: %1; border: none; "
                       "border-radius: 8px; }"
                       "QToolButton#collapseTab:hover { background-color: %2; }")
            .arg(theme::kPanel.name(), theme::kPanelRaised.name()));
    collapseTab_->hide();
    connect(collapseTab_, &QToolButton::clicked, this,
        [this] { setPanelCollapsed(false); });
    positionCollapseTab();

    int initialNotebook = 0;
    if (!activeNotebookName.isEmpty()) {
        initialNotebook = -1;
        for (int i = 0; i < library_->notebookCount(); ++i) {
            if (library_->notebookName(i) == activeNotebookName) {
                initialNotebook = i;
                break;
            }
        }
        if (initialNotebook < 0) initialNotebook = 0;
    }
    if (library_->notebookCount() == 0) initialNotebook = -1;
    sidePanel_->selectNotebook(initialNotebook);
}

void NotesWindow::buildNavPanel()
{
    navPanel_ = new QWidget(this);
    navPanel_->setObjectName("navPanel");
    navPanel_->setStyleSheet(
        QStringLiteral("QWidget#navPanel { background-color: %1; border: none; "
                       "border-radius: 18px; }"
                       "QToolButton#navMenuButton { color: %2; background-color: transparent; "
                       "border: none; border-radius: 10px; padding: 6px 12px; font-size: 13px; }"
                       "QToolButton#navMenuButton:hover { background-color: %3; }"
                       "QToolButton#navMenuButton:pressed { background-color: %4; }"
                       "QToolButton#navMenuButton::menu-indicator { image: none; width: 0px; }"
                       "QFrame#navSeparator { background-color: %5; border: none; "
                       "min-width: 1px; max-width: 1px; }"
                       "QLabel { color: %2; background: transparent; font-size: 13px; }"
                       "QCheckBox { color: %2; background: transparent; font-size: 13px; spacing: 8px; }"
                       "QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid %2; "
                       "border-radius: 4px; background-color: transparent; }"
                       "QCheckBox::indicator:hover { border-color: #ffffff; }"
                       "QCheckBox::indicator:checked { background-color: %2; "
                       "border: 1px solid #ffffff; }"
                       "QSpinBox { color: %2; background-color: transparent; "
                       "border: none; border-radius: 8px; padding: 3px 8px; font-size: 13px; }"
                       "QSpinBox:hover { background-color: %3; }"
                       "QSpinBox:focus { background-color: %4; }"
                       "QSpinBox::up-button, QSpinBox::down-button { width: 0px; border: none; }"
                       "QSpinBox::up-arrow, QSpinBox::down-arrow { width: 0px; height: 0px; image: none; }")
            .arg(theme::kPanel.name(),
                 theme::kTextPrimary.name(),
                 theme::alphaCss(QColor(Qt::white), 10),
                 theme::alphaCss(QColor(Qt::white), 18),
                 theme::alphaCss(QColor(Qt::white), 70)));

    const QString menuStyle = QStringLiteral(
        "QMenu { background-color: %1; border: none; padding: 6px; }"
        "QMenu::item { color: %2; background-color: transparent; "
        "padding: 6px 24px 6px 18px; border-radius: 6px; }"
        "QMenu::item:selected { background-color: %3; }"
        "QMenu::item:disabled { color: %4; }"
        "QMenu::separator { height: 1px; background-color: %5; margin: 5px 10px; }"
        "QMenu::indicator { width: 12px; height: 12px; }")
        .arg(theme::kPanel.name(),
             theme::kTextPrimary.name(),
             theme::alphaCss(QColor(Qt::white), 18),
             theme::alphaCss(theme::kTextPrimary, 120),
             theme::kOutline.name());

    QHBoxLayout* navLayout = new QHBoxLayout(navPanel_);
    navLayout->setContentsMargins(12, 8, 12, 8);
    navLayout->setSpacing(4);

    const std::vector<QMenu*> menus = {fileMenu_, editMenu_, viewMenu_};
    for (QMenu* menu : menus) {
        menu->setStyleSheet(menuStyle);
        QToolButton* btn = new QToolButton(navPanel_);
        btn->setObjectName("navMenuButton");
        btn->setText(menu->title());
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        btn->setMenu(menu);
        navLayout->addWidget(btn);
    }

    navLayout->addSpacing(8);
    QFrame* navSeparator = new QFrame(navPanel_);
    navSeparator->setObjectName("navSeparator");
    navSeparator->setFrameShape(QFrame::VLine);
    navSeparator->setFrameShadow(QFrame::Plain);
    navSeparator->setFixedHeight(kNavRowHeight - 8);
    navSeparator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    navLayout->addWidget(navSeparator, 0, Qt::AlignVCenter);
    navLayout->addSpacing(8);

    toolOptions_ = new QWidget(navPanel_);
    QHBoxLayout* toolsLayout = new QHBoxLayout(toolOptions_);
    toolsLayout->setContentsMargins(0, 0, 0, 0);
    toolsLayout->setSpacing(4);

    makeNavLabel(toolOptions_, toolsLayout, QStringLiteral("Size"));

    sizeSlider_ = new SizeSlider(toolOptions_);
    sizeSlider_->setObjectName("sizeSlider");
    sizeSlider_->setToolTip("Tamano del trazo");
    sizeSlider_->setMinimum(kMinToolSize);
    sizeSlider_->setMaximum(kMaxToolSize);
    sizeSlider_->setSingleStep(1);
    sizeSlider_->setPageStep(4);
    sizeSlider_->setValue(toolSizes_.value(currentToolId_, kMinToolSize));
    sizeSlider_->setFixedSize(150, kNavRowHeight);
    sizeSlider_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    toolsLayout->addWidget(sizeSlider_, 0, Qt::AlignVCenter);
    connect(sizeSlider_, &QSlider::valueChanged, this, &NotesWindow::onSizeSliderChanged);

    sizeNumber_ = new SizeNumberField(toolOptions_);
    sizeNumber_->setObjectName("sizeNumber");
    sizeNumber_->setBounds(kMinToolSize, kMaxToolSize);
    sizeNumber_->setFixedHeight(kNavRowHeight);
    sizeNumber_->setFixedWidth(50);
    toolsLayout->addWidget(sizeNumber_, 0, Qt::AlignVCenter);
    connect(sizeNumber_, &SizeNumberField::valueChanged, this, &NotesWindow::onSizeNumberChanged);

    toolsLayout->addSpacing(10);

    makeNavLabel(toolOptions_, toolsLayout, QStringLiteral("Pressure"));

    pressureBox_ = new QCheckBox(toolOptions_);
    pressureBox_->setObjectName("pressureBox");
    pressureBox_->setToolTip("Grosor segun la presion del lapiz");
    pressureBox_->setFocusPolicy(Qt::NoFocus);
    pressureBox_->setCursor(Qt::PointingHandCursor);
    pressureBox_->setChecked(library_->savedPressure());
    pressureBox_->setFixedHeight(kNavRowHeight);
    pressureBox_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    toolsLayout->addWidget(pressureBox_, 0, Qt::AlignVCenter);
    connect(pressureBox_, &QCheckBox::toggled, this, [this](bool on) {
        canvas_->setPressureEnabled(on);
        if (library_) library_->setSavedPressure(on);
    });

    navLayout->addWidget(toolOptions_);

    sizeChoiceOptions_ = new QWidget(navPanel_);
    QHBoxLayout* choiceLayout = new QHBoxLayout(sizeChoiceOptions_);
    choiceLayout->setContentsMargins(0, 0, 0, 0);
    choiceLayout->setSpacing(4);

    makeNavLabel(sizeChoiceOptions_, choiceLayout, QStringLiteral("Size"));

    sizeChoice_ = new SizeNumberField(sizeChoiceOptions_);
    sizeChoice_->setObjectName("sizeChoice");
    sizeChoice_->setBounds(1, 48);
    choiceLayout->addWidget(sizeChoice_, 0, Qt::AlignVCenter);
    connect(sizeChoice_, &SizeNumberField::valueChanged, this, &NotesWindow::onSizeChoiceChanged);

    navLayout->addWidget(sizeChoiceOptions_);
    navLayout->addStretch(1);

    syncToolOptions();
    navPanel_->adjustSize();
    positionNavPanel();
}

void NotesWindow::syncToolOptions()
{
    const bool sliderVisible = toolUsesSlider(currentToolId_);
    const bool choiceVisible = !sliderVisible && toolUsesSizeChoice(currentToolId_);
    const auto setGroupVisible = [this](QWidget* group, bool visible) {
        if (!group || group->isHidden() == !visible) return;
        group->setVisible(visible);
        if (navPanel_) {
            navPanel_->adjustSize();
            positionNavPanel();
        }
    };
    setGroupVisible(toolOptions_, sliderVisible);
    setGroupVisible(sizeChoiceOptions_, choiceVisible);

    if (sliderVisible) {
        if (!sizeSlider_ || !pressureBox_) return;
        const int v = qBound(kMinToolSize, toolSizes_.value(currentToolId_, kMinToolSize), kMaxToolSize);
        {
            const QSignalBlocker blocker(sizeSlider_);
            sizeSlider_->setValue(v);
        }
        {
            const QSignalBlocker blocker(sizeNumber_);
            sizeNumber_->setValue(v);
        }
        {
            const QSignalBlocker blocker(pressureBox_);
            pressureBox_->setChecked(library_ ? library_->savedPressure() : false);
        }
        currentSize_ = v;
        applyToolSizeToCanvas();
        canvas_->setPressureEnabled(pressureBox_->isChecked());
        return;
    }

    if (!choiceVisible || !sizeChoice_) return;
    const SizeChoice choice = sizeChoiceForTool(currentToolId_);
    if (choiceToolId_ != currentToolId_) {
        choiceToolId_ = currentToolId_;
        sizeChoice_->setBounds(choice.minimum, choice.maximum);
    }
    currentSize_ = qBound(choice.minimum, toolSizes_.value(currentToolId_, choice.fallback), choice.maximum);
    {
        const QSignalBlocker blocker(sizeChoice_);
        sizeChoice_->setValue(currentSize_);
    }
    applyToolSizeToCanvas();
}

void NotesWindow::commitToolSize(int v)
{
    currentSize_ = v;
    toolSizes_[currentToolId_] = v;
    if (library_) library_->setSavedSize(currentToolId_, v);
    applyToolSizeToCanvas();
}

void NotesWindow::onSizeSliderChanged(int v)
{
    if (!toolUsesSlider(currentToolId_)) return;
    commitToolSize(v);
    const QSignalBlocker blocker(sizeNumber_);
    sizeNumber_->setValue(v);
}

void NotesWindow::onSizeNumberChanged(int v)
{
    if (!toolUsesSlider(currentToolId_)) return;
    commitToolSize(v);
    const QSignalBlocker blocker(sizeSlider_);
    sizeSlider_->setValue(v);
}

void NotesWindow::onSizeChoiceChanged(int v)
{
    if (!toolUsesSizeChoice(currentToolId_)) return;
    commitToolSize(v);
}


void NotesWindow::applyToolSizeToCanvas()
{
    if (!canvas_) return;
    const float v = float(currentSize_);
    if (currentToolId_ == QLatin1String("eraser"))
        canvas_->setEraserRadius(v * 0.5f);
    else if (currentToolId_ == QLatin1String("text"))
        canvas_->setFontSize(v);
    else if (currentToolId_ == QLatin1String("shape"))
        canvas_->setShapePenWidth(v);
    else
        canvas_->setStrokeSize(v);
}

void NotesWindow::setPanelCollapsed(bool collapsed)
{
    if (panelCollapsed_ == collapsed) {
        panelAct_->setChecked(!collapsed);
        return;
    }
    panelCollapsed_ = collapsed;
    panelAct_->setChecked(!collapsed);

    const bool anims = notes::anim::enabled();

    if (sidePanel_) {
        if (panelAnim_) {
            panelAnim_->stop();
            panelAnim_->deleteLater();
            panelAnim_ = nullptr;
        }
        const int w = sidePanel_->width();
        const int y = sidePanel_->pos().y();
        const QPoint resting(width() - w - kSidePanelMargin, y);
        const QPoint off(width() + 4, y);
        const QPoint start = sidePanel_->pos();
        const QPoint target = collapsed ? off : resting;
        sidePanel_->show();
        sidePanel_->raise();
        if (!anims) {
            sidePanel_->move(target);
            sidePanel_->setVisible(!collapsed);
        } else {
            panelAnim_ = new QPropertyAnimation(sidePanel_, "pos", this);
            panelAnim_->setDuration(220);
            panelAnim_->setStartValue(start);
            panelAnim_->setEndValue(target);
            panelAnim_->setEasingCurve(collapsed ? QEasingCurve::InCubic
                                                 : QEasingCurve::OutCubic);
            connect(panelAnim_, &QPropertyAnimation::finished, this, [this] {
                if (panelCollapsed_) sidePanel_->hide();
            });
            panelAnim_->start();
        }
    }

    if (collapseTab_) {
        if (tabFade_) {
            tabFade_->stop();
            tabFade_->deleteLater();
            tabFade_ = nullptr;
        }
        if (collapseTab_->graphicsEffect())
            collapseTab_->graphicsEffect()->deleteLater();
        auto* eff = new QGraphicsOpacityEffect(collapseTab_);
        collapseTab_->setGraphicsEffect(eff);
        collapseTab_->show();
        collapseTab_->raise();
        if (!anims) {
            eff->setOpacity(1.0);
            collapseTab_->setVisible(collapsed);
        } else {
            eff->setOpacity(collapsed ? 0.0 : 1.0);
            tabFade_ = new QPropertyAnimation(eff, "opacity", this);
            tabFade_->setDuration(160);
            tabFade_->setStartValue(collapsed ? 0.0 : 1.0);
            tabFade_->setEndValue(collapsed ? 1.0 : 0.0);
            tabFade_->setEasingCurve(QEasingCurve::OutCubic);
            connect(tabFade_, &QPropertyAnimation::finished, this, [this, collapsed] {
                collapseTab_->setVisible(collapsed);
            });
            tabFade_->start();
        }
    }
}

ShapeKind NotesWindow::shapeKindForIcon(const QIcon& icon) const
{
    const qint64 k = icon.cacheKey();
    if (k == shapeTriangleIcon_.cacheKey()) return ShapeKind::Triangle;
    if (k == shapeEllipseIcon_.cacheKey()) return ShapeKind::Ellipse;
    return ShapeKind::Rectangle;
}

void NotesWindow::syncFromPanel()
{
    if (!sidePanel_ || !library_ || !canvas_) return;
    const int nb = sidePanel_->selectedNotebook();
    const int pid = sidePanel_->selectedPageId();
    const Page* page = (nb >= 0 && pid >= 0) ? library_->pageById(nb, pid) : nullptr;


    canvas_->setFocus(Qt::OtherFocusReason);

    if (nb == currentNb_ && pid == currentPageId_) {
        if (canvas_->isDirty()) {
            saveCurrentPage();
            updateTitle();
        }
        canvas_->setEnabled(page != nullptr);
        canvas_->setVisible(page != nullptr);
        return;
    }

    saveCurrentPage();
    currentNb_ = nb;
    currentPageId_ = pid;

    if (page) {
        if (page->doc.isEmpty()) {
            canvas_->newDocument();
        } else if (!canvas_->loadState(page->doc)) {
            canvas_->newDocument();
        }
        canvas_->setEnabled(true);
        canvas_->show();
    } else {
        canvas_->newDocument();
        canvas_->setEnabled(false);
        canvas_->hide();
    }
    canvas_->markSaved();
    updateTitle();
}

void NotesWindow::saveCurrentPage()
{
    if (!library_ || !canvas_ || currentNb_ < 0 || currentPageId_ < 0) return;
    if (library_->pageById(currentNb_, currentPageId_))
        library_->setPageDocumentById(currentNb_, currentPageId_, canvas_->serializeState());
}

bool NotesWindow::importPage()
{
    const QString path = QFileDialog::getOpenFileName(this, "Importar pagina", QString(),
        "Notas (*.notes);;Todos (*)");
    if (path.isEmpty()) return false;
    QFile f(path);
    if (!f.open(QIODeviceBase::ReadOnly)) {
        QMessageBox::warning(this, "Notas", "No se pudo abrir el archivo.");
        return false;
    }
    const QByteArray bytes = f.readAll();
    DocumentData tmp;
    if (!deserializeDocument(bytes, tmp)) {
        QMessageBox::warning(this, "Notas", "El archivo no es una pagina valida.");
        return false;
    }

    if (library_->notebookCount() == 0 && sidePanel_)
        sidePanel_->handleAddNotebook();
    int nb = sidePanel_ ? sidePanel_->selectedNotebook() : -1;
    if (nb < 0 || nb >= library_->notebookCount())
        nb = library_->notebookCount() - 1;

    QString base = QFileInfo(path).completeBaseName();
    QString name = base;
    for (int n = 2; library_->pageNameExists(nb, name); ++n)
        name = QString("%1 %2").arg(base).arg(n);

    const int id = library_->addPage(nb, name);
    if (id < 0) return false;
    library_->setPageDocumentById(nb, id, bytes);
    if (sidePanel_) sidePanel_->activatePage(nb, id);
    updateTitle();
    return true;
}

bool NotesWindow::exportPage()
{
    const QString path = QFileDialog::getSaveFileName(this, "Exportar pagina", QString(),
        "Notas (*.notes);;Todos (*)");
    if (path.isEmpty()) return false;
    saveCurrentPage();
    const QByteArray bytes = canvas_->serializeState();
    QFile f(path);
    if (!f.open(QIODeviceBase::WriteOnly | QIODeviceBase::Truncate)) {
        QMessageBox::warning(this, "Notas", "No se pudo escribir el archivo.");
        return false;
    }
    const qint64 written = f.write(bytes);
    f.close();
    if (written != bytes.size()) {
        QMessageBox::warning(this, "Notas", "No se pudo escribir el archivo.");
        return false;
    }
    return true;
}

bool NotesWindow::exportPdf()
{
    if (!canvas_ || !library_) return false;

    QVector<PageRef> pages;
    for (int nb = 0; nb < library_->notebookCount(); ++nb) {
        const int count = library_->pageCount(nb);
        for (int pos = 0; pos < count; ++pos) {
            const Page* p = library_->pageAt(nb, pos);
            if (!p) continue;
            PageRef ref;
            ref.notebook = nb;
            ref.pageId = p->id;
            ref.notebookName = library_->notebookName(nb);
            ref.name = p->name.isEmpty() ? tr("Pagina %1").arg(pos + 1) : p->name;
            ref.isCurrent = (nb == currentNb_ && p->id == currentPageId_);
            pages.push_back(ref);
        }
    }
    if (pages.isEmpty()) {
        QMessageBox::warning(this, "Notas", "No hay paginas que exportar.");
        return false;
    }

    PagePickerDialog picker(pages, this);
    if (picker.exec() != QDialog::Accepted) return false;
    const QVector<int> rows = picker.selectedRows();
    if (rows.isEmpty()) return false;

    QString path = QFileDialog::getSaveFileName(this, "Exportar a PDF", QString(),
        "PDF (*.pdf)");
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        path += QStringLiteral(".pdf");

    saveCurrentPage();

    QVector<PdfPage> out;
    int skipped = 0;
    for (int row : rows) {
        const PageRef& ref = pages.at(row);
        DocumentData data;
        const bool ok = ref.isCurrent
            ? deserializeDocument(canvas_->serializeState(), data)
            : deserializeDocument(library_->pageDocumentById(ref.notebook, ref.pageId), data);
        if (!ok) {
            ++skipped;
            continue;
        }
        out.push_back(PdfPage{ref.name, data});
    }

    if (out.isEmpty()) {
        QMessageBox::warning(this, "Notas", "No se pudo preparar ninguna pagina.");
        return false;
    }

    QString error;
    if (!notes::exportPdf(path, out, &error)) {
        QMessageBox::warning(this, "Notas",
            error.isEmpty() ? QStringLiteral("No se pudo escribir el PDF.") : error);
        return false;
    }
    if (skipped > 0) {
        QMessageBox::information(this, "Notas",
            tr("%1 de %2 paginas no se pudieron leer y se omitieron.")
                .arg(skipped).arg(rows.size()));
    }
    return true;
}

void NotesWindow::updateTitle()
{
    QString name = "Sin titulo";
    if (library_ && currentNb_ >= 0 && currentPageId_ >= 0) {
        if (const Page* p = library_->pageById(currentNb_, currentPageId_))
            name = library_->notebookName(currentNb_) + " / " + p->name;
    }
    setWindowTitle(QStringLiteral("%1%2 - Notas")
        .arg(canvas_->isDirty() ? "* " : QString(), name));
}

bool NotesWindow::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == canvas_ && e->type() == QEvent::Resize) {
        positionToolPanel();
        positionNavPanel();
        positionSidePanel();
        positionCollapseTab();
    }
    return QMainWindow::eventFilter(obj, e);
}

void NotesWindow::resizeEvent(QResizeEvent* e)
{
    positionSidePanel();
    positionCollapseTab();
    QMainWindow::resizeEvent(e);
    positionToolPanel();
    positionNavPanel();
}

void NotesWindow::closeEvent(QCloseEvent* e)
{
    saveCurrentPage();
    if (library_) library_->saveNow();
    e->accept();
}

void NotesWindow::positionToolPanel()
{
    if (!toolPanel_ || !canvas_) return;
    const QPoint c0 = canvas_->mapTo(this, QPoint(0, 0));
    const int px = c0.x() + 16;
    const int py = c0.y() + qMax(0, (canvas_->height() - toolPanel_->height()) / 2);
    toolPanel_->move(px, py);
    toolPanel_->raise();
}

void NotesWindow::positionNavPanel()
{
    if (!navPanel_ || !canvas_) return;
    constexpr int kMargin = 12;
    const QPoint c0 = canvas_->mapTo(this, QPoint(0, 0));
    const int w = qMax(navPanel_->sizeHint().width(), width() - 2 * kMargin);
    navPanel_->setGeometry(c0.x() + kMargin, c0.y() + kMargin, w, navPanel_->height());
    navPanel_->raise();
}

void NotesWindow::positionSidePanel()
{
    if (!sidePanel_) return;
    if (panelAnim_ && panelAnim_->state() != QAbstractAnimation::Stopped)
        return;
    constexpr int kMargin = kSidePanelMargin;
    const QPoint c0 = canvas_->mapTo(this, QPoint(0, 0));
    const int top = c0.y() + kMargin + navBarBottomOffset();
    const int bottom = c0.y() + canvas_->height() - kMargin;
    const int w = sidePanel_->width();
    const int x = panelCollapsed_ ? width() + 4 : width() - w - kMargin;
    sidePanel_->move(x, top);
    sidePanel_->resize(w, qMax(0, bottom - top));
    sidePanel_->raise();
}

void NotesWindow::positionCollapseTab()
{
    if (!collapseTab_) return;
    constexpr int kMargin = 14;
    const QPoint c0 = canvas_->mapTo(this, QPoint(0, 0));
    const int cx = width() - kMargin - collapseTab_->width();
    const int cy = c0.y() + kMargin + navBarBottomOffset();
    collapseTab_->move(cx, cy);
    collapseTab_->raise();
}

int NotesWindow::navBarBottomOffset() const
{
    if (!navPanel_ || !canvas_) return 0;
    const int bottom = navPanel_->mapTo(this, QPoint(0, navPanel_->height())).y();
    return qMax(0, bottom - canvas_->mapTo(this, QPoint(0, 0)).y());
}

QToolButton* NotesWindow::makeSwatch(const QColor& c)
{
    QPixmap pm(26, 26);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(0, 0, 26, 26, 6, 6);
    p.end();

    QToolButton* btn = new QToolButton(toolPanel_);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCheckable(true);
    btn->setFixedSize(34, 34);
    btn->setIconSize(QSize(26, 26));
    btn->setIcon(QIcon(pm));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("swatchColor", c);
    btn->setStyleSheet(
        "QToolButton { border: none; border-radius: 8px; }"
        "QToolButton:hover { background-color: transparent; }"
        "QToolButton:checked { background-color: rgba(255,255,255,20%); border-radius: 8px; }");
    connect(btn, &QToolButton::clicked, this, [this, c] {
        selectedColor_ = c;
        canvas_->setColor(c);
        if (library_) library_->setSavedColor(c.name());
        refreshSwatches();
    });
    return btn;
}

void NotesWindow::refreshSwatches()
{
    for (QToolButton* b : swatches_) {
        const QColor c = b->property("swatchColor").value<QColor>();
        b->setChecked(c == selectedColor_);
    }
}

} // namespace notes
