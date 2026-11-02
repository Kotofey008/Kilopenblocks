#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Texture loading mirrors original lodepng + glTexImage2D path
// (GridModel::LoadTextures @ 0xa7d50, TexAtlas system present)
class Texture {
public:
    Texture() = default;
    ~Texture();

    bool LoadPNG(const std::string& path);
    bool LoadPNGFromMemory(const uint8_t* data, size_t size);
    bool LoadPNGAsset(const char* assetPath);
    void Bind() const;
    void Unbind() const;
    unsigned Width() const { return width_; }
    unsigned Height() const { return height_; }
    unsigned GLId() const { return glId_; }
    bool Valid() const { return !pixels_.empty(); }
    const uint8_t* Pixels() const { return pixels_.data(); }

    // Atlas helpers: tile grid for blocks.png (256x256 → 16x16 tiles of 16px)
    static constexpr int kAtlasTiles = 16;
    static void TileUV(int tileU, int tileV, float& u0, float& v0, float& u1, float& v1);

private:
    unsigned width_ = 0, height_ = 0;
    unsigned glId_ = 0;
    std::vector<uint8_t> pixels_; // RGBA8888
    bool UploadGL();
};

// Get face UV from BlockDef: face 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
struct BlockDef;
void GetFaceUV(const BlockDef& def, int face, float& u0, float& v0, float& u1, float& v1);

// Animated water: Game::EnableAnimatedWater / GridModel::SetAnimatedWater
// Scrolls UV over time for water (block 5) and lava tiles
struct FluidAnim {
    float waterOffsetU = 0.f;
    float waterOffsetV = 0.f;
    float lavaOffsetU = 0.f;
    // From GridModel WaterFlowSpeed / LavaFlowSpeed (start.scene defaults)
    float waterFlowSpeed = 0.625f;
    float lavaFlowSpeed = 0.15f;
    bool enabled = true;
    void SetFlowSpeeds(float water, float lava) {
        waterFlowSpeed = water;
        lavaFlowSpeed = lava;
    }
    void Update(float dt) {
        if (!enabled) return;
        // UV offsets advanced by GridModel WaterFlowSpeed / LavaFlowSpeed (scene units/sec).
        // Exact binary mapping of speed→atlas UV is not fully recovered; values from
        // start.scene replace previous hardcodes so they control animation rate.
        waterOffsetU += dt * waterFlowSpeed;
        waterOffsetV += dt * waterFlowSpeed * 0.6f;
        lavaOffsetU  += dt * lavaFlowSpeed;
        if (waterOffsetU > 1.f) waterOffsetU -= 1.f;
        if (waterOffsetV > 1.f) waterOffsetV -= 1.f;
        if (lavaOffsetU > 1.f) lavaOffsetU -= 1.f;
    }
};
