#pragma once
#include "Vector.h"
#include "GridData.h"
#include "Scene.h"
#include <cstdint>

class EntitySet;

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

    virtual void Move(const Vector& delta);
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
    // AABB size from LocalBBox (width, height, depth)
    Vector size_{0.49f, 1.75f, 0.49f};
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
    void UpdateRotation(float lookDeltaYaw, float lookDeltaPitch);
    void UpdateCameraTransform(class Camera& camera);
    void UpdateCamera();

    void SetYawPitch(float yaw, float pitch);
    float GetYaw() const { return yaw_; }
    float GetPitch() const { return pitch_; }

    void SetMoveInput(float forward, float strafe, bool jump);
    Vector GetEyePosition() const;
    Vector GetLookDirection() const;
    void GetBasis(Vector& outRight, Vector& outUp, Vector& outForward) const;
    void AddOrDeleteBlock(bool deleteMode, unsigned char placeType);

    // Apply Scene EntityPlayerSet config (ParamSets, BBox, reach, eye height)
    void ApplyPlayerSetConfig(const SceneEntityPlayerSetConfig& cfg);

    float GetOperationDistance() const { return operationDistance_; }
    float GetEyeHeight() const { return eyeHeight_; }
    const PlayerParamSet& CurrentParamSet() const;

    // Binary mode @ +0xa4 (UpdateMode @ 0x5b040): 0 walk, 1 swim, 2 ladder, 3 fly.
    // ParamSets[i] is selected by this index — NOT "sprint".
    enum class Mode { Walk = 0, Swim = 1, Ladder = 2, Fly = 3 };

private:
    float yaw_ = 0.f;
    float pitch_ = 0.f;
    float eyeHeight_ = 1.75f;
    float moveForward_ = 0.f;
    float moveStrafe_ = 0.f;
    bool jumpPressed_ = false;
    Mode mode_ = Mode::Walk;
    float operationDistance_ = 8.f;
    // Horizontal velocity for accel/decel (SimplePlayer::Update uses ParamSet accel)
    float horizSpeed_ = 0.f;
    PlayerParamSet paramSets_[4];
    bool flipX_ = false;
    bool flipY_ = false;
};

void GetRotationVectors(Vector& outRight, Vector& outUp, Vector& outForward,
                        float yawDegrees, float pitchDegrees);

class EntitySet {
public:
    virtual ~EntitySet() = default;
};

// Mirrors binary EntityPlayerSet: owns ParamSets, camera link, local player, touch params
class EntityPlayerSet : public EntitySet {
public:
    EntityPlayerSet();

    EntityPlayer* GetLocalPlayer() const { return local_; }
    void SetLocalPlayer(EntityPlayer* p) { local_ = p; }

    void SetConfig(const SceneEntityPlayerSetConfig& cfg);
    const SceneEntityPlayerSetConfig& GetConfig() const { return config_; }

    void ApplyToLocalPlayer();

    // Binary GetFlying / SetFlying @ 0x5e1d0 / 0x5e1e0 — drives mode 3 in UpdateMode
    bool GetFlying() const { return flying_; }
    void SetFlying(bool f) { flying_ = f; }

    float GetTouchSensitivity() const { return config_.touchSensitivity; }
    float GetOperationDistance() const { return config_.operationDistance; }
    int GetOperationTimeMs() const { return config_.operationTime; }
    int GetDxThreshold() const { return config_.dxThreshold; }
    int GetDyThreshold() const { return config_.dyThreshold; }
    const PlayerParamSet& GetParamSet(int i) const {
        return config_.paramSets[(i >= 0 && i < 4) ? i : 0];
    }

private:
    EntityPlayer* local_ = nullptr;
    SceneEntityPlayerSetConfig config_;
    bool flying_ = false;
};
