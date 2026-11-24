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

    // Step heights used by Entity::Move → StepSlideMove (binary set +0x138/+0x13c).
    // Defaults match ParamSet::ParamSet @ 0x5e000 (step≈0.501, smallStep≈0.0626).
    void SetStepSizes(float step, float smallStep) {
        stepSize_ = step;
        smallStepSize_ = smallStep;
    }

    bool OnGround() const { return onGround_; }
    int GetId() const { return id_; }

protected:
    // Solid for collision / ground (air, water, flowers, mushrooms, leaves are not).
    static bool IsCollisionSolid(unsigned char t);

    // True if player AABB at `pos` (feet origin) intersects any solid cell.
    bool CollidesAt(const Vector& pos) const;

    // Highest solid top (cellY+1) under the footprint in (pos.y - maxDrop, pos.y].
    bool FindSupportY(const Vector& pos, float maxDrop, float& outY) const;

    // Axis-separated slide. If already stuck in solid, still allows motion so the
    // player can walk/jump out.
    bool TryMoveAxis(Vector& pos, float d, int axis) const;

    // SlideMove stand-in (axis-separated). Sets blockedHoriz if full dx/dz not taken.
    bool SlideMoveAxes(Vector& pos, float dx, float dy, float dz, bool& blockedHoriz) const;

    // StepSlideMove @ 0xe0e80 stand-in. Writes final pose to out; returns horiz²
    // travel from start. outLanded = true when the step-down hit a surface.
    float TryStepSlide(Vector& out, const Vector& start, const Vector& desiredEnd,
                       float step, bool& outLanded) const;

    EntitySet* set_ = nullptr;
    GridData* grid_ = nullptr;
    Vector position_{0, 0, 0};
    Vector velocity_{0, 0, 0};
    // AABB size from LocalBBox (width, height, depth)
    Vector size_{0.49f, 1.75f, 0.49f};
    bool onGround_ = false;
    float stepSize_ = 0.501f;
    float smallStepSize_ = 0.062625f;
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
    // Binary AddOrDeleteBlock @ 0x5cfb0 — queues action only (no SetCell here).
    // deleteMode true → break; false → place with placeType (MtlSelIndex resolved by caller).
    // activateMode → mode 3 (ActivateCell). Cooldown via operationTimeMs.
    void AddOrDeleteBlock(bool deleteMode, unsigned char placeType, bool activateMode = false);

    // Apply Scene EntityPlayerSet config (ParamSets, BBox, reach, eye height)
    void ApplyPlayerSetConfig(const SceneEntityPlayerSetConfig& cfg);

    // EntityPlayer::TracePoint @ 0x5cdd0 — Clear(+0xf0) then ray every frame
    void TracePoint();
    // Hit cell from last TracePoint (binary +0x100)
    int GetHitCellIndex() const { return hitTrace_.cellIndex; }
    const GridDataTraceResult& GetHitTrace() const { return hitTrace_; }

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
    int operationTimeMs_ = 250; // EntityPlayerSet +0x1ac / scene OperationTime
    // Horizontal velocity for walk/swim/ladder (binary-style scalar along intent)
    float horizSpeed_ = 0.f;
    // EntityPlayer +0xa8 / +0xac / +0xb0 — fly-mode axis speeds (UpdateMovement mode 3)
    // Integrated as: delta = (right*strafe + up*vertical + forward*forward) * dt
    float flyVelForward_ = 0.f; // +0xa8
    float flyVelUp_ = 0.f;      // +0xac
    float flyVelStrafe_ = 0.f;  // +0xb0
    PlayerParamSet paramSets_[4];
    bool flipX_ = false;
    bool flipY_ = false;

    // --- Binary deferred block action (EntityPlayer +0xf0..+0x126) ---
    // TracePoint writes hitTrace_ (+0xf0); cellIndex at +0x100.
    GridDataTraceResult hitTrace_;
    // +0x118 cooldown deadline (ms clock); 0 = ready
    int actionCooldownUntilMs_ = 0;
    // +0x11c: 0=none, 1=break/SAU-place, 2=place, 3=activate
    int pendingAction_ = 0;
    // +0x120 cell index, +0x124 type, +0x126 state
    int pendingCellIndex_ = -1;
    unsigned char pendingType_ = 0;
    unsigned short pendingState_ = 0;
    // Frame clock for cooldown (ms); advanced by FixedUpdate
    int actionClockMs_ = 0;
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
    // EntityPlayerSet +0x1a4 — DrawCursor color (ctor: 0xFFFFFFFF white)
    uint32_t GetCursorColor() const { return cursorColor_; }
    void SetCursorColor(uint32_t c) { cursorColor_ = c; }
    int GetOperationTimeMs() const { return config_.operationTime; }
    int GetDxThreshold() const { return config_.dxThreshold; }
    int GetDyThreshold() const { return config_.dyThreshold; }
    const PlayerParamSet& GetParamSet(int i) const {
        return config_.paramSets[(i >= 0 && i < 4) ? i : 0];
    }

    // Binary input flags +0x262/+0x263/+0x264 (HandleTouch / HandleMessage)
    void SetPlaceAction(bool v) { flagPlace_ = v; }
    void SetBreakAction(bool v) { flagBreak_ = v; }
    void SetActivateAction(bool v) { flagActivate_ = v; }
    bool GetPlaceAction() const { return flagPlace_; }
    bool GetBreakAction() const { return flagBreak_; }
    bool GetActivateAction() const { return flagActivate_; }
    void ClearActionFlags() { flagPlace_ = flagBreak_ = flagActivate_ = false; }

private:
    EntityPlayer* local_ = nullptr;
    SceneEntityPlayerSetConfig config_;
    bool flying_ = false;
    bool flagPlace_ = false;    // +0x262
    bool flagBreak_ = false;    // +0x263
    bool flagActivate_ = false; // +0x264
    // Binary EntityPlayerSet ctor: *(this+0x1a4) = 0xffffffff
    uint32_t cursorColor_ = 0xFFFFFFFFu;
};
