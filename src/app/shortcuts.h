#pragma once
#include <QKeySequence>

class QAction;

namespace notes {

enum class ActionId
{
    NewDoc,
    Open,
    Save,
    SaveAs,
    Undo,
    Redo,
    Copy,
    Cut,
    Paste,
    Pointer,
    Pencil,
    Highlighter,
    Eraser,
    Text,
    Shape,
};

QKeySequence defaultShortcut(ActionId id);
void applyShortcut(QAction* action, ActionId id);

} // namespace notes
