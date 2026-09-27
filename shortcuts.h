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
};

QKeySequence defaultShortcut(ActionId id);
void applyShortcut(QAction* action, ActionId id);

} // namespace notes