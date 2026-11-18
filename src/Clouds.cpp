#include "Assets.h"
#include "Clouds.h"
#include <cmath>

void Clouds::LoadResources(const char* texturePath) {
    if (!texture_.LoadPNGAsset(texturePath))
        texture_.LoadPNGAsset("textures/clouds.png");
    texture_.EnsureUploaded();
    BuildMesh(0, 0, 120.f, 512.f);
}

void Clouds::Update(float dt) {
    if (!enabled_) return;
    offsetU_ += scrollSpeed_ * dt;
    offsetV_ += scrollSpeed_ * 0.6f * dt;
    if (offsetU_ > 1.f) offsetU_ -= 1.f;
    if (offsetV_ > 1.f) offsetV_ -= 1.f;
}

void Clouds::BuildMesh(float cx, float cz, float height, float size) {
    // Single large quad plane (Clouds::Mesh::Create style)
    mesh_.Clear();
    float h = size * 0.5f;
    float u0 = offsetU_, v0 = offsetV_;
    float u1 = offsetU_ + 4.f, v1 = offsetV_ + 4.f; // tile
    // Vertex tint from SkyLightScale (UpdateSkyBox: 0.2 night .. 1.0 day)
    float s = skyLightScale_;
    if (s < 0.f) s = 0.f;
    if (s > 1.f) s = 1.f;
    uint32_t c8 = (uint32_t)(s * 255.f + 0.5f);
    uint32_t col = 0xFF000000u | (c8 << 16) | (c8 << 8) | c8;
    PCT1Vertex verts[4] = {
        {cx - h, height, cz - h, col, u0, v0},
        {cx - h, height, cz + h, col, u0, v1},
        {cx + h, height, cz + h, col, u1, v1},
        {cx + h, height, cz - h, col, u1, v0},
    };
    mesh_.PushQuad(verts);
}
