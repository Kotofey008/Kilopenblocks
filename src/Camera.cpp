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
    // Full FPS view: rotation from yaw/pitch + translation (matches GetLookDirection)
    float yr = yaw_ * 3.14159265f / 180.f;
    float pr = pitch_ * 3.14159265f / 180.f;
    float cy = std::cos(yr), sy = std::sin(yr);
    float cp = std::cos(pr), sp = std::sin(pr);

    // Forward (camera looks along -Z in view space)
    float fx = -sy * cp;
    float fy =  sp;
    float fz =  cy * cp;
    // Right = normalize(cross(forward, worldUp))
    // worldUp = (0,1,0); cross(f, up) = (fz, 0, -fx) then renormalize
    float rx = fz, ry = 0.f, rz = -fx;
    float rlen = std::sqrt(rx*rx + rz*rz);
    if (rlen > 1e-6f) { rx /= rlen; rz /= rlen; }
    else { rx = 1.f; rz = 0.f; }
    // Up = cross(right, forward)
    float ux = ry * fz - rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy - ry * fx;

    Matrix4 m = Matrix4::Identity();
    // Column-major OpenGL: first row of rotation is right, etc.
    m.m[0] = rx;  m.m[4] = ux;  m.m[8]  = -fx; m.m[12] = 0;
    m.m[1] = ry;  m.m[5] = uy;  m.m[9]  = -fy; m.m[13] = 0;
    m.m[2] = rz;  m.m[6] = uz;  m.m[10] = -fz; m.m[14] = 0;
    m.m[3] = 0;   m.m[7] = 0;   m.m[11] = 0;   m.m[15] = 1;
    // Apply -position in camera space: t = -R * position
    float px = position_.x, py = position_.y, pz = position_.z;
    m.m[12] = -(rx*px + ry*py + rz*pz);
    m.m[13] = -(ux*px + uy*py + uz*pz);
    m.m[14] = -(-fx*px + -fy*py + -fz*pz);
    return m;
}

Matrix4 Camera::GetProjectionMatrix() const {
    Matrix4 m = Matrix4::Identity();
    if (projection_ == Projection::Perspective) {
        float f = 1.f / std::tan(fov_ * 3.14159265f / 360.f);
        float fn = near_;
        float ff = far_ > fn + 1.f ? far_ : 256.f;
        m.m[0] = f / (aspect_ > 0.01f ? aspect_ : 1.777f);
        m.m[5] = f;
        m.m[10] = (ff + fn) / (fn - ff);
        m.m[11] = -1.f;
        m.m[14] = (2.f * ff * fn) / (fn - ff);
        m.m[15] = 0.f;
    }
    return m;
}
