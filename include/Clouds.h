#pragma once
#include "Mesh.h"
#include "Texture.h"
#include "Vector.h"

// Clouds system recovered from symbols:
// Clouds::Mesh::Create(int,int), UpdateMesh, SetOffset(float,float),
// GetSkyLightScale, CreateTexture/LoadResources (clouds.png 256x256)

class Clouds {
public:
    Clouds() = default;
    void LoadResources(const char* texturePath = "assets/textures/clouds.png");
    void SetEnabled(bool e) { enabled_ = e; }
    bool IsEnabled() const { return enabled_; }
    void SetOffset(float u, float v) { offsetU_ = u; offsetV_ = v; }
    void GetOffset(float& u, float& v) const { u = offsetU_; v = offsetV_; }
    void SetSkyLightScale(float s) { skyLightScale_ = s; }
    float GetSkyLightScale() const { return skyLightScale_; }

    // Scroll and rebuild simple plane mesh
    void Update(float dt);
    void BuildMesh(float centerX, float centerZ, float height, float size);
    const MeshBuffer& GetMesh() const { return mesh_; }
    Texture& GetTexture() { return texture_; }
    void Bind() { texture_.Bind(); }

private:
    bool enabled_ = true;
    float offsetU_ = 0.f, offsetV_ = 0.f;
    float scrollSpeed_ = 0.01f;
    float skyLightScale_ = 1.f;
    Texture texture_;
    MeshBuffer mesh_;
};
