#include "Camera.h"
#include <cmath>

Camera::Camera() {}

void Camera::SetPerspective(float fovYDegrees, float aspect, float nearPlane) {
    projection_ = Projection::Perspective;
    fov_ = fovYDegrees;
    aspect_ = aspect;
    near_ = nearPlane;
}

void Camera::SetOrtho(float, float, float, float, float nearP, float farP) {
    projection_ = Projection::Ortho;
    near_ = nearP;
    far_ = farP;
}

void Camera::SetViewportRect(float x, float y, float w, float h) {
    vpX_ = x; vpY_ = y; vpW_ = w; vpH_ = h;
}
void Camera::GetViewportRect(float& x, float& y, float& w, float& h) const {
    x = vpX_; y = vpY_; w = vpW_; h = vpH_;
}

void Camera::SetFieldOfView(float fov) { fov_ = fov; }
void Camera::SetNearPlane(float n) { near_ = n; }
void Camera::SetFarPlane(float f) { far_ = f; }
void Camera::SetProjection(Projection p) { projection_ = p; }

void Camera::SetYawPitch(float yaw, float pitch) {
    yaw_ = yaw;
    pitch_ = pitch;
}

Matrix4 Camera::GetViewMatrix() const {
    // Simple look-at from position with yaw/pitch
    float yr = yaw_ * 3.14159265f / 180.f;
    float pr = pitch_ * 3.14159265f / 180.f;
    Vector forward{
        -std::sin(yr) * std::cos(pr),
         std::sin(pr),
         std::cos(yr) * std::cos(pr)
    };
    // Build a minimal view matrix (translation only for now; full basis TBD)
    Matrix4 m = Matrix4::Identity();
    m.m[12] = -position_.x;
    m.m[13] = -position_.y;
    m.m[14] = -position_.z;
    (void)forward;
    return m;
}

Matrix4 Camera::GetProjectionMatrix() const {
    Matrix4 m = Matrix4::Identity();
    if (projection_ == Projection::Perspective) {
        float f = 1.f / std::tan(fov_ * 3.14159265f / 360.f);
        m.m[0] = f / aspect_;
        m.m[5] = f;
        m.m[10] = (far_ + near_) / (near_ - far_);
        m.m[11] = -1.f;
        m.m[14] = (2.f * far_ * near_) / (near_ - far_);
        m.m[15] = 0.f;
    }
    return m;
}
