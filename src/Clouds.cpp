#include "Clouds.h"
#include <cmath>

void Clouds::LoadResources(const char* texturePath) {
    if (!texture_.LoadPNG(texturePath))
        texture_.LoadPNG("assets/textures/clouds.png");
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
    uint32_t col = 0xFFFFFFFFu;
    PCT1Vertex verts[4] = {
        {cx - h, height, cz - h, col, u0, v0},
        {cx - h, height, cz + h, col, u0, v1},
        {cx + h, height, cz + h, col, u1, v1},
        {cx + h, height, cz - h, col, u1, v0},
    };
    mesh_.PushQuad(verts);
}
