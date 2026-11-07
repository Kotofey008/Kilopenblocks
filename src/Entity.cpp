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

// ---------------------------------------------------------------------------
// Collision solid test — shared by CheckGround / Move / StepSlideMove.
// Water, air, decorative plants and leaves do not block or support.
// ---------------------------------------------------------------------------
bool Entity::IsCollisionSolid(unsigned char t) {
    return t != BlockId::Air && t != BlockId::Water &&
           t != BlockId::Flower1 && t != BlockId::Mushroom1 &&
           t != BlockId::Mushroom2 && t != BlockId::Leaves1 &&
           t != BlockId::Leaves2;
}

// ---------------------------------------------------------------------------
// Entity::CheckGround @ 0x5a750 (191 B) — CONFIRMED from pdf + pdg
//
// Binary:
//   start = entity position (feet origin +0x18/+0x1c/+0x20)
//   end   = (x, y - 0.001, z)          // constant @ 0x107e28
//   Trace AABB (extents at set+0x11c/+0x128) start→end
//   onGround (+0x15) = (hit fraction < 1.0)
//
// Equivalent voxel test: any solid cell under the horizontal footprint at
// y = feetY - 0.001. NO long-range snap — only the 1mm probe matches binary.
// ---------------------------------------------------------------------------
bool Entity::CheckGround() {
    if (!grid_) { onGround_ = false; return false; }

    const float kGroundEps = 0.001f; // binary constant 0x107e28
    const float halfW = size_.x * 0.5f;
    const float halfD = size_.z * 0.5f;
    const float sampleY = position_.y - kGroundEps;
    const int y0 = (int)std::floor(sampleY);

    const int ix0 = (int)std::floor(position_.x - halfW);
    const int ix1 = (int)std::floor(position_.x + halfW - 1e-6f);
    const int iz0 = (int)std::floor(position_.z - halfD);
    const int iz1 = (int)std::floor(position_.z + halfD - 1e-6f);

    onGround_ = false;
    for (int bx = ix0; bx <= ix1 && !onGround_; ++bx) {
        for (int bz = iz0; bz <= iz1 && !onGround_; ++bz) {
            int idx = grid_->CoordToIndex(bx, y0, bz);
            if (idx < 0) continue;
            if (IsCollisionSolid(grid_->cellTypes[idx]))
                onGround_ = true;
        }
    }
    return onGround_;
}

bool Entity::CollidesAt(const Vector& pos) const {
    if (!grid_) return false;
    const float halfW = size_.x * 0.5f;
    const float halfD = size_.z * 0.5f;
    const float h = size_.y;
    // Feet exactly on a block top (integer Y) must NOT count the block below:
    // solid cell y occupies [y, y+1), top surface is y+1.0 → floor(feet)=y+1.
    const int x0 = (int)std::floor(pos.x - halfW);
    const int x1 = (int)std::floor(pos.x + halfW - 1e-6f);
    const int y0 = (int)std::floor(pos.y);
    const int y1 = (int)std::floor(pos.y + h - 1e-6f);
    const int z0 = (int)std::floor(pos.z - halfD);
    const int z1 = (int)std::floor(pos.z + halfD - 1e-6f);
    for (int bx = x0; bx <= x1; ++bx)
    for (int by = y0; by <= y1; ++by)
    for (int bz = z0; bz <= z1; ++bz) {
        int idx = grid_->CoordToIndex(bx, by, bz);
        if (idx < 0) continue;
        if (IsCollisionSolid(grid_->cellTypes[idx]))
            return true;
    }
    return false;
}

// Highest solid top under the footprint in (pos.y - maxDrop, pos.y].
// Block cell Y occupies [Y, Y+1); standing surface is Y+1.
bool Entity::FindSupportY(const Vector& pos, float maxDrop, float& outY) const {
    if (!grid_ || maxDrop <= 0.f) return false;
    const float halfW = size_.x * 0.5f;
    const float halfD = size_.z * 0.5f;
    const float minY = pos.y - maxDrop;
    const int ix0 = (int)std::floor(pos.x - halfW);
    const int ix1 = (int)std::floor(pos.x + halfW - 1e-6f);
    const int iz0 = (int)std::floor(pos.z - halfD);
    const int iz1 = (int)std::floor(pos.z + halfD - 1e-6f);
    // Cells whose tops can lie in (minY, pos.y]
    const int yLo = (int)std::floor(minY);
    const int yHi = (int)std::floor(pos.y - 1e-5f);
    float best = -1e30f;
    bool found = false;
    for (int by = yLo; by <= yHi; ++by) {
        const float top = (float)(by + 1);
        if (top <= minY + 1e-5f || top > pos.y + 1e-4f)
            continue;
        for (int bx = ix0; bx <= ix1; ++bx) {
            for (int bz = iz0; bz <= iz1; ++bz) {
                int idx = grid_->CoordToIndex(bx, by, bz);
                if (idx < 0) continue;
                if (IsCollisionSolid(grid_->cellTypes[idx])) {
                    if (top > best) best = top;
                    found = true;
                }
            }
        }
    }
    if (found) outY = best;
    return found;
}

bool Entity::TryMoveAxis(Vector& pos, float d, int axis) const {
    if (d == 0.f || !grid_) {
        if (axis == 0) pos.x += d;
        else if (axis == 1) pos.y += d;
        else pos.z += d;
        return true;
    }
    const bool stuck = CollidesAt(pos);
    auto at = [&](float t) {
        Vector p = pos;
        if (axis == 0) p.x += d * t;
        else if (axis == 1) p.y += d * t;
        else p.z += d * t;
        return p;
    };
    // Full step free, or already embedded → take the full delta (exit solid).
    if (!CollidesAt(at(1.f)) || stuck) {
        pos = at(1.f);
        return true;
    }
    // Blocked: binary-search the largest free fraction along this axis.
    // All-or-nothing rejection left the player floating a few mm above a
    // block top → onGround stayed false (jump delay / no jump after landings).
    float lo = 0.f, hi = 1.f;
    for (int i = 0; i < 12; ++i) {
        float mid = 0.5f * (lo + hi);
        if (CollidesAt(at(mid)))
            hi = mid;
        else
            lo = mid;
    }
    if (lo > 1e-5f) {
        pos = at(lo);
        return true; // partial move up to contact
    }
    return false;
}

// ---------------------------------------------------------------------------
// StepSlideMove @ 0xe0e80 — binary control flow (ghidra / pdg):
//
//   result = SlideMove(start → desiredEnd)
//   if (desiredEnd.y <= start.y) && (result blocked bit0) && stepHeight != 0:
//     upward Trace(start → start+(0,step,0)); require fraction ≥ 1 (clear)
//     elevStart = start + (0,step,0)
//     elevEnd   = desiredEnd + (0,step,0)
//     result = SlideMove(elevStart → elevEnd)
//     down Trace(result → (result.x, desiredEnd.y, result.z))
//     result.y = lerp(result.y, desiredEnd.y, downFraction)
//     if downFraction < 1: flags |= 2
//   write result to out (desiredEnd in binary is in/out)
//
// Entity::Move @ 0x5a810:
//   desired = pos + delta
//   r1 = StepSlideMove(..., stepSize, pos, desired)
//   if horiz(r1) < horiz(delta): r2 = StepSlideMove(..., smallStep, ...); pick better
//   pos = chosen
//   (velocity zero for floor/ceiling is done by caller via flags / vertical shortfall)
//
// Voxel stand-ins: axis TryMoveAxis for SlideMove; CollidesAt for Trace clear/hit.
// ---------------------------------------------------------------------------

// Axis-separated stand-in for SlideMove. Returns true if horizontal motion was
// blocked (could not take full dx/dz) — corresponds to binary blocked bit 0.
bool Entity::SlideMoveAxes(Vector& pos, float dx, float dy, float dz,
                           bool& blockedHoriz) const {
    blockedHoriz = false;
    const float x0 = pos.x, z0 = pos.z;
    TryMoveAxis(pos, dx, 0);
    TryMoveAxis(pos, dy, 1);
    TryMoveAxis(pos, dz, 2);
    const float gotX = pos.x - x0;
    const float gotZ = pos.z - z0;
    // Incomplete horizontal progress → blocked (bit 0).
    if (dx * dx + dz * dz > 1e-12f) {
        const float want2 = dx * dx + dz * dz;
        const float got2 = gotX * gotX + gotZ * gotZ;
        if (got2 + 1e-8f < want2)
            blockedHoriz = true;
    }
    return true;
}

// One full StepSlideMove for a given step height. Writes final position to out.
// Returns squared horizontal distance travelled from start (for Move's picker).
// outLanded: true if the step-down trace hit (fraction < 1) — floor contact.
float Entity::TryStepSlide(Vector& out, const Vector& start, const Vector& desiredEnd,
                           float step, bool& outLanded) const {
    outLanded = false;
    const float dx = desiredEnd.x - start.x;
    const float dy = desiredEnd.y - start.y;
    const float dz = desiredEnd.z - start.z;

    // --- first SlideMove(start → desiredEnd) ---
    Vector result = start;
    bool blocked = false;
    SlideMoveAxes(result, dx, dy, dz, blocked);

    // Binary gate: (desiredEnd.y <= start.y) && blocked && step != 0
    if (desiredEnd.y <= start.y && blocked && step > 0.f) {
        // Upward must be fully clear from start (fraction ≥ 1).
        Vector elevStart = start;
        elevStart.y += step;
        if (!CollidesAt(elevStart)) {
            Vector elevEnd = desiredEnd;
            elevEnd.y += step;

            // Slide at elevated height.
            Vector elev = elevStart;
            bool elevBlocked = false;
            SlideMoveAxes(elev, elevEnd.x - elevStart.x, elevEnd.y - elevStart.y,
                          elevEnd.z - elevStart.z, elevBlocked);
            (void)elevBlocked;

            // Downward toward original desiredEnd.y (binary low target).
            // Voxel: binary-search free y in [desiredEnd.y, elev.y].
            const float highY = elev.y;
            const float lowY = desiredEnd.y;
            Vector landed = elev;
            if (highY > lowY + 1e-6f) {
                Vector atLow = elev;
                atLow.y = lowY;
                if (!CollidesAt(atLow)) {
                    // Full clear down to desiredEnd.y — no contact.
                    landed.y = lowY;
                    outLanded = false;
                } else {
                    // Hit: approach contact from free side.
                    float lo = 0.f, hi = 1.f; // 0 = highY, 1 = lowY
                    for (int i = 0; i < 12; ++i) {
                        float mid = 0.5f * (lo + hi);
                        Vector t = elev;
                        t.y = highY + (lowY - highY) * mid;
                        if (CollidesAt(t))
                            hi = mid;
                        else
                            lo = mid;
                    }
                    landed.y = highY + (lowY - highY) * lo;
                    outLanded = true; // fraction < 1
                }
            } else {
                // elevated result already at or below low target
                if (CollidesAt(landed)) {
                    // should not happen if elev slide was free; reject step path
                } else {
                    outLanded = false;
                }
            }

            if (!CollidesAt(landed)) {
                result = landed;
            }
            // else keep first SlideMove result
        }
    }

    out = result;
    const float hx = result.x - start.x;
    const float hz = result.z - start.z;
    return hx * hx + hz * hz;
}

void Entity::Move(const Vector& delta) {
    if (!grid_) {
        position_.x += delta.x;
        position_.y += delta.y;
        position_.z += delta.z;
        return;
    }

    // Mild unstick only: nudge up if the AABB is embedded (no long-range
    // FindSupportY teleport — that was pulling the player onto distant tops).
    if (CollidesAt(position_)) {
        for (float up = 0.02f; up <= 1.05f; up += 0.02f) {
            Vector t = position_;
            t.y += up;
            if (!CollidesAt(t)) {
                position_ = t;
                break;
            }
        }
    }

    const Vector start = position_;
    Vector desired = start;
    desired.x += delta.x;
    desired.y += delta.y;
    desired.z += delta.z;

    // Entity::Move: try large step, then small step if first horiz is worse than
    // desired horiz; pick the result with greater horizontal travel from start.
    bool landed1 = false, landed2 = false;
    Vector r1, r2;
    const float h1 = TryStepSlide(r1, start, desired, stepSize_, landed1);
    float bestH = h1;
    Vector best = r1;
    bool bestLanded = landed1;

    const float desiredH2 = delta.x * delta.x + delta.z * delta.z;
    if (desiredH2 > 1e-12f && h1 + 1e-8f < desiredH2) {
        const float h2 = TryStepSlide(r2, start, desired, smallStepSize_, landed2);
        if (h2 > bestH) {
            best = r2;
            bestH = h2;
            bestLanded = landed2;
        }
    }

    // Floor / ceiling contact from vertical shortfall (primary slide path).
    // Binary uses Move return flags; we approximate: vertical not fully applied.
    if (delta.y != 0.f) {
        const float intendedY = start.y + delta.y;
        if (std::fabs(best.y - intendedY) > 1e-4f)
            velocity_.y = 0.f;
    }
    // Step-down contact (binary flags bit 2): also kill vertical velocity.
    // Do NOT zero vy when step path left us in air (outLanded == false) —
    // that was the levitation bug (hover with vy=0 until gravity rebuilt).
    if (bestLanded)
        velocity_.y = 0.f;

    position_ = best;
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
    // Binary UpdateMode @ 0x5b040: IsUnderwater(entity position) — feet origin
    // (+0x18), not mid-body. Sampling eyeHeight*0.5 missed 1-block-deep water
    // (surface below chest) → Walk in a ford. Sample just above feet so any
    // water covering the lower body engages Swim; IsUnderwater still uses the
    // free-surface height (thin level-7 puddles stay Walk).
    Vector sample = position_;
    sample.y += 0.2f;
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
        // SimplePlayer mode==1 @ 0xdc9b0
        // Standing on solid while submerged: impulse jump (ParamSet.Jump).
        // Free water: hold jump → accel up toward MaximumSpeed.
        // Impulse only when not already rising (avoids re-boost every frame).
        if (onGround_ && jumpPressed_ && velocity_.y <= 0.f) {
            velocity_.y = ps.jump;
            onGround_ = false;
        } else if (jumpPressed_ && !onGround_) {
            float vy = velocity_.y + ps.acceleration * dt;
            if (vy > ps.maximumSpeed)
                vy = ps.maximumSpeed;
            velocity_.y = vy;
        } else if (!onGround_) {
            velocity_.y += ps.gravity * dt;
        } else {
            velocity_.y = std::min(velocity_.y, 0.f);
        }
        if (velocity_.y < ps.gravityLimit) {
            float pulled = velocity_.y + ps.deceleration * ps.deceleration * dt;
            if (pulled > ps.gravityLimit)
                pulled = ps.gravityLimit;
            velocity_.y = pulled;
        }
    } else {
        // Walk — SimplePlayer mode 0:
        //   airborne: integrate gravity
        //   on ground + jump: set vy = Jump (hold-to-rejump on land is OK when
        //     CheckGround is the 1mm probe only — no long-range surface snap)
        // Guard velocity_.y <= 0 so a same-frame onGround flicker cannot stack
        // jump impulses into levitation.
        if (!onGround_) {
            velocity_.y += ps.gravity * dt;
            if (velocity_.y < ps.gravityLimit)
                velocity_.y = ps.gravityLimit;
        } else if (jumpPressed_ && velocity_.y <= 0.f) {
            velocity_.y = ps.jump;
            onGround_ = false;
        } else if (onGround_) {
            velocity_.y = std::min(velocity_.y, 0.f);
        }
    }

    // 3) Horizontal — MaximumSpeed + Acceleration / Deceleration
    //    Confirmed in SimplePlayer::Update @ 0xdc9b0:
    //      +0x1f0 multiplies GetRotationVectors *right*
    //      +0x1f8 multiplies GetRotationVectors *forward*
    //    MoveRight accelerates +0x1f0 (positive along right).
    //    GetRotationVectors (0xb7190): at yaw=0, right=(-1,0,0), forward=(0,0,+1).
    //    Therefore +strafe must follow right, not -right:
    //      intent = forward * moveForward + right * moveStrafe
    //      ix = (-sinY)*fwd + (-cosY)*strafe
    //      iz = ( cosY)*fwd + (-sinY)*strafe
    float yawRad = yaw_ * 3.14159265f / 180.f;
    float sinY = std::sin(yawRad), cosY = std::cos(yawRad);
    float ix = (-sinY) * moveForward_ + (-cosY) * moveStrafe_;
    float iz = ( cosY) * moveForward_ + (-sinY) * moveStrafe_;
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

    // Entity::Move / StepSlideMove reads step from EntitySet +0x138/+0x13c;
    // ParamSet +0xc/+0x10 (binary defaults ≈0.501 / 0.0626). Push current set.
    SetStepSizes(ps.stepSize, ps.smallStepSize);
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
