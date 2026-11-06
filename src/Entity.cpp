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
    // Defaults match binary ParamSet ctor @ 0x5e000 / start.scene until ApplyPlayerSetConfig
    size_ = Vector(0.49f, 1.75f, 0.49f);
    paramSets_[0] = PlayerParamSet{};
    paramSets_[1] = PlayerParamSet{2.f, 9.f, 9.f, 0.501f, 0.062625f, -4.9f, -2.f, 3.f};
    paramSets_[2] = PlayerParamSet{2.f, 9.f, 18.f, 0.501f, 0.062625f, 0.f, 0.f, 5.f};
    paramSets_[3] = PlayerParamSet{6.f, 18.f, 18.f, 0.501f, 0.062625f, -9.81f, -50.f, 5.f};
}

EntityPlayer::~EntityPlayer() {}

void EntityPlayer::Init() {
    Entity::Init();
    mode_ = Mode::Walk;
    horizSpeed_ = 0.f;
}

void EntityPlayer::ApplyPlayerSetConfig(const SceneEntityPlayerSetConfig& cfg) {
    for (int i = 0; i < 4; ++i)
        paramSets_[i] = cfg.paramSets[i];
    operationDistance_ = cfg.operationDistance;
    eyeHeight_ = cfg.EyeHeightFromBBox();
    // size_ = full AABB extents (width, height, depth)
    size_ = Vector(cfg.BodyWidth(), cfg.BodyHeight(), cfg.BodyWidth());
    flipX_ = cfg.flipXRotation;
    flipY_ = cfg.flipYRotation;
}

const PlayerParamSet& EntityPlayer::CurrentParamSet() const {
    int i = (int)mode_;
    if (i < 0 || i >= 4) i = 0;
    return paramSets_[i];
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
    // EntityPlayer::UpdateMode @ 0x5b040 — confirmed control flow:
    //   if EntityPlayerSet.flying → mode = 3 (Fly)
    //   else if mode was Fly → mode = 0, then re-evaluate environment
    //   else if OnLadder(...) → mode = 2
    //   else mode = IsUnderwater(eye/body) ? 1 : 0
    auto* pset = static_cast<EntityPlayerSet*>(set_);
    if (pset && pset->GetFlying()) {
        mode_ = Mode::Fly;
        return;
    }
    if (mode_ == Mode::Fly)
        mode_ = Mode::Walk;

    if (!grid_) {
        mode_ = Mode::Walk;
        return;
    }
    // OnLadder(feet, size, offset) — binary; our OnLadder is stub → false
    if (grid_->OnLadder(position_, size_, Vector(0, 0, 0))) {
        mode_ = Mode::Ladder;
        return;
    }
    // Sample near eye / chest (binary adds bbox extents to set origin)
    Vector sample = position_;
    sample.y += eyeHeight_ * 0.5f;
    mode_ = grid_->IsUnderwater(sample) ? Mode::Swim : Mode::Walk;
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

    // CONFIRMED (0xb7190 / recovered notes): right = normalize(cross(forward, worldUp))
    // At (yaw=0,pitch=0): forward=+Z, right=(-1,0,0), up=(0,1,0).
    // Do NOT flip this — wrong cross order inverts up and flips the view vertically.
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

    // up = cross(right, forward)
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
    // Clamp per-frame delta so a single bad touch sample cannot spin 360°.
    if (lookDeltaYaw > 30.f) lookDeltaYaw = 30.f;
    if (lookDeltaYaw < -30.f) lookDeltaYaw = -30.f;
    if (lookDeltaPitch > 30.f) lookDeltaPitch = 30.f;
    if (lookDeltaPitch < -30.f) lookDeltaPitch = -30.f;

    pitch_ += lookDeltaPitch;
    // Stay away from exact ±90 to avoid basis singularity in GetRotationVectors
    if (pitch_ > 89.9f) pitch_ = 89.9f;
    if (pitch_ < -89.9f) pitch_ = -89.9f;

    yaw_ += lookDeltaYaw;
    // Binary wraps with constants 360 / -180 @ 0x107e48 / 0x107e4c
    while (yaw_ > 180.f) yaw_ -= 360.f;
    while (yaw_ <= -180.f) yaw_ += 360.f;
}

// ---------------------------------------------------------------------------
// EntityPlayer::UpdateCameraTransform @ 0x5b580 (1262 B)
//
// Hard lock: camera eye = player feet + eyeHeight; orientation from yaw/pitch.
// Every frame overwrites Camera position + basis so the view cannot drift.
// ---------------------------------------------------------------------------
void EntityPlayer::UpdateCameraTransform(Camera& camera) {
    Vector right, up, forward;
    GetRotationVectors(right, up, forward, yaw_, pitch_);
    Vector eye = GetEyePosition(); // feet + (0, eyeHeight_, 0)

    // Angles → internal basis, then force the GetRotationVectors result
    camera.SetYawPitch(yaw_, pitch_);
    camera.SetPosition(eye);
    camera.SetBasis(right, up, forward);
}

void EntityPlayer::UpdateCamera() {
    // Legacy no-arg stub; real work is UpdateCameraTransform(Camera&).
}

void EntityPlayer::FixedUpdate() {
    // 1) Resolve mode → selects ParamSets[mode] (binary UpdateMode then physics)
    UpdateMode();
    const PlayerParamSet& ps = CurrentParamSet();
    const float dt = 1.f / 60.f;

    // 2) Vertical — from SimplePlayer::Update(ParamSet const&, float) @ 0xdc9b0
    //    ParamSet layout (scene / binary): +0 maxSpeed, +4 accel, +8 decel,
    //    +0xc step, +0x10 smallStep, +0x14 gravity, +0x18 gravityLimit, +0x1c jump
    //    Vertical state at SimplePlayer+0x1f4 (recovered as velocity_.y / +500 in pdg).
    //
    //    mode 0 Walk: gravity when airborne; on ground jump → vy = Jump
    //    mode 1 Swim: CONFIRMED
    //      if jump held:
    //        vy = min(MaximumSpeed, vy + Acceleration*dt)   // rise while holding
    //      else:
    //        vy += Gravity*dt                               // sink (ParamSets[1].Gravity=-4.9)
    //      if vy < GravityLimit:                            // soft floor at -2
    //        vy = min(GravityLimit, vy + Deceleration^2 * dt)
    //    mode 2 Ladder: jump held → vy = Jump, else 0 (binary)
    //    mode 3 Fly: separate path (accel along look; kept as prior RE)
    if (mode_ == Mode::Fly) {
        velocity_.y = moveForward_ * std::sin(pitch_ * 3.14159265f / 180.f) * ps.maximumSpeed;
    } else if (mode_ == Mode::Ladder) {
        // SimplePlayer mode==2: bVar13 (jump/move-up) → vy = Jump, else 0
        velocity_.y = jumpPressed_ ? ps.jump : 0.f;
    } else if (mode_ == Mode::Swim) {
        // SimplePlayer mode==1 — hold jump to swim up
        if (jumpPressed_) {
            float vy = velocity_.y + ps.acceleration * dt;
            if (vy > ps.maximumSpeed)
                vy = ps.maximumSpeed;
            velocity_.y = vy;
        } else {
            velocity_.y += ps.gravity * dt;
        }
        // Soft clamp when falling faster than GravityLimit (swim: -2)
        if (velocity_.y < ps.gravityLimit) {
            float pulled = velocity_.y + ps.deceleration * ps.deceleration * dt;
            if (pulled > ps.gravityLimit)
                pulled = ps.gravityLimit;
            velocity_.y = pulled;
        }
    } else {
        // Walk
        if (!onGround_) {
            velocity_.y += ps.gravity * dt;
            if (velocity_.y < ps.gravityLimit)
                velocity_.y = ps.gravityLimit;
        } else if (jumpPressed_) {
            velocity_.y = ps.jump;
            onGround_ = false;
        } else {
            velocity_.y = std::min(velocity_.y, 0.f);
        }
    }

    // 3) Horizontal — MaximumSpeed + Acceleration / Deceleration
    //    Confirmed in SimplePlayer::Update: load edi+0 (max), edi+4 (accel), edi+8 (decel)
    // Horizontal intent in yaw frame (matches original algebraic form):
    //   at yaw=0: forward→+Z, +strafe→+X. With GetRotationVectors right=-X at yaw=0,
    //   this equals forward*fwd + (-right)*strafe.
    float yawRad = yaw_ * 3.14159265f / 180.f;
    float sinY = std::sin(yawRad), cosY = std::cos(yawRad);
    float ix = (-sinY) * moveForward_ + (cosY) * moveStrafe_;
    float iz = ( cosY) * moveForward_ + (sinY) * moveStrafe_;
    float ilen = std::sqrt(ix * ix + iz * iz);
    float inputMag = ilen;
    if (ilen > 1.f) { ix /= ilen; iz /= ilen; inputMag = 1.f; }
    else if (ilen > 1e-6f) { ix /= ilen; iz /= ilen; }

    if (inputMag > 1e-4f) {
        horizSpeed_ += ps.acceleration * dt;
        if (horizSpeed_ > ps.maximumSpeed)
            horizSpeed_ = ps.maximumSpeed;
    } else {
        horizSpeed_ -= ps.deceleration * dt;
        if (horizSpeed_ < 0.f) horizSpeed_ = 0.f;
    }

    Vector delta;
    delta.x = ix * horizSpeed_ * dt;
    delta.z = iz * horizSpeed_ * dt;
    delta.y = velocity_.y * dt;

    // StepSize / SmallStepSize: used in SimplePlayer collision step-up (edi+0xc/+0x10).
    // Not applied here — Entity::Move has no step-climb path yet (UNKNOWN coupling).

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
    const float reach = operationDistance_; // start.scene OperationDistance (default 8)
    Vector to(eye.x + dir.x * reach, eye.y + dir.y * reach, eye.z + dir.z * reach);
    GridDataTraceResult tr;
    grid_->TracePoint(eye, to, tr, this);
    if (!tr.hit) return;
    if (deleteMode) {
        grid_->BreakBlock(tr);
    } else {
        // IsPossibleToAdd (0x99530): rejects if target cell non-empty OR any
        // registered entity AABB intersects the destination cell (vtable+0x80).
        // Player AABB = position with size_ extents (feet origin, Y-up).
        grid_->PlaceBlock(tr, placeType, &position_, &size_);
    }
}

// --- EntityPlayerSet ---

EntityPlayerSet::EntityPlayerSet() {
    // Binary ParamSet defaults; overwritten by Scene::Load EntityPlayerSet section
    config_.paramSets[0] = PlayerParamSet{};
    config_.paramSets[1] = PlayerParamSet{2.f, 9.f, 9.f, 0.501f, 0.062625f, -4.9f, -2.f, 3.f};
    config_.paramSets[2] = PlayerParamSet{2.f, 9.f, 18.f, 0.501f, 0.062625f, 0.f, 0.f, 5.f};
    config_.paramSets[3] = PlayerParamSet{6.f, 18.f, 18.f, 0.501f, 0.062625f, -9.81f, -50.f, 5.f};
}

void EntityPlayerSet::SetConfig(const SceneEntityPlayerSetConfig& cfg) {
    config_ = cfg;
    ApplyToLocalPlayer();
}

void EntityPlayerSet::ApplyToLocalPlayer() {
    if (local_)
        local_->ApplyPlayerSetConfig(config_);
}
