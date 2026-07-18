#pragma once

// Camera pad for the volumetric composite mode. Lives as a child widget of
// the viewer's CVolumeViewerView (viewport corner, does not pan or zoom with
// the scene). Two independent halves:
//  - left: azimuth dial (compass) — drag rotates the in-plane tilt direction
//  - right: elevation gauge — drag sets the tilt angle away from the surface
//    normal (needle vertical = straight down, 0..45 degrees)
// Double-click resets the half under the cursor; scroll adjusts the
// perspective strength (shown as an arc on the azimuth dial's rim).

#include <QWidget>

class CameraGizmoWidget : public QWidget
{
    Q_OBJECT

public:
    static constexpr float kMaxTiltDeg = 45.0f;

    explicit CameraGizmoWidget(QWidget* parent = nullptr);

    // Update the displayed state without emitting cameraChanged.
    void setCamera(float azimuthDeg, float tiltDeg, float perspective);
    float azimuthDeg() const { return _azimuthDeg; }
    float tiltDeg() const { return _tiltDeg; }
    float perspective() const { return _perspective; }

signals:
    void cameraChanged(float azimuthDeg, float tiltDeg, float perspective);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Pane { None, Azimuth, Elevation };

    Pane paneAt(const QPointF& pos) const;
    void updateFromDrag(const QPointF& pos);
    void repositionInParent();
    QPointF azimuthCenter() const;
    QPointF elevationCenter() const;
    double dialRadius() const;

    float _azimuthDeg = 0.0f;
    float _tiltDeg = 0.0f;
    float _perspective = 0.0f;
    int _wheelAccum = 0;
    Pane _dragPane = Pane::None;
};
