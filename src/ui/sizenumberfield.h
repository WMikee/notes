#pragma once
#include <QSpinBox>

namespace notes {


class SizeNumberField : public QSpinBox
{
    Q_OBJECT
public:
    explicit SizeNumberField(QWidget* parent = nullptr);

    void setBounds(int minimum, int maximum);

    int value() const { return value_; }
    void setValue(int v);

signals:
    void valueChanged(int v);

private:
    void onSpinValueChanged(int v);

    int value_ = 1;
};

}
