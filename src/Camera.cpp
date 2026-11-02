#include "Camera.h"
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.f;
}

Camera::Camera() {
    RebuildBasisFromAngles();
}

void Camera::SetPerspective(float fovYDegrees, float aspect, float nearPlane) {
    projection_ = Projection::Perspective;
    fov_ = fovYDegrees;
    aspect_ = aspect > 0.01f ? aspect : 1.777f;
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

void Camera::RebuildBasisFromAngles() {
    // Same convention as GetRotationVectors / GetLookDirection:
    // Y-up, yaw around Y, pitch around X; yaw=0 pitch=0 → look +Z.
    float yr = yaw_ * kDeg2Rad;
    float pr = pitch_ * kDeg2Rad;
    float cy = std::cos(yr), sy = std::sin(yr);
    float cp = std::cos(pr), sp = std::sin(pr);

    forward_.x = -sy * cp;
    forward_.y =  sp;
    forward_.z =  cy * cp;

    // right = normalize(cross(forward, worldUp=(0,1,0))) = normalize((-fz, 0, fx))
    right_.x = -forward_.z;
    right_.y = 0.f;
    right_.z =  forward_.x;
    float rlen = std::sqrt(right_.x * right_.x + right_.z * right_.z);
    if (rlen > 1e-6f) {
        right_.x /= rlen;
        right_.z /= rlen;
    } else {
        // Looking straight up/down — pick a stable right from yaw alone
        right_.x = cy;
        right_.y = 0.f;
        right_.z = sy;
    }

    // up = cross(right, forward)
    up_.x = right_.y * forward_.z - right_.z * forward_.y;
    up_.y = right_.z * forward_.x - right_.x * forward_.z;
    up_.z = right_.x * forward_.y - right_.y * forward_.x;
}

void Camera::SetYawPitch(float yaw, float pitch) {
    yaw_ = yaw;
    if (pitch > 89.f) pitch = 89.f;
    if (pitch < -89.f) pitch = -89.f;
    pitch_ = pitch;
    RebuildBasisFromAngles();
}

void Camera::SetBasis(const Vector& right, const Vector& up, const Vector& forward) {
    right_ = right;
    up_ = up;
    forward_ = forward;
}

Matrix4 Camera::GetViewMatrix() const {
    // Column-major view matrix for glLoadMatrixf:
    //   columns = right, up, -forward, translation
    // Translation = -R^T * position  (camera looks along +forward in world)
    const float rx = right_.x, ry = right_.y, rz = right_.z;
    const float ux = up_.x,    uy = up_.y,    uz = up_.z;
    const float fx = forward_.x, fy = forward_.y, fz = forward_.z;
    const float px = position_.x, py = position_.y, pz = position_.z;

    Matrix4 m = Matrix4::Identity();
    // col0 = right
    m.m[0] = rx;  m.m[1] = ry;  m.m[2] = rz;  m.m[3] = 0.f;
    // col1 = up
    m.m[4] = ux;  m.m[5] = uy;  m.m[6] = uz;  m.m[7] = 0.f;
    // col2 = -forward  (OpenGL camera looks down -Z in view space)
    m.m[8]  = -fx; m.m[9]  = -fy; m.m[10] = -fz; m.m[11] = 0.f;
    // col3 = translation
    m.m[12] = -(rx * px + ry * py + rz * pz);
    m.m[13] = -(ux * px + uy * py + uz * pz);
    m.m[14] = -(-fx * px + -fy * py + -fz * pz); // = +(fx*px+fy*py+fz*pz)
    m.m[15] = 1.f;
    return m;
}

Matrix4 Camera::GetProjectionMatrix() const {
    Matrix4 m = Matrix4::Identity();
    if (projection_ == Projection::Perspective) {
        // Standard OpenGL perspective; FOV is vertical, degrees.
        float f = 1.f / std::tan(fov_ * kDeg2Rad * 0.5f);
        float fn = near_;
        float ff = far_ > fn + 1.f ? far_ : 256.f;
        float asp = aspect_ > 0.01f ? aspect_ : 1.777f;
        m.m[0]  = f / asp;
        m.m[5]  = f;
        m.m[10] = (ff + fn) / (fn - ff);
        m.m[11] = -1.f;
        m.m[14] = (2.f * ff * fn) / (fn - ff);
        m.m[15] = 0.f;
    }
    return m;
}
