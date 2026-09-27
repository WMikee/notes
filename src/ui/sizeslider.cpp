#include "ui/sizeslider.h"
#include "theme.h"
#include <QMouseEvent>
#include <QPainter>

namespace notes {

namespace {

constexpr int kHandle = 8;
constexpr int kGroove = 7;

}

SizeSlider::SizeSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

void SizeSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    const int grooveY = (height() - kGroove) / 2;
    const int hx = handleX();
    const qreal radius = kGroove / 2.0;
    p.setBrush(theme::kGroove);
    p.drawRoundedRect(QRectF(0, grooveY, width(), kGroove), radius, radius);
    p.setBrush(theme::kGrooveFill);
    p.drawRoundedRect(QRectF(0, grooveY, hx, kGroove), radius, radius);
    p.setBrush(QColor(0xff, 0xff, 0xff));
    p.drawEllipse(QRectF(hx - kHandle, height() / 2.0 - kHandle,
                         2 * kHandle, 2 * kHandle));
}

void SizeSlider::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        setValue(valueAtX(int(e->position().x())));
        e->accept();
        return;
    }
    QSlider::mousePressEvent(e);
}

void SizeSlider::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton) {
        setValue(valueAtX(int(e->position().x())));
        e->accept();
        return;
    }
    QSlider::mouseMoveEvent(e);
}

int SizeSlider::travel() const
{
    return qMax(1, width() - 2 * kHandle);
}

double SizeSlider::fraction() const
{
    const int span = maximum() - minimum();
    if (span <= 0) return 0.0;
    return double(value() - minimum()) / double(span);
}

int SizeSlider::handleX() const
{
    return kHandle + qRound(travel() * fraction());
}

int SizeSlider::valueAtX(int x) const
{
    const double t = qBound(0.0, double(x - kHandle) / travel(), 1.0);
    return minimum() + qRound(t * (maximum() - minimum()));
}

}
