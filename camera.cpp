#include "camera.h"
#include <algorithm>

namespace notes {

QPointF Camera::toWorld(const QPointF& screen) const
{
    return QPointF((screen.x() - double(offset.x())) / zoom,
                   (screen.y() - double(offset.y())) / zoom);
}

void Camera::panBy(const QPointF& delta)
{
    offset += QVector2D(float(delta.x()), float(delta.y()));
    clampOffset();
}

void Camera::zoomAt(const QPointF& screenAnchor, const QPointF& worldAnchor, double factor)
{
    setScale(screenAnchor, worldAnchor, double(zoom) * factor);
}

void Camera::setScale(const QPointF& screenAnchor, const QPointF& worldAnchor, double newScale)
{
    zoom = float(std::clamp(newScale, double(kMinZoom), double(kMaxZoom)));
    const float x = float(screenAnchor.x()) - float(worldAnchor.x()) * zoom;
    const float y = float(screenAnchor.y()) - float(worldAnchor.y()) * zoom;
    offset = QVector2D(x, y);
    clampOffset();
}

void Camera::centerOnPage()
{
    if (bounds_.isNull()) return;
    const double cx = (bounds_.left() + bounds_.right()) * 0.5;
    const double cy = (bounds_.top() + bounds_.bottom()) * 0.5;
    const double x = double(viewSize_.width()) * 0.5 - cx * zoom;
    const double y = double(viewSize_.height()) * 0.5 - cy * zoom;
    offset = QVector2D(float(x), float(y));
    clampOffset();
}

void Camera::clampOffset()
{
    if (bounds_.isNull()) return;
    const double vw = double(viewSize_.width());
    const double vh = double(viewSize_.height());
    const double m = kPanMargin;

    const double extL = bounds_.left() - m;
    const double extR = bounds_.right() + m;
    const double extT = bounds_.top() - m;
    const double extB = bounds_.bottom() + m;

    const double loX = -(m + extR * zoom);
    const double hiX = vw + m - extL * zoom;
    const double loY = -(m + extB * zoom);
    const double hiY = vh + m - extT * zoom;

    const double ox = loX <= hiX ? std::clamp(double(offset.x()), loX, hiX)
                                 : (loX + hiX) * 0.5;
    const double oy = loY <= hiY ? std::clamp(double(offset.y()), loY, hiY)
                                 : (loY + hiY) * 0.5;
    offset = QVector2D(float(ox), float(oy));
}

}