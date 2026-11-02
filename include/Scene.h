#pragma once
#include "Vector.h"
#include "GridData.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// Observed: LoadSceneObjects(char const*, vector<SceneObject*>&, Properties&, Scene*)
// Specializations: UISceneObject, GridModel, Camera, EntityPlayerSet, SkyBox, Clouds

struct Properties {
    std::map<std::string, std::string> values;
    bool Has(const std::string& key) const { return values.count(key) != 0; }
    const std::string& Get(const std::string& key, const std::string& def = "") const {
        auto it = values.find(key);
        return it != values.end() ? it->second : def;
    }
};

class SceneObject {
public:
    enum class Kind {
        Base = 0, UI = 1, GridModel = 2, Camera = 3,
        EntityPlayerSet = 4, SkyBox = 5, Clouds = 6
    };
    virtual Kind GetKind() const { return Kind::Base; }
    int id = 0;
    std::string name;
    Vector position;
    Vector scale{1, 1, 1};
    bool visible = true;
    int updateOrder = 0;
    int renderOrder = 0;
    Properties props;
    virtual ~SceneObject() = default;
    virtual void LoadResources() {}
    virtual void FreeResources() {}
    virtual void Update() {}
    virtual void Render3D() {}
    virtual void RenderUI() {}
    virtual void SetProperties(Properties&) {}
    virtual void GetProperties(Properties&) {}
};

class UISceneObject : public SceneObject {
public:
    Kind GetKind() const override { return Kind::UI; }
    std::string fileName;
};

// Camera section — binary SetProperties @ 0x4eeb0 uses FieldOfView, NearPlane, FarPlane, ...
struct SceneCameraConfig {
    bool present = false;
    int id = 0;
    Vector position{0.5f, 65.5f, 0.5f};
    bool perspective = true;
    float fieldOfView = 90.f;
    float nearPlane = 0.025f;
    float farPlane = 350.f;
    float viewportX0 = 0.f, viewportY0 = 0.f, viewportX1 = 1.f, viewportY1 = 1.f;
    bool sizeFromViewport = true;
    bool clearColor = true;
    bool clearDepth = true;
    float clearR = 0.f, clearG = 0.f, clearB = 0.f, clearA = 1.f;
};

// EntityPlayerSet::ParamSet — binary ctor @ 0x5e000 (8 floats / 32 B)
// Defaults: speed=3, accel=9, decel=18, step=0.501, small=0.0626, grav=-9.81, limit=-50, jump=5
struct PlayerParamSet {
    // Explicit ctor required for C++11 (NDK): default member initializers
    // make the type a non-aggregate, so PlayerParamSet{a,b,...} needs a ctor.
    // Defaults match binary ParamSet::ParamSet @ 0x5e000.
    PlayerParamSet(
        float maxSpeed = 3.f,
        float accel = 9.f,
        float decel = 18.f,
        float step = 0.501f,
        float smallStep = 0.062625f,
        float grav = -9.81f,
        float gravLimit = -50.f,
        float jumpSpeed = 5.f)
        : maximumSpeed(maxSpeed)
        , acceleration(accel)
        , deceleration(decel)
        , stepSize(step)
        , smallStepSize(smallStep)
        , gravity(grav)
        , gravityLimit(gravLimit)
        , jump(jumpSpeed)
    {}

    float maximumSpeed;
    float acceleration;
    float deceleration;
    float stepSize;
    float smallStepSize;
    float gravity;
    float gravityLimit;
    float jump;
};

// ParamSet index == EntityPlayer mode @ +0xa4 (UpdateMode @ 0x5b040):
//   [0] walk, [1] swim (IsUnderwater), [2] ladder (OnLadder), [3] fly (SetFlying)
enum class PlayerParamMode : int {
    Walk = 0, Swim = 1, Ladder = 2, Fly = 3, Count = 4
};

struct SceneEntityPlayerSetConfig {
    bool present = false;
    int id = 0;
    int cameraObjectId = 1;
    int gridModelObjectId = 5;
    Vector localBBoxMin{-0.245f, -1.5f, -0.245f};
    Vector localBBoxMax{0.245f, 0.25f, 0.245f};
    Vector collisionBBoxMin{-0.245f, -1.5f, -0.245f};
    Vector collisionBBoxMax{0.245f, 0.25f, 0.245f};
    float operationDistance = 8.f;
    int operationTime = 300;
    float touchSensitivity = 0.33f;
    bool flipXRotation = false;
    bool flipYRotation = false;
    int dxThreshold = 200;
    int dyThreshold = 200;
    PlayerParamSet paramSets[4];

    // Feet at local Min.y, head at Max.y → eye height from feet ≈ span
    float EyeHeightFromBBox() const {
        return localBBoxMax.y - localBBoxMin.y;
    }
    float BodyWidth() const {
        return localBBoxMax.x - localBBoxMin.x;
    }
    float BodyHeight() const {
        return localBBoxMax.y - localBBoxMin.y;
    }
};

struct SceneGlobalsConfig {
    float timeMultiplier = 1.f;
    int maxTimestep = 200;
    Vector worldSizeMin{-576.f, -64.f, -576.f};
    Vector worldSizeMax{576.f, 192.f, 576.f};
    float clearR = 1.f, clearG = 1.f, clearB = 180.f / 255.f, clearA = 140.f / 255.f;
    bool hasClearColor = false;
    std::string texAtlas0;
};


// GridModel section — SetProperties @ 0xa53a0; LoadResources → LoadTextureEffects + LoadTextures
// Property names from .rodata: BlocksTextureFileName, WaterTextureFileName, LavaTextureFileName,
//   MtlDescFileName, WaterPlaneColor, LavaPlaneColor, WaterFlowSpeed, LavaFlowSpeed,
//   UseProceduralTextures, WaterPaletteFileName, LavaPaletteFileName
struct SceneGridModelConfig {
    bool present = false;
    int id = 0;
    int cameraObjectId = 1;
    std::string blocksTextureFileName = "textures/blocks.png";
    std::string waterTextureFileName = "textures/water.png";
    std::string lavaTextureFileName = "textures/lava.png";
    std::string mtlDescFileName = "scenes/blocks.txt";
    // Color as 0..1 RGBA (scene Color = A R G B with A first)
    float waterPlaneR = 232.f/255.f, waterPlaneG = 144.f/255.f, waterPlaneB = 5.f/255.f, waterPlaneA = 64.f/255.f;
    float lavaPlaneR = 0.f, lavaPlaneG = 50.f/255.f, lavaPlaneB = 204.f/255.f, lavaPlaneA = 204.f/255.f;
    float waterFlowSpeed = 0.625f;  // start.scene; drives FluidAnim UV scroll
    float lavaFlowSpeed = 0.15f;
    bool useProceduralTextures = true;
    std::string waterPaletteFileName = "textures/water_pal.png";
    std::string lavaPaletteFileName = "textures/lava_pal.png";
};


// SkyBox — SetProperties @ 0xddea0; windows drive dayFactor in SetTimeOfDay @ 0xde770
struct SceneSkyBoxConfig {
    bool present = false;
    int id = 0;
    float sunriseBegin = 0.208333f;
    float sunriseEnd   = 0.270833f;
    float sunsetBegin  = 0.729167f;
    float sunsetEnd    = 0.791667f;
    // Colors 0..1 (scene Color = A R G B)
    float dayLowerR = 1.f, dayLowerG = 207.f/255.f, dayLowerB = 167.f/255.f;
    float dayUpperR = 1.f, dayUpperG = 168.f/255.f, dayUpperB = 115.f/255.f;
    float nightLowerR = 19.f/255.f, nightLowerG = 12.f/255.f, nightLowerB = 9.f/255.f;
    float nightUpperR = 10.f/255.f, nightUpperG = 6.f/255.f, nightUpperB = 4.f/255.f;
    bool horizon = true;
    std::string sunTextureFileName = "textures/sun.png";
    std::string moonTextureFileName = "textures/moon.png";
    float sunScale = 3.f;
    float moonScale = 3.f;
    int numStars = 600;
    int starsSeed = 1;
};

class Scene {
public:
    SceneGlobalsConfig globals;
    SceneCameraConfig camera;
    SceneEntityPlayerSetConfig playerSet;
    SceneGridModelConfig gridModel;
    SceneSkyBoxConfig skyBox;

    std::vector<SceneObject*> objects;
    GridData* grid = nullptr;

    // Mobile-safe active world (XZ). Full scene bounds stay in globals.
    static constexpr float kMobileWorldHalfExtent = 64.f;
    static constexpr float kMobileWorldMinY = -16.f;
    static constexpr float kMobileWorldMaxY = 96.f;

    bool Load(const std::string& path);
    void ApplyGlobalsToGrid(GridData& g, bool mobileClamp) const;

    // Convenience for older call sites
    Vector WorldSizeMin() const { return globals.worldSizeMin; }
    Vector WorldSizeMax() const { return globals.worldSizeMax; }
    float TimeMultiplier() const { return globals.timeMultiplier; }
};
