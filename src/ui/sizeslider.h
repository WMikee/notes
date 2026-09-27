#pragma once
#include <QSlider>

class QPaintEvent;
class QMouseEvent;

namespace notes {


class SizeSlider : public QSlider
{
    Q_OBJECT
public:
    explicit SizeSlider(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;

private:
    int travel() const;
    double fraction() const;
    int handleX() const;
    int valueAtX(int x) const;
};

}
