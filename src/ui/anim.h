#pragma once
#include <QGuiApplication>
#include <QStyleHints>

namespace notes::anim {

inline bool enabled()
{
    return QGuiApplication::styleHints()->useHoverEffects();
}

} // namespace notes::anim
