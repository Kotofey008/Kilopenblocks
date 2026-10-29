#pragma once
#include "Vector.h"

// Camera API recovered from symbols:
// SetPerspective(fov, aspect, near?), SetOrtho, SetViewportRect, SetFieldOfView,
// Get/Set NearPlane, FarPlane, Projection, ProjRect, CameraRenderOrder

class Camera {
public:
    enum class Projection { Perspective = 0, Ortho = 1 };

    Camera();

    void SetPerspective(float fovYDegrees, float aspect, float nearPlane);
    void SetOrtho(float left, float right, float bottom, float top, float nearP, float farP);
    void SetViewportRect(float x, float y, float w, float h);
    void GetViewportRect(float& x, float& y, float& w, float& h) const;

    void SetFieldOfView(float fov);
    float GetFieldOfView() const { return fov_; }
    void SetNearPlane(float n);
    float GetNearPlane() const { return near_; }
    void SetFarPlane(float f);
    float GetFarPlane() const { return far_; }
    void SetProjection(Projection p);
    Projection GetProjection() const { return projection_; }
    void SetCameraRenderOrder(int o) { renderOrder_ = o; }
    int GetCameraRenderOrder() const { return renderOrder_; }

    void SetPosition(const Vector& p) { position_ = p; }
    void SetYawPitch(float yaw, float pitch);
    Vector GetPosition() const { return position_; }

    // Build view/projection matrices (for GL use)
    Matrix4 GetViewMatrix() const;
    Matrix4 GetProjectionMatrix() const;

private:
    Projection projection_ = Projection::Perspective;
    float fov_ = 70.f;
    float aspect_ = 1.777f;
    float near_ = 0.1f;
    float far_ = 256.f;
    float vpX_ = 0, vpY_ = 0, vpW_ = 1, vpH_ = 1;
    int renderOrder_ = 0;
    Vector position_{0, 0, 0};
    float yaw_ = 0.f, pitch_ = 0.f;
};
