#pragma once
#include "Vector.h"
#include "GridData.h"
#include <cstdint>

class EntitySet;

// Base Entity — methods recovered from symbols:
// Entity(EntitySet*), Init, Update, FixedUpdate, FixedPostUpdate
// Move(Vector const&), CheckGround(), GetTransform(int), Load/Save(Stream)

class Entity {
public:
    explicit Entity(EntitySet* set = nullptr);
    virtual ~Entity();

    virtual void Init();
    virtual void Update();
    virtual void FixedUpdate();
    virtual void FixedPostUpdate();
    virtual void Load(class Stream& s, int version);
    virtual void Save(class Stream& s);

    // Move with collision against GridData (observed size 649)
    virtual void Move(const Vector& delta);
    // Ground check (size 191)
    virtual bool CheckGround();

    virtual Matrix4 GetTransform(int index = 0);

    void SetPosition(const Vector& p) { position_ = p; }
    const Vector& GetPosition() const { return position_; }
    void SetVelocity(const Vector& v) { velocity_ = v; }
    const Vector& GetVelocity() const { return velocity_; }
    void SetGrid(GridData* g) { grid_ = g; }

    bool OnGround() const { return onGround_; }
    int GetId() const { return id_; }

protected:
    EntitySet* set_ = nullptr;
    GridData* grid_ = nullptr;
    Vector position_{0, 0, 0};
    Vector velocity_{0, 0, 0};
    Vector size_{0.6f, 1.8f, 0.6f}; // approximate player AABB
    bool onGround_ = false;
    int id_ = 0;
    static int g_FreeEntityID;
};

class EntityPlayerSet;

class EntityPlayer : public Entity {
public:
    explicit EntityPlayer(EntityPlayerSet* set = nullptr);
    ~EntityPlayer() override;

    void Init() override;
    void FixedUpdate() override;
    void FixedPostUpdate() override;
    Matrix4 GetTransform(int index = 0) override;

    void UpdateMode();
    void UpdateRotation();
    void UpdateCamera();

    void SetYawPitch(float yaw, float pitch);
    float GetYaw() const { return yaw_; }
    float GetPitch() const { return pitch_; }

    // Input-driven motion (set each frame from InputMgr)
    void SetMoveInput(float forward, float strafe, bool jump);
    Vector GetEyePosition() const;
    Vector GetLookDirection() const;

    enum class Mode { Walking = 0, Flying = 1, Spectator = 2 };

private:
    float yaw_ = 0.f;
    float pitch_ = 0.f;
    float eyeHeight_ = 1.62f;
    float moveForward_ = 0.f;
    float moveStrafe_ = 0.f;
    bool jumpPressed_ = false;
    Mode mode_ = Mode::Walking;
    float walkSpeed_ = 4.3f;   // typical voxel walk
    float flySpeed_ = 8.f;
    float gravity_ = -20.f;
    float jumpSpeed_ = 8.f;
};

class EntitySet {
public:
    virtual ~EntitySet() = default;
};

class EntityPlayerSet : public EntitySet {
public:
    EntityPlayer* GetLocalPlayer() const { return local_; }
    void SetLocalPlayer(EntityPlayer* p) { local_ = p; }
private:
    EntityPlayer* local_ = nullptr;
};
