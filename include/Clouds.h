#pragma once
#include "Mesh.h"
#include "Texture.h"
#include "Vector.h"
#include <string>
#include <vector>
#include <cstdint>

struct SceneCloudsConfig;

// Clouds from Ghidra CreateBitmap / UpdateMesh / Render3D:
// - occupancy from clouds.png bright/white pixels (signed byte < 0 / >=128)
// - black pixels = empty sky gaps
// - each cell tinted by that pixel's RGB; UV samples that pixel
// - offset floats +0x174/+0x178 scroll continuously (wind) — mesh moves smoothly
class Clouds {
public:
    Clouds() = default;

    void LoadResources(const char* texturePath = "textures/clouds.png");
    void ApplyConfig(const SceneCloudsConfig& cfg);

    void SetEnabled(bool e) { enabled_ = e; }
    bool IsEnabled() const { return enabled_; }
    void SetSkyLightScale(float s) { skyLightScale_ = s; }
    float GetSkyLightScale() const { return skyLightScale_; }

    void Update(float dt);
    // centerX/Z = camera; always rebuilds with continuous wind offset (no teleport)
    void UpdateMesh(float centerX, float centerZ);
    const MeshBuffer& GetMesh() const { return mesh_; }
    Texture& GetTexture() { return texture_; }
    void Bind() { texture_.Bind(); }

private:
    bool enabled_ = true;
    float offsetU_ = 0.f, offsetV_ = 0.f; // continuous scroll (cells)
    float windX_ = 0.05f, windY_ = 0.f, windZ_ = 0.f;
    float skyLightScale_ = 1.f;

    float baseY_ = 129.5f;
    float cellSX_ = 12.f, cellSY_ = 5.f, cellSZ_ = 12.f;
    int radius_ = 20;

    int mapW_ = 0, mapH_ = 0;
    std::vector<uint32_t> bits_;
    std::vector<uint8_t> cellR_, cellG_, cellB_; // per-pixel colour from bitmap
    Texture texture_;
    MeshBuffer mesh_;

    bool Occupied(int mx, int mz) const;
    void GetCellRGB(int mx, int mz, uint8_t& r, uint8_t& g, uint8_t& b) const;
    void PixelUV(int mx, int mz, float& u, float& v) const;
};
