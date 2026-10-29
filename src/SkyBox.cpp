#include "SkyBox.h"
#include <algorithm>

void SkyBox::LoadResources() {
    sunTex_.LoadPNG("assets/textures/sun.png");
    moonTex_.LoadPNG("assets/textures/moon.png");
}

void SkyBox::SetTimeOfDay(float t) {
    timeOfDay_ = t - std::floor(t);
}

Vector SkyBox::GetSunDirection() const {
    // Angle: 0 at midnight below horizon, PI at noon
    float ang = timeOfDay_ * 6.2831853f - 1.5707963f; // noon at 0.5
    float y = std::sin(ang);
    float xz = std::cos(ang);
    return Vector(xz * 0.3f, y, xz * 0.95f);
}

Vector SkyBox::GetMoonDirection() const {
    Vector s = GetSunDirection();
    return Vector(-s.x, -s.y, -s.z);
}

float SkyBox::GetSunIntensity() const {
    Vector d = GetSunDirection();
    return std::max(0.f, d.y);
}

float SkyBox::GetAmbientScale() const {
    float sun = GetSunIntensity();
    return 0.15f + 0.85f * sun;
}

void SkyBox::GetSkyColor(float& r, float& g, float& b) const {
    float sun = GetSunIntensity();
    // day blue → sunset orange → night dark blue
    if (sun > 0.2f) {
        r = 0.45f + 0.15f * sun;
        g = 0.65f + 0.15f * sun;
        b = 0.95f;
    } else if (sun > 0.f) {
        float t = sun / 0.2f;
        r = 0.9f * (1-t) + 0.45f * t;
        g = 0.35f * (1-t) + 0.65f * t;
        b = 0.25f * (1-t) + 0.95f * t;
    } else {
        r = 0.02f; g = 0.02f; b = 0.08f;
    }
}

void SkyBox::BuildSunMoonMeshes(const Vector& eye, float dist,
                                MeshBuffer& sunMesh, MeshBuffer& moonMesh) {
    auto billboard = [&](MeshBuffer& mb, Vector dir, float scale) {
        mb.Clear();
        // normalize dir
        float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
        if (len < 1e-4f) return;
        dir.x /= len; dir.y /= len; dir.z /= len;
        if (dir.y < -0.05f) return; // below horizon
        Vector c(eye.x + dir.x * dist, eye.y + dir.y * dist, eye.z + dir.z * dist);
        // simple axis-aligned billboard
        float h = scale * 0.5f;
        uint32_t col = 0xFFFFFFFFu;
        PCT1Vertex v[4] = {
            {c.x - h, c.y - h, c.z, col, 0, 1},
            {c.x - h, c.y + h, c.z, col, 0, 0},
            {c.x + h, c.y + h, c.z, col, 1, 0},
            {c.x + h, c.y - h, c.z, col, 1, 1},
        };
        mb.PushQuad(v);
    };
    billboard(sunMesh, GetSunDirection(), 12.f);
    billboard(moonMesh, GetMoonDirection(), 10.f);
}
