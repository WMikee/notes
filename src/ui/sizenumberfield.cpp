#include "ui/sizenumberfield.h"
#include "theme.h"
#include <QPalette>

namespace notes {

SizeNumberField::SizeNumberField(QWidget* parent) : QSpinBox(parent)
{
    setObjectName("sizeNumber");
    setToolTip("Tamano (escribe un valor)");
    setKeyboardTracking(false);
    setAlignment(Qt::AlignLeft);


    setFocusPolicy(Qt::ClickFocus);
    setMinimumWidth(58);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    QPalette p = palette();
    p.setColor(QPalette::Window, theme::kPanel);
    p.setColor(QPalette::Base, theme::kPanel);
    p.setColor(QPalette::WindowText, theme::kTextPrimary);
    p.setColor(QPalette::Text, theme::kTextPrimary);
    p.setColor(QPalette::ButtonText, theme::kTextPrimary);
    setPalette(p);

    QSpinBox::setValue(value_);

    connect(this, &QSpinBox::valueChanged, this, &SizeNumberField::onSpinValueChanged);
}

void SizeNumberField::setBounds(int minimum, int maximum)
{
    const int lo = qMin(minimum, maximum);
    const int hi = qMax(minimum, maximum);
    const bool blocked = blockSignals(true);
    setRange(lo, hi);
    if (value_ < lo || value_ > hi) {
        value_ = qBound(lo, value_, hi);
        QSpinBox::setValue(value_);
    }
    blockSignals(blocked);
}

void SizeNumberField::setValue(int v)
{
    value_ = qBound(minimum(), v, maximum());
    const bool blocked = blockSignals(true);
    QSpinBox::setValue(value_);
    blockSignals(blocked);
}

void SizeNumberField::onSpinValueChanged(int v)
{
    if (v == value_) return;
    value_ = v;
    emit valueChanged(v);
}

}
