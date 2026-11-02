#include "Assets.h"
#include "SkyBox.h"
#include "Scene.h"
#include <algorithm>
#include <cstdint>

void SkyBox::LoadResources() {
    if (!sunTexPath_.empty())
        sunTex_.LoadPNGAsset(sunTexPath_.c_str());
    if (!moonTexPath_.empty())
        moonTex_.LoadPNGAsset(moonTexPath_.c_str());
    starsBuilt_ = false;
}

void SkyBox::ApplyConfig(const SceneSkyBoxConfig& cfg) {
    if (!cfg.present) return;
    sunriseBegin_ = cfg.sunriseBegin;
    sunriseEnd_   = cfg.sunriseEnd;
    sunsetBegin_  = cfg.sunsetBegin;
    sunsetEnd_    = cfg.sunsetEnd;

    dayLowerR_ = cfg.dayLowerR; dayLowerG_ = cfg.dayLowerG; dayLowerB_ = cfg.dayLowerB;
    dayUpperR_ = cfg.dayUpperR; dayUpperG_ = cfg.dayUpperG; dayUpperB_ = cfg.dayUpperB;
    nightLowerR_ = cfg.nightLowerR; nightLowerG_ = cfg.nightLowerG; nightLowerB_ = cfg.nightLowerB;
    nightUpperR_ = cfg.nightUpperR; nightUpperG_ = cfg.nightUpperG; nightUpperB_ = cfg.nightUpperB;

    horizon_ = cfg.horizon;
    if (!cfg.sunTextureFileName.empty()) sunTexPath_ = cfg.sunTextureFileName;
    if (!cfg.moonTextureFileName.empty()) moonTexPath_ = cfg.moonTextureFileName;
    sunScale_ = cfg.sunScale;
    moonScale_ = cfg.moonScale;
    numStars_ = cfg.numStars;
    starsSeed_ = cfg.starsSeed;
    starsBuilt_ = false;
    RecomputeDayFactor();
}

void SkyBox::RecomputeDayFactor() {
    // SkyBox::SetTimeOfDay @ 0xde770 — dayFactor @ +0x184
    const float t = timeOfDay_;
    const float sb = sunriseBegin_, se = sunriseEnd_;
    const float xb = sunsetBegin_, xe = sunsetEnd_;

    if (t < sb || t >= xe) {
        dayFactor_ = 0.f;
        return;
    }
    if (t >= se && t < xb) {
        dayFactor_ = 1.f;
        return;
    }
    if (t >= sb && t < se) {
        float denom = se - sb;
        dayFactor_ = (denom > 1e-6f) ? (t - sb) / denom : 1.f;
        return;
    }
    float denom = xe - xb;
    dayFactor_ = (denom > 1e-6f) ? (xe - t) / denom : 0.f;
}

void SkyBox::SetTimeOfDay(float t) {
    timeOfDay_ = t - std::floor(t);
    RecomputeDayFactor();
}

Vector SkyBox::GetSunDirection() const {
    float ang = timeOfDay_ * 6.2831853f - 1.5707963f;
    float y = std::sin(ang);
    float xz = std::cos(ang);
    return Vector(xz * 0.3f, y, xz * 0.95f);
}

Vector SkyBox::GetMoonDirection() const {
    Vector s = GetSunDirection();
    return Vector(-s.x, -s.y, -s.z);
}

float SkyBox::GetSunIntensity() const {
    float elev = std::max(0.f, GetSunDirection().y);
    return std::max(dayFactor_ * elev, dayFactor_ * 0.15f);
}

float SkyBox::GetAmbientScale() const {
    return 0.12f + 0.88f * dayFactor_;
}

void SkyBox::GetSkyUpperColor(float& r, float& g, float& b) const {
    float f = dayFactor_;
    r = nightUpperR_ + (dayUpperR_ - nightUpperR_) * f;
    g = nightUpperG_ + (dayUpperG_ - nightUpperG_) * f;
    b = nightUpperB_ + (dayUpperB_ - nightUpperB_) * f;
}

void SkyBox::GetSkyLowerColor(float& r, float& g, float& b) const {
    float f = dayFactor_;
    r = nightLowerR_ + (dayLowerR_ - nightLowerR_) * f;
    g = nightLowerG_ + (dayLowerG_ - nightLowerG_) * f;
    b = nightLowerB_ + (dayLowerB_ - nightLowerB_) * f;
}

void SkyBox::GetSkyColor(float& r, float& g, float& b) const {
    float ur, ug, ub, lr, lg, lb;
    GetSkyUpperColor(ur, ug, ub);
    GetSkyLowerColor(lr, lg, lb);
    r = (ur + lr) * 0.5f;
    g = (ug + lg) * 0.5f;
    b = (ub + lb) * 0.5f;
}

void SkyBox::BuildSunMoonMeshes(const Vector& eye, float dist,
                                MeshBuffer& sunMesh, MeshBuffer& moonMesh) {
    auto billboard = [&](MeshBuffer& mb, Vector dir, float scale) {
        mb.Clear();
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len < 1e-4f) return;
        dir.x /= len; dir.y /= len; dir.z /= len;
        if (dir.y < -0.05f) return;
        Vector c(eye.x + dir.x * dist, eye.y + dir.y * dist, eye.z + dir.z * dist);
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
    const float base = 4.f;
    billboard(sunMesh, GetSunDirection(), sunScale_ * base);
    billboard(moonMesh, GetMoonDirection(), moonScale_ * base);
}

void SkyBox::EnsureStarsMesh() {
    if (starsBuilt_) return;
    starsBuilt_ = true;
    starsMesh_.Clear();
    if (numStars_ <= 0) return;

    uint32_t state = (uint32_t)starsSeed_ * 0x9E3779B9u + 1u;
    auto nextF = [&]() -> float {
        state = state * 0x343FDu + 0x269EC3u;
        return (float)((state >> 16) & 0x7FFF) / 32767.f;
    };

    const int n = std::min(numStars_, 2000);
    const float dist = 180.f;
    const float h = 0.35f;
    uint32_t col = 0xFFFFFFFFu;
    for (int i = 0; i < n; ++i) {
        float u = nextF();
        float v = nextF();
        float theta = u * 6.2831853f;
        float y = v * 0.85f + 0.15f;
        float rr = std::sqrt(std::max(0.f, 1.f - y * y));
        float x = std::cos(theta) * rr;
        float z = std::sin(theta) * rr;
        Vector c(x * dist, y * dist, z * dist);
        PCT1Vertex q[4] = {
            {c.x - h, c.y - h, c.z, col, 0, 1},
            {c.x - h, c.y + h, c.z, col, 0, 0},
            {c.x + h, c.y + h, c.z, col, 1, 0},
            {c.x + h, c.y - h, c.z, col, 1, 1},
        };
        starsMesh_.PushQuad(q);
    }
}
