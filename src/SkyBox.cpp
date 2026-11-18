#include "Assets.h"
#include "SkyBox.h"
#include "Scene.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kHalfPi = 1.57079632679489661923f;
constexpr uint32_t kLcgMul = 0x343FDu;
constexpr uint32_t kLcgAdd = 0x269EC3u;
}

uint32_t SkyBox::PackColor(float r, float g, float b, float a) {
    auto ch = [](float x) -> uint32_t {
        if (x < 0.f) x = 0.f;
        if (x > 1.f) x = 1.f;
        return (uint32_t)(x * 255.f + 0.5f);
    };
    return (ch(a) << 24) | (ch(b) << 16) | (ch(g) << 8) | ch(r);
}

void SkyBox::LoadResources() {
    // Paths from scene are "textures/sun.png" (assets-relative).
    auto load = [](Texture& tex, const std::string& path) {
        if (path.empty()) return;
        if (!tex.LoadPNGAsset(path.c_str())) {
            // fallback without folder prefix
            std::string base = path;
            size_t s = base.find_last_of('/');
            if (s != std::string::npos)
                tex.LoadPNGAsset(base.substr(s + 1).c_str());
        }
        // Black background → transparent (same idea as font alpha mask)
        tex.ConvertKeyBlackToAlpha();
        tex.EnsureUploaded();
    };
    load(sunTex_, sunTexPath_);
    load(moonTex_, moonTexPath_);
    starsBuilt_ = false;
    cachedDayFactor_ = -1.f;
    UpdateSkyMesh();
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
    if (cfg.scaleX > 0.1f) skyObjectScale_ = cfg.scaleX;
    starsBuilt_ = false;
    cachedDayFactor_ = -1.f;
    RecomputeDayFactor();
    UpdateSkyMesh();
}

void SkyBox::RecomputeDayFactor() {
    const float t = timeOfDay_;
    const float sb = sunriseBegin_, se = sunriseEnd_;
    const float xb = sunsetBegin_, xe = sunsetEnd_;
    if (t <= sb || t >= xe) { dayFactor_ = 0.f; return; }
    if (t >= se && t < xb) { dayFactor_ = 1.f; return; }
    if (t > sb && t < se) {
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
    UpdateSkyMesh();
    if (starsBuilt_) RebuildStarsSubset();
}

Vector SkyBox::GetSunDirection() const {
    float ang = timeOfDay_ * kTwoPi - kHalfPi;
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
    r = (ur + lr) * 0.5f; g = (ug + lg) * 0.5f; b = (ub + lb) * 0.5f;
}

void SkyBox::UpdateSkyMesh() {
    if (dayFactor_ == cachedDayFactor_ && horizonMesh_.IndexCount() > 0)
        return;
    cachedDayFactor_ = dayFactor_;
    float lr, lg, lb, ur, ug, ub;
    GetSkyLowerColor(lr, lg, lb);
    GetSkyUpperColor(ur, ug, ub);
    uint32_t colLo = PackColor(lr, lg, lb);
    uint32_t colHi = PackColor(ur, ug, ub);
    horizonMesh_.Clear();
    if (!horizon_) return;
    const float rad = skyObjectScale_ * 12.f;
    const float yLo = -skyObjectScale_ * 0.5f;
    const float yHi = skyObjectScale_ * 4.f;
    const int segs = 16;
    for (int i = 0; i < segs; ++i) {
        float a0 = (float)i / (float)segs * kTwoPi;
        float a1 = (float)(i + 1) / (float)segs * kTwoPi;
        float x0 = std::cos(a0) * rad, z0 = std::sin(a0) * rad;
        float x1 = std::cos(a1) * rad, z1 = std::sin(a1) * rad;
        PCT1Vertex v[4] = {
            {x0, yLo, z0, colLo, 0.f, 1.f},
            {x1, yLo, z1, colLo, 1.f, 1.f},
            {x1, yHi, z1, colHi, 1.f, 0.f},
            {x0, yHi, z0, colHi, 0.f, 0.f},
        };
        horizonMesh_.PushQuad(v);
    }
    for (int i = 0; i < segs; ++i) {
        float a0 = (float)i / (float)segs * kTwoPi;
        float a1 = (float)(i + 1) / (float)segs * kTwoPi;
        PCT1Vertex v[4] = {
            {0.f, yHi, 0.f, colHi, 0.5f, 0.5f},
            {std::cos(a0) * rad, yHi, std::sin(a0) * rad, colHi, 0.f, 0.f},
            {std::cos(a1) * rad, yHi, std::sin(a1) * rad, colHi, 1.f, 0.f},
            {0.f, yHi, 0.f, colHi, 0.5f, 0.5f},
        };
        horizonMesh_.PushQuad(v);
    }
}

void SkyBox::BuildSunMoonMeshes(const Vector& eye, const Vector& right, const Vector& up,
                                float dist, MeshBuffer& sunMesh, MeshBuffer& moonMesh) {
    auto billboard = [&](MeshBuffer& mb, Vector dir, float scale) {
        mb.Clear();
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len < 1e-4f) return;
        dir.x /= len; dir.y /= len; dir.z /= len;
        if (dir.y < -0.02f) return;
        float d = dist > 1.f ? dist : 120.f;
        Vector c(eye.x + dir.x * d, eye.y + dir.y * d, eye.z + dir.z * d);
        float h = scale * skyObjectScale_ * 0.5f;

        // Screen-aligned billboard facing camera: axes = camera right/up projected
        // onto plane perpendicular to (center - eye) ≈ dir.
        Vector zaxis(dir.x, dir.y, dir.z); // from eye to body
        Vector r = right;
        // Remove component along z so sprite stays centered on the ray
        float dot = r.x * zaxis.x + r.y * zaxis.y + r.z * zaxis.z;
        r.x -= dot * zaxis.x; r.y -= dot * zaxis.y; r.z -= dot * zaxis.z;
        float rl = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
        Vector u = up;
        float dotu = u.x * zaxis.x + u.y * zaxis.y + u.z * zaxis.z;
        u.x -= dotu * zaxis.x; u.y -= dotu * zaxis.y; u.z -= dotu * zaxis.z;
        float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        if (rl < 1e-4f || ul < 1e-4f) {
            Vector tmp = (std::fabs(zaxis.y) < 0.99f) ? Vector(0.f, 1.f, 0.f) : Vector(1.f, 0.f, 0.f);
            r = Vector(tmp.y * zaxis.z - tmp.z * zaxis.y,
                       tmp.z * zaxis.x - tmp.x * zaxis.z,
                       tmp.x * zaxis.y - tmp.y * zaxis.x);
            rl = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
            if (rl < 1e-4f) return;
            r.x /= rl; r.y /= rl; r.z /= rl;
            u = Vector(zaxis.y * r.z - zaxis.z * r.y,
                       zaxis.z * r.x - zaxis.x * r.z,
                       zaxis.x * r.y - zaxis.y * r.x);
            ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
            if (ul < 1e-4f) return;
            u.x /= ul; u.y /= ul; u.z /= ul;
        } else {
            r.x /= rl; r.y /= rl; r.z /= rl;
            u.x /= ul; u.y /= ul; u.z /= ul;
        }

        uint32_t col = 0xFFFFFFFFu;
        Vector p0(c.x - r.x * h - u.x * h, c.y - r.y * h - u.y * h, c.z - r.z * h - u.z * h);
        Vector p1(c.x - r.x * h + u.x * h, c.y - r.y * h + u.y * h, c.z - r.z * h + u.z * h);
        Vector p2(c.x + r.x * h + u.x * h, c.y + r.y * h + u.y * h, c.z + r.z * h + u.z * h);
        Vector p3(c.x + r.x * h - u.x * h, c.y + r.y * h - u.y * h, c.z + r.z * h - u.z * h);
        PCT1Vertex v[4] = {
            {p0.x, p0.y, p0.z, col, 0.f, 1.f},
            {p1.x, p1.y, p1.z, col, 0.f, 0.f},
            {p2.x, p2.y, p2.z, col, 1.f, 0.f},
            {p3.x, p3.y, p3.z, col, 1.f, 1.f},
        };
        mb.PushQuad(v);
    };
    billboard(sunMesh, GetSunDirection(), sunScale_);
    billboard(moonMesh, GetMoonDirection(), moonScale_);
}

void SkyBox::EnsureStarsMesh() {
    if (starsBuilt_) return;
    starsBuilt_ = true;
    starsFullMesh_.Clear();
    starSortKeys_.clear();
    starOrder_.clear();
    if (numStars_ <= 0) return;

    int seed = starsSeed_;
    int s = seed;
    s = ((s + 1) * (int)kLcgMul + (int)kLcgAdd) * (s + 1) + s;
    s = (s * (int)kLcgMul + (int)kLcgAdd) * s + seed;
    s = (s * (int)kLcgMul + (int)kLcgAdd) * s + seed;
    s = (s * (int)kLcgMul + (int)kLcgAdd) * s + seed;
    s = (s * (int)kLcgMul + (int)kLcgAdd) * s + seed;
    s = seed + (s * (int)kLcgMul + (int)kLcgAdd) * s;
    uint32_t state = (uint32_t)(s * (s * (int)kLcgMul + (int)kLcgAdd));
    auto nextU = [&]() -> uint32_t { state = state * kLcgMul + kLcgAdd; return state; };
    auto nextF = [&]() -> float { return (float)((nextU() >> 16) & 0x7FFF) / 32767.f; };

    const int n = numStars_;
    const float dist = skyObjectScale_ * 11.25f;
    starSortKeys_.resize((size_t)n);
    starOrder_.resize((size_t)n);

    struct Star { float x,y,z,half; uint32_t col; uint16_t key; };
    std::vector<Star> stars;
    stars.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        float az = nextF() * kTwoPi;
        float el = nextF() * (kHalfPi * 0.92f) + 0.08f;
        float size01 = nextF();
        float x = std::cos(el) * std::cos(az);
        float y = std::sin(el);
        float z = std::cos(el) * std::sin(az);
        uint32_t col = (size01 < 0.55f) ? 0xFFEEEEDDu : 0xFFBBBBAAu;
        float half = 0.25f + size01 * 0.55f;
        uint16_t key = (uint16_t)std::min(65535, (int)((az / kTwoPi) * 65535.f));
        stars.push_back({x*dist, y*dist, z*dist, half, col, key});
        starSortKeys_[(size_t)i] = key;
        starOrder_[(size_t)i] = i;
    }
    std::sort(starOrder_.begin(), starOrder_.end(), [&](int a, int b) {
        return starSortKeys_[(size_t)a] < starSortKeys_[(size_t)b];
    });
    for (int oi = 0; oi < n; ++oi) {
        const Star& st = stars[(size_t)starOrder_[(size_t)oi]];
        float h = st.half;
        PCT1Vertex q[4] = {
            {st.x-h, st.y-h, st.z, st.col, 0.f, 1.f},
            {st.x-h, st.y+h, st.z, st.col, 0.f, 0.f},
            {st.x+h, st.y+h, st.z, st.col, 1.f, 0.f},
            {st.x+h, st.y-h, st.z, st.col, 1.f, 1.f},
        };
        starsFullMesh_.PushQuad(q);
    }
    RebuildStarsSubset();
}

void SkyBox::RebuildStarsSubset() {
    starsDrawMesh_.Clear();
    if (!starsBuilt_ || numStars_ <= 0) return;
    float night = 1.f - dayFactor_;
    if (night <= 0.02f) return;
    int count = (int)((float)numStars_ * night);
    if (count < 1) count = 1;
    if (count > numStars_) count = numStars_;
    int start = (int)(timeOfDay_ * (float)numStars_) % numStars_;
    if (start < 0) start += numStars_;
    const auto& srcV = starsFullMesh_.Vertices();
    if (srcV.size() < (size_t)numStars_ * 4) return;
    for (int k = 0; k < count; ++k) {
        int si = (start + k) % numStars_;
        size_t baseVert = (size_t)si * 4;
        PCT1Vertex q[4] = { srcV[baseVert], srcV[baseVert+1], srcV[baseVert+2], srcV[baseVert+3] };
        for (int vi = 0; vi < 4; ++vi) {
            uint32_t c = q[vi].color;
            uint32_t a = (c >> 24) & 0xff;
            a = (uint32_t)((float)a * night);
            q[vi].color = (a << 24) | (c & 0x00FFFFFFu);
        }
        starsDrawMesh_.PushQuad(q);
    }
}
