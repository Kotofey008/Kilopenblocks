#include "Entity.h"
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
    pitch_ = std::max(-89.f, std::min(89.f, pitch));
}

void EntityPlayer::SetMoveInput(float forward, float strafe, bool jump) {
    moveForward_ = forward;
    moveStrafe_ = strafe;
    jumpPressed_ = jump;
}

void EntityPlayer::UpdateMode() {
    // original switches walk/fly; keep simple
}

void EntityPlayer::UpdateRotation() {
    // yaw/pitch already set from input; original may smooth
}

void EntityPlayer::UpdateCamera() {
    // Camera follows player eye position — implemented by caller using GetTransform
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
    float fx = -std::sin(yawRad) * moveForward_ + std::cos(yawRad) * moveStrafe_;
    float fz =  std::cos(yawRad) * moveForward_ + std::sin(yawRad) * moveStrafe_;
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
    Vector eye = position_;
    eye.y += eyeHeight_;
    return Matrix4::Translation(eye);
}

Vector EntityPlayer::GetEyePosition() const {
    return Vector(position_.x, position_.y + eyeHeight_, position_.z);
}
Vector EntityPlayer::GetLookDirection() const {
    float yr = yaw_ * 3.14159265f / 180.f;
    float pr = pitch_ * 3.14159265f / 180.f;
    float cp = std::cos(pr);
    return Vector(-std::sin(yr) * cp, std::sin(pr), std::cos(yr) * cp);
}
