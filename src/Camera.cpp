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
    // Y-up FPS: yaw around Y, pitch around X.
    // (yaw=0, pitch=0) → look along +Z (matches movement forward).
    float yr = yaw_ * kDeg2Rad;
    float pr = pitch_ * kDeg2Rad;
    float cy = std::cos(yr), sy = std::sin(yr);
    float cp = std::cos(pr), sp = std::sin(pr);

    forward_.x = -sy * cp;
    forward_.y =  sp;
    forward_.z =  cy * cp;

    // right = normalize(cross(forward, worldUp))
    right_.x = -forward_.z;
    right_.y = 0.f;
    right_.z =  forward_.x;
    float rlen = std::sqrt(right_.x * right_.x + right_.z * right_.z);
    if (rlen > 1e-6f) {
        right_.x /= rlen;
        right_.z /= rlen;
    } else {
        // Looking straight up/down — stable right from yaw
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
    if (pitch > 89.9f) pitch = 89.9f;
    if (pitch < -89.9f) pitch = -89.9f;
    pitch_ = pitch;
    RebuildBasisFromAngles();
}

void Camera::SetBasis(const Vector& right, const Vector& up, const Vector& forward) {
    right_ = right;
    up_ = up;
    forward_ = forward;
}

void Camera::SetPosition(const Vector& p) {
    position_ = p;
}

Matrix4 Camera::GetViewMatrix() const {
    // ------------------------------------------------------------------
    // OpenGL column-major VIEW matrix (world → camera).
    //
    // p_cam.x =  dot(p - eye, right)
    // p_cam.y =  dot(p - eye, up)
    // p_cam.z =  dot(p - eye, -forward)   // camera looks down -Z
    //
    // As a matrix (rows conceptually):
    //   [  rx   ry   rz  -dot(right, eye) ]
    //   [  ux   uy   uz  -dot(up, eye)    ]
    //   [ -fx  -fy  -fz   dot(forward,eye)]
    //   [   0    0    0    1              ]
    //
    // Column-major layout for glLoadMatrixf:
    //   col0 = (rx, ux, -fx, 0)
    //   col1 = (ry, uy, -fy, 0)
    //   col2 = (rz, uz, -fz, 0)
    //   col3 = (-dot(r,e), -dot(u,e), dot(f,e), 1)
    //
    // Previous bug: rotation was stored as columns = (right, up, -forward)
    // which is camera-to-world, not world-to-camera → world "flew" on look.
    // ------------------------------------------------------------------
    const float rx = right_.x, ry = right_.y, rz = right_.z;
    const float ux = up_.x,    uy = up_.y,    uz = up_.z;
    const float fx = forward_.x, fy = forward_.y, fz = forward_.z;
    const float px = position_.x, py = position_.y, pz = position_.z;

    const float tx = -(rx * px + ry * py + rz * pz);
    const float ty = -(ux * px + uy * py + uz * pz);
    const float tz =  (fx * px + fy * py + fz * pz); // -dot(-f, eye)

    Matrix4 m;
    // column 0
    m.m[0]  =  rx;  m.m[1]  =  ux;  m.m[2]  = -fx;  m.m[3]  = 0.f;
    // column 1
    m.m[4]  =  ry;  m.m[5]  =  uy;  m.m[6]  = -fy;  m.m[7]  = 0.f;
    // column 2
    m.m[8]  =  rz;  m.m[9]  =  uz;  m.m[10] = -fz;  m.m[11] = 0.f;
    // column 3 (translation)
    m.m[12] =  tx;  m.m[13] =  ty;  m.m[14] =  tz;  m.m[15] = 1.f;
    return m;
}

Matrix4 Camera::GetProjectionMatrix() const {
    Matrix4 m = Matrix4::Identity();
    if (projection_ == Projection::Perspective) {
        float f = 1.f / std::tan(fov_ * kDeg2Rad * 0.5f);
        float fn = near_ > 1e-4f ? near_ : 0.1f;
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
