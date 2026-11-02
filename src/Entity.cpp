#include "Entity.h"
#include "Camera.h"
#include "TreeBuilder.h" // BlockId
#include <cmath>
#include <algorithm>

int Entity::g_FreeEntityID = 1;

Entity::Entity(EntitySet* set) : set_(set), id_(g_FreeEntityID++) {}
Entity::~Entity() {}

void Entity::Init() {}
void Entity::Update() {}
void Entity::FixedUpdate() {}
void Entity::FixedPostUpdate() {}
void Entity::Load(Stream&, int) {}
void Entity::Save(Stream&) {}

Matrix4 Entity::GetTransform(int) {
    return Matrix4::Translation(position_);
}

bool Entity::CheckGround() {
    if (!grid_) { onGround_ = false; return false; }
    // Sample a point slightly below the feet
    float feetY = position_.y - 0.05f;
    int bx = (int)std::floor(position_.x);
    int by = (int)std::floor(feetY);
    int bz = (int)std::floor(position_.z);
    int idx = grid_->CoordToIndex(bx, by, bz);
    if (idx < 0) { onGround_ = false; return false; }
    unsigned char t = grid_->cellTypes[idx];
    // solid if not air, water, or non-colliding plants
    onGround_ = (t != BlockId::Air && t != BlockId::Water &&
                 t != BlockId::Flower1 && t != BlockId::Mushroom1 &&
                 t != BlockId::Mushroom2 && t != BlockId::Leaves1 &&
                 t != BlockId::Leaves2);
    return onGround_;
}

void Entity::Move(const Vector& delta) {
    if (!grid_) {
        position_.x += delta.x;
        position_.y += delta.y;
        position_.z += delta.z;
        return;
    }
    // Axis-separated collision (common for voxel engines; matches size of Move ~649B)
    auto tryAxis = [&](float& pos, float d, int axis) {
        if (d == 0.f) return;
        float newPos = pos + d;
        // check bounding box corners against blocks
        float halfW = size_.x * 0.5f;
        float halfD = size_.z * 0.5f;
        float h = size_.y;
        float xs[2] = { position_.x - halfW, position_.x + halfW };
        float ys[2] = { position_.y, position_.y + h };
        float zs[2] = { position_.z - halfD, position_.z + halfD };
        if (axis == 0) { xs[0] = newPos - halfW; xs[1] = newPos + halfW; }
        if (axis == 1) { ys[0] = newPos; ys[1] = newPos + h; }
        if (axis == 2) { zs[0] = newPos - halfD; zs[1] = newPos + halfD; }

        bool blocked = false;
        for (int ix = 0; ix < 2 && !blocked; ++ix)
        for (int iy = 0; iy < 2 && !blocked; ++iy)
        for (int iz = 0; iz < 2 && !blocked; ++iz) {
            int bx = (int)std::floor(xs[ix]);
            int by = (int)std::floor(ys[iy]);
            int bz = (int)std::floor(zs[iz]);
            int idx = grid_->CoordToIndex(bx, by, bz);
            if (idx < 0) continue;
            unsigned char t = grid_->cellTypes[idx];
            if (t != BlockId::Air && t != BlockId::Water &&
                t != BlockId::Flower1 && t != BlockId::Mushroom1 &&
                t != BlockId::Mushroom2) {
                // leaves are semi-solid for simplicity (original may differ)
                if (t == BlockId::Leaves1 || t == BlockId::Leaves2) continue;
                blocked = true;
            }
        }
        if (!blocked) pos = newPos;
        else if (axis == 1) velocity_.y = 0; // hit floor/ceiling
    };

    tryAxis(position_.x, delta.x, 0);
    tryAxis(position_.y, delta.y, 1);
    tryAxis(position_.z, delta.z, 2);
    CheckGround();
}

// --- EntityPlayer ---

EntityPlayer::EntityPlayer(EntityPlayerSet* set) : Entity(set) {
    size_ = Vector(0.6f, 1.8f, 0.6f);
}

EntityPlayer::~EntityPlayer() {}

void EntityPlayer::Init() {
    Entity::Init();
    eyeHeight_ = 1.62f;
    mode_ = Mode::Walking;
}

void EntityPlayer::SetYawPitch(float yaw, float pitch) {
    yaw_ = yaw;
    // Match binary clamps at 0x5b4e4 / 0x5b531: ±90 degrees (constants @ 0x107e40/44)
    if (pitch > 90.f) pitch = 90.f;
    if (pitch < -90.f) pitch = -90.f;
    pitch_ = pitch;
}

void EntityPlayer::SetMoveInput(float forward, float strafe, bool jump) {
    moveForward_ = forward;
    moveStrafe_ = strafe;
    jumpPressed_ = jump;
}

void EntityPlayer::UpdateMode() {
    // original switches walk/fly; keep simple
}

// ---------------------------------------------------------------------------
// GetRotationVectors @ 0xb7190 (501 B) — CONFIRMED structure
//
// Args: (Vector& right, Vector& up, Vector& forward, float yawDeg, float pitchDeg)
// Converts degrees→radians via ×π / 180 (constants at 0x107e00 / 0x107dfc),
// then builds Y-up FPS basis. Forward at (yaw=0,pitch=0) = +Z.
// ---------------------------------------------------------------------------
void GetRotationVectors(Vector& outRight, Vector& outUp, Vector& outForward,
                        float yawDegrees, float pitchDegrees) {
    const float kPi = 3.14159265358979323846f;
    const float kDeg2Rad = kPi / 180.f;
    float yr = yawDegrees * kDeg2Rad;
    float pr = pitchDegrees * kDeg2Rad;
    float cy = std::cos(yr), sy = std::sin(yr);
    float cp = std::cos(pr), sp = std::sin(pr);

    // Forward
    outForward.x = -sy * cp;
    outForward.y =  sp;
    outForward.z =  cy * cp;

    // Right = normalize(cross(forward, worldUp))
    outRight.x = -outForward.z;
    outRight.y = 0.f;
    outRight.z =  outForward.x;
    float rlen = std::sqrt(outRight.x * outRight.x + outRight.z * outRight.z);
    if (rlen > 1e-6f) {
        outRight.x /= rlen;
        outRight.z /= rlen;
    } else {
        outRight.x = cy;
        outRight.y = 0.f;
        outRight.z = sy;
    }

    // Up = cross(right, forward)
    outUp.x = outRight.y * outForward.z - outRight.z * outForward.y;
    outUp.y = outRight.z * outForward.x - outRight.x * outForward.z;
    outUp.z = outRight.x * outForward.y - outRight.y * outForward.x;
}

// ---------------------------------------------------------------------------
// EntityPlayer::UpdateRotation @ 0x5b4a0 (214 B)
//
// Binary:
//   pitch (this+0xc0) += inputPitchDelta; clamp to [-90, +90]
//   yaw   (this+0xc4) += inputYawDelta;   wrap via ±360 into (-180, 180]
// Input deltas come from the look controller (edx+0x254 yaw, +0x258 pitch).
// ---------------------------------------------------------------------------
void EntityPlayer::UpdateRotation(float lookDeltaYaw, float lookDeltaPitch) {
    pitch_ += lookDeltaPitch;
    if (pitch_ > 90.f) pitch_ = 90.f;
    if (pitch_ < -90.f) pitch_ = -90.f;

    yaw_ += lookDeltaYaw;
    // Binary wraps with constants 360 / -180 @ 0x107e48 / 0x107e4c
    while (yaw_ > 180.f) yaw_ -= 360.f;
    while (yaw_ <= -180.f) yaw_ += 360.f;
}

// ---------------------------------------------------------------------------
// EntityPlayer::UpdateCameraTransform @ 0x5b580 (1262 B) — structure recovered
//
// Binary flow (CONFIRMED from pdf):
//   1. GetRotationVectors(right, up, forward, yaw, pitch)
//   2. Eye = feet + (0, eyeHeight, 0)  (eye height field on player)
//   3. Build / orthonormalize basis (several sqrt paths for edge cases)
//   4. Write transform into camera / scene-object world matrix
//
// Reconstruction maps this onto Camera::SetPosition + SetBasis. Full
// SceneObject hierarchy / InvWorldTM path is not present in this port.
// ---------------------------------------------------------------------------
void EntityPlayer::UpdateCameraTransform(Camera& camera) {
    Vector right, up, forward;
    GetRotationVectors(right, up, forward, yaw_, pitch_);
    Vector eye = GetEyePosition();
    // Set angles first (rebuilds basis), then overwrite basis with the
    // GetRotationVectors result so both stay consistent.
    camera.SetYawPitch(yaw_, pitch_);
    camera.SetPosition(eye);
    camera.SetBasis(right, up, forward);
}

void EntityPlayer::UpdateCamera() {
    // Legacy no-arg stub; real work is UpdateCameraTransform(Camera&).
}

void EntityPlayer::FixedUpdate() {
    // Gravity
    if (mode_ == Mode::Walking) {
        if (!onGround_)
            velocity_.y += gravity_ * (1.f / 60.f);
        else if (jumpPressed_) {
            velocity_.y = jumpSpeed_;
            onGround_ = false;
        } else {
            velocity_.y = std::min(velocity_.y, 0.f);
        }
    } else {
        // flying: vertical from pitch/input
        velocity_.y = moveForward_ * std::sin(pitch_ * 3.14159265f / 180.f) * flySpeed_;
    }

    float yawRad = yaw_ * 3.14159265f / 180.f;
    float speed = (mode_ == Mode::Flying) ? flySpeed_ : walkSpeed_;
    float sinY = std::sin(yawRad), cosY = std::cos(yawRad);
    // Match GetLookDirection XZ: (-sin, cos)
    float fx = (-sinY) * moveForward_ + (cosY) * moveStrafe_;
    float fz = ( cosY) * moveForward_ + (sinY) * moveStrafe_;
    // normalize horizontal
    float len = std::sqrt(fx*fx + fz*fz);
    if (len > 1.f) { fx /= len; fz /= len; }

    Vector delta;
    delta.x = fx * speed * (1.f / 60.f);
    delta.z = fz * speed * (1.f / 60.f);
    delta.y = velocity_.y * (1.f / 60.f);

    Move(delta);
    jumpPressed_ = false;
}

void EntityPlayer::FixedPostUpdate() {
    CheckGround();
}

Matrix4 EntityPlayer::GetTransform(int) {
    // Original returns full world TM; approximate with eye translation.
    // Rotation is applied to the Camera via UpdateCameraTransform.
    Vector eye = position_;
    eye.y += eyeHeight_;
    return Matrix4::Translation(eye);
}

Vector EntityPlayer::GetEyePosition() const {
    return Vector(position_.x, position_.y + eyeHeight_, position_.z);
}

Vector EntityPlayer::GetLookDirection() const {
    Vector r, u, f;
    GetRotationVectors(r, u, f, yaw_, pitch_);
    return f;
}

void EntityPlayer::GetBasis(Vector& outRight, Vector& outUp, Vector& outForward) const {
    GetRotationVectors(outRight, outUp, outForward, yaw_, pitch_);
}

void EntityPlayer::AddOrDeleteBlock(bool deleteMode, unsigned char placeType) {
    if (!grid_) return;
    Vector eye = GetEyePosition();
    Vector dir = GetLookDirection();
    const float reach = 6.f;
    Vector to(eye.x + dir.x * reach, eye.y + dir.y * reach, eye.z + dir.z * reach);
    GridDataTraceResult tr;
    grid_->TracePoint(eye, to, tr, this);
    if (!tr.hit) return;
    if (deleteMode) {
        grid_->BreakBlock(tr);
    } else {
        grid_->PlaceBlock(tr, placeType);
    }
}
