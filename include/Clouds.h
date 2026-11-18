#pragma once
#include "Mesh.h"
#include "Texture.h"
#include "Vector.h"
#include <string>
#include <vector>
#include <cstdint>

// Clouds recovered from Ghidra:
//   CreateBitmap — load clouds.png → occupancy bitfield (high-bit / bright pixels)
//   CreateTexture — soft radial alpha (simplified: key-black on same bitmap)
//   UpdateMesh / UpdateCellMesh — voxel faces in Radius around camera
//   Scene: Position.y=129.5, Scale=12 5 12, Radius=20, Wind=(0.05,0,0)
class Clouds {
public:
    Clouds() = default;

    void LoadResources(const char* texturePath = "textures/clouds.png");
    void ApplyScene(float posY, float scaleX, float scaleY, float scaleZ,
                    int radius, float windX, float windY, float windZ,
                    const std::string& bitmapPath);

    void SetEnabled(bool e) { enabled_ = e; }
    bool IsEnabled() const { return enabled_; }
    void SetOffset(float u, float v) { offsetU_ = u; offsetV_ = v; }
    void GetOffset(float& u, float& v) const { u = offsetU_; v = offsetV_; }
    void SetSkyLightScale(float s) { skyLightScale_ = s; }
    float GetSkyLightScale() const { return skyLightScale_; }

    void Update(float dt);
    // Rebuild voxel mesh centered on world XZ (player eye)
    void UpdateMesh(float centerX, float centerZ);
    const MeshBuffer& GetMesh() const { return mesh_; }
    Texture& GetTexture() { return texture_; }
    void Bind() { texture_.Bind(); }

private:
    bool enabled_ = true;
    float offsetU_ = 0.f, offsetV_ = 0.f; // wind scroll in cell units
    float windX_ = 0.05f, windY_ = 0.f, windZ_ = 0.f;
    float skyLightScale_ = 1.f;

    float baseY_ = 129.5f;       // scene Position.y
    float cellSX_ = 12.f;        // Scale.x
    float cellSY_ = 5.f;         // Scale.y
    float cellSZ_ = 12.f;        // Scale.z
    int radius_ = 20;            // +0x130

    int mapW_ = 0, mapH_ = 0;
    std::vector<uint32_t> bits_; // occupancy, (mapW*mapH+31)/32 words
    Texture texture_;
    MeshBuffer mesh_;
    int lastCellX_ = 0x7fffffff, lastCellZ_ = 0x7fffffff;

    bool Occupied(int mx, int mz) const;
    void EmitBox(MeshBuffer& out, float x0, float y0, float z0,
                 float x1, float y1, float z1, uint32_t col);
};
