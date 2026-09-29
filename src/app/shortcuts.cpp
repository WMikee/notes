#include "app/shortcuts.h"
#include <QAction>

namespace notes {

QKeySequence defaultShortcut(ActionId id)
{
    switch (id) {
    case ActionId::NewDoc:  return QKeySequence::New;
    case ActionId::Open:    return QKeySequence::Open;
    case ActionId::Save:    return QKeySequence::Save;
    case ActionId::SaveAs:  return QKeySequence::SaveAs;
    case ActionId::Undo:    return QKeySequence::Undo;
    case ActionId::Redo:    return QKeySequence::Redo;
    case ActionId::Copy:    return QKeySequence::Copy;
    case ActionId::Cut:     return QKeySequence::Cut;
    case ActionId::Paste:   return QKeySequence::Paste;
    case ActionId::Pointer:     return QKeySequence(Qt::Key_V);
    case ActionId::Pencil:      return QKeySequence(Qt::Key_B);
    case ActionId::Highlighter: return QKeySequence(Qt::Key_H);
    case ActionId::Eraser:      return QKeySequence(Qt::Key_E);
    case ActionId::Text:        return QKeySequence(Qt::Key_T);
    case ActionId::Shape:       return QKeySequence(Qt::Key_F);
    }
    return {};
}

void applyShortcut(QAction* action, ActionId id)
{
    action->setShortcut(defaultShortcut(id));
}

} // namespace notes
