#include "CameraGizmoWidget.hpp"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kPaneSize = 92;
constexpr int kPaneGap = 8;
constexpr int kMargin = 12;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
} // namespace

CameraGizmoWidget::CameraGizmoWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(kPaneSize * 2 + kPaneGap, kPaneSize);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Volumetric camera. Left dial: azimuth — spins the patch in "
                  "the view plane. Right gauge: tilt — tips the camera from "
                  "straight down (vertical needle) toward flat; on screen the "
                  "tilt is always toward the top edge.\nDouble-click a half "
                  "to reset it, scroll to change perspective.\nThe slab "
                  "rotates rigidly in the flattened (slab) space, so the "
                  "render follows the page as it bends."));
    if (parent) {
        parent->installEventFilter(this);
        repositionInParent();
    }
}

void CameraGizmoWidget::setCamera(float azimuthDeg, float tiltDeg, float perspective)
{
    _azimuthDeg = azimuthDeg;
    _tiltDeg = std::clamp(tiltDeg, 0.0f, kMaxTiltDeg);
    _perspective = std::clamp(perspective, 0.0f, 1.0f);
    update();
}

void CameraGizmoWidget::setRightInset(int inset)
{
    if (_rightInset == inset)
        return;
    _rightInset = inset;
    repositionInParent();
}

QPointF CameraGizmoWidget::azimuthCenter() const
{
    return QPointF(kPaneSize * 0.5, height() * 0.5);
}

QPointF CameraGizmoWidget::elevationCenter() const
{
    // Needle pivot: bottom-center of the right pane.
    return QPointF(kPaneSize + kPaneGap + kPaneSize * 0.5, height() - 8.0);
}

double CameraGizmoWidget::dialRadius() const
{
    return kPaneSize * 0.5 - 5.0;
}

CameraGizmoWidget::Pane CameraGizmoWidget::paneAt(const QPointF& pos) const
{
    if (pos.x() < kPaneSize + kPaneGap * 0.5)
        return Pane::Azimuth;
    return Pane::Elevation;
}

void CameraGizmoWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor accent(0, 220, 255);
    const QColor faint(200, 200, 200, 70);

    // ---- Azimuth dial (left) ----
    {
        const QPointF c = azimuthCenter();
        const double r = dialRadius();

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 20, 160));
        p.drawEllipse(c, r, r);

        QPen rim(QColor(accent.red(), accent.green(), accent.blue(), 200), 1.4);
        rim.setCosmetic(true);
        p.setPen(rim);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r, r);

        // Perspective strength: arc along the rim, clockwise from the top.
        if (_perspective > 0.0f) {
            QPen arcPen(QColor(255, 200, 60, 220), 2.4);
            arcPen.setCosmetic(true);
            p.setPen(arcPen);
            const QRectF arcRect(c.x() - r, c.y() - r, r * 2.0, r * 2.0);
            p.drawArc(arcRect, 90 * 16, -int(std::lround(_perspective * 360.0)) * 16);
        }

        // Cardinal ticks (+U right, +V down, matching slab coords).
        p.setPen(QPen(faint, 1.0));
        for (int i = 0; i < 4; ++i) {
            const double a = i * 90.0 * kDegToRad;
            const QPointF dir(std::cos(a), std::sin(a));
            p.drawLine(c + dir * (r * 0.82), c + dir * r);
        }

        // Needle + rim dot at the current azimuth.
        const double a = double(_azimuthDeg) * kDegToRad;
        const QPointF dir(std::cos(a), std::sin(a));
        QPen needle(accent, 1.6);
        needle.setCosmetic(true);
        p.setPen(needle);
        p.drawLine(c, c + dir * (r * 0.86));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 100));
        p.drawEllipse(c + dir * (r * 0.86), 5.0, 5.0);
        p.setBrush(accent);
        p.drawEllipse(c + dir * (r * 0.86), 2.5, 2.5);
        p.drawEllipse(c, 2.0, 2.0);
    }

    // ---- Elevation gauge (right) ----
    {
        const QPointF pivot = elevationCenter();
        const double r = kPaneSize - 18.0;

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 20, 160));
        QPainterPath bg;
        bg.moveTo(pivot);
        // True-angle sector: vertical (0 deg tilt) to 45 deg.
        bg.arcTo(QRectF(pivot.x() - r, pivot.y() - r, r * 2.0, r * 2.0), 45.0, 45.0);
        bg.closeSubpath();
        p.drawPath(bg);

        // Gauge arc; the needle angle equals the actual tilt angle.
        QPen rim(QColor(0, 220, 255, 200), 1.4);
        rim.setCosmetic(true);
        p.setPen(rim);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(pivot.x() - r, pivot.y() - r, r * 2.0, r * 2.0),
                  45 * 16, 45 * 16);

        // Reference ticks at 0, 22.5 and 45 degrees of tilt.
        p.setPen(QPen(faint, 1.0));
        for (const double tickTilt : {0.0, 22.5, 45.0}) {
            const double a = tickTilt * kDegToRad;
            const QPointF dir(std::sin(a), -std::cos(a));
            p.drawLine(pivot + dir * (r * 0.86), pivot + dir * r);
        }

        // Needle at the current tilt (vertical = straight down the normal).
        const double a = double(_tiltDeg) * kDegToRad;
        const QPointF dir(std::sin(a), -std::cos(a));
        QPen needle(accent, 1.6);
        needle.setCosmetic(true);
        p.setPen(needle);
        p.drawLine(pivot, pivot + dir * (r * 0.9));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 100));
        p.drawEllipse(pivot + dir * (r * 0.9), 5.0, 5.0);
        p.setBrush(accent);
        p.drawEllipse(pivot + dir * (r * 0.9), 2.5, 2.5);
        p.drawEllipse(pivot, 2.0, 2.0);
    }
}

void CameraGizmoWidget::updateFromDrag(const QPointF& pos)
{
    if (_dragPane == Pane::Azimuth) {
        const QPointF c = azimuthCenter();
        const double dx = pos.x() - c.x();
        const double dy = pos.y() - c.y();
        if (std::hypot(dx, dy) < 3.0)
            return;  // too close to the center to define a direction
        _azimuthDeg = float(std::atan2(dy, dx) / kDegToRad);
    } else if (_dragPane == Pane::Elevation) {
        const QPointF pivot = elevationCenter();
        const double dx = pos.x() - pivot.x();
        const double dy = pivot.y() - pos.y();  // up is positive
        const double angle = std::atan2(std::max(dx, 0.0), std::max(dy, 0.0));
        _tiltDeg = std::clamp(float(angle / kDegToRad), 0.0f, kMaxTiltDeg);
    } else {
        return;
    }
    update();
    emit cameraChanged(_azimuthDeg, _tiltDeg, _perspective);
}

void CameraGizmoWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        _dragPane = paneAt(event->position());
        updateFromDrag(event->position());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CameraGizmoWidget::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && _dragPane != Pane::None) {
        updateFromDrag(event->position());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void CameraGizmoWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (paneAt(event->position()) == Pane::Azimuth)
            _azimuthDeg = 0.0f;
        else
            _tiltDeg = 0.0f;
        update();
        emit cameraChanged(_azimuthDeg, _tiltDeg, _perspective);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CameraGizmoWidget::wheelEvent(QWheelEvent* event)
{
    _wheelAccum += event->angleDelta().y();
    const int steps = _wheelAccum / 120;
    if (steps != 0) {
        _wheelAccum -= steps * 120;
        _perspective = std::clamp(_perspective + float(steps) * 0.05f, 0.0f, 1.0f);
        update();
        emit cameraChanged(_azimuthDeg, _tiltDeg, _perspective);
    }
    event->accept();
}

bool CameraGizmoWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parent() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        repositionInParent();
    }
    return QWidget::eventFilter(watched, event);
}

void CameraGizmoWidget::repositionInParent()
{
    if (auto* p = parentWidget()) {
        move(p->width() - width() - kMargin - _rightInset,
             p->height() - height() - kMargin);
    }
}
