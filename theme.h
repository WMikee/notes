#pragma once
#include <QColor>
#include <QString>

namespace notes {
namespace theme {

inline const QColor kCanvasBackground{0x1c, 0x1e, 0x1c};
inline const QColor kPanel{0x24, 0x27, 0x25};
inline const QColor kPanelRaised{0x2f, 0x33, 0x30};
inline const QColor kOutline{0x39, 0x3e, 0x3a};
inline const QColor kGroove{0x34, 0x39, 0x35};
inline const QColor kGrooveFill{0xe6, 0xec, 0xe4};
inline const QColor kTextPrimary{0xe3, 0xe8, 0xe1};
inline const QColor kTextDim{0xb6, 0xbf, 0xb5};

inline QString alphaCss(const QColor& c, int alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

}
}
