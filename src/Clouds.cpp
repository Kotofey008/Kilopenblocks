#include "Assets.h"
#include "Clouds.h"
#include <cmath>
#include <cstring>
#include <algorithm>

bool Clouds::Occupied(int mx, int mz) const {
    if (mapW_ <= 0 || mapH_ <= 0 || bits_.empty()) return false;
    // Wrap like binary (coord & (size-1)) for power-of-two maps
    int x = mx % mapW_; if (x < 0) x += mapW_;
    int z = mz % mapH_; if (z < 0) z += mapH_;
    int idx = z * mapW_ + x;
    return (bits_[(unsigned)idx >> 5] >> (idx & 31)) & 1u;
}

void Clouds::EmitBox(MeshBuffer& out, float x0, float y0, float z0,
                     float x1, float y1, float z1, uint32_t col) {
    // 6 faces, outward, UV 0..1 per face (soft alpha from texture)
    auto quad = [&](float ax, float ay, float az, float bx, float by, float bz,
                    float cx, float cy, float cz, float dx, float dy, float dz) {
        PCT1Vertex v[4] = {
            {ax, ay, az, col, 0.f, 0.f},
            {bx, by, bz, col, 1.f, 0.f},
            {cx, cy, cz, col, 1.f, 1.f},
            {dx, dy, dz, col, 0.f, 1.f},
        };
        out.PushQuad(v);
    };
    // +Y -Y +X -X +Z -Z
    quad(x0,y1,z0, x1,y1,z0, x1,y1,z1, x0,y1,z1);
    quad(x0,y0,z1, x1,y0,z1, x1,y0,z0, x0,y0,z0);
    quad(x1,y0,z0, x1,y0,z1, x1,y1,z1, x1,y1,z0);
    quad(x0,y0,z1, x0,y0,z0, x0,y1,z0, x0,y1,z1);
    quad(x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1);
    quad(x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0);
}

void Clouds::ApplyScene(float posY, float scaleX, float scaleY, float scaleZ,
                        int radius, float windX, float windY, float windZ,
                        const std::string& bitmapPath) {
    baseY_ = posY;
    cellSX_ = scaleX > 0.1f ? scaleX : 12.f;
    cellSY_ = scaleY > 0.1f ? scaleY : 5.f;
    cellSZ_ = scaleZ > 0.1f ? scaleZ : 12.f;
    radius_ = radius > 0 ? radius : 20;
    windX_ = windX; windY_ = windY; windZ_ = windZ;
    if (!bitmapPath.empty())
        LoadResources(bitmapPath.c_str());
    lastCellX_ = 0x7fffffff;
}

void Clouds::LoadResources(const char* texturePath) {
    // CreateBitmap @ 0x618d0: load image, build occupancy for bright/high-bit pixels
    bits_.clear();
    mapW_ = mapH_ = 0;
    if (!texture_.LoadPNGAsset(texturePath))
        texture_.LoadPNGAsset("textures/clouds.png");

    // Occupancy from source pixels BEFORE alpha conversion:
    // binary: signed char < 0 → set bit  (high bit set, value >= 128)
    if (texture_.Valid() && texture_.Width() > 0 && texture_.Height() > 0) {
        mapW_ = (int)texture_.Width();
        mapH_ = (int)texture_.Height();
        const size_t n = (size_t)mapW_ * (size_t)mapH_;
        bits_.assign((n + 31) / 32, 0u);
        const uint8_t* px = texture_.Pixels();
        for (size_t i = 0; i < n; ++i) {
            // Bright / high-bit → cloud cell (matches signed char < 0 test on byte)
            uint8_t r = px[i * 4 + 0];
            if (r >= 128) {
                bits_[i >> 5] |= 1u << (i & 31);
            }
        }
    }

    // CreateTexture path + display: black → alpha (keep RGB for lit soft clouds)
    texture_.ConvertKeyBlackToAlpha();
    texture_.EnsureUploaded();
    lastCellX_ = 0x7fffffff;
}

void Clouds::Update(float dt) {
    if (!enabled_) return;
    // Wind scrolls the sampling origin (binary offset floats +0x174/+0x178)
    offsetU_ += windX_ * dt;
    offsetV_ += windZ_ * dt;
}

void Clouds::UpdateMesh(float centerX, float centerZ) {
    if (!enabled_ || mapW_ <= 0) {
        mesh_.Clear();
        return;
    }
    // Cell under player
    int cx = (int)std::floor(centerX / cellSX_ + offsetU_);
    int cz = (int)std::floor(centerZ / cellSZ_ + offsetV_);
    // Rebuild when moved ≥1 cell or first time
    if (cx == lastCellX_ && cz == lastCellZ_ && mesh_.IndexCount() > 0)
        return;
    lastCellX_ = cx;
    lastCellZ_ = cz;

    mesh_.Clear();
    float s = skyLightScale_;
    if (s < 0.05f) s = 0.05f;
    if (s > 1.f) s = 1.f;
    uint32_t c8 = (uint32_t)(s * 255.f + 0.5f);
    // RGBA LE: R G B A
    uint32_t col = (0xFFu << 24) | (c8 << 16) | (c8 << 8) | c8;

    const int R = radius_;
    // Vertical extent: one cell tall at baseY (binary Scale.y = 5)
    // Multiple layers if we want thickness — single layer at baseY matches scene Pos.y
    float y0 = baseY_;
    float y1 = baseY_ + cellSY_;

    for (int dz = -R; dz <= R; ++dz) {
        for (int dx = -R; dx <= R; ++dx) {
            int mx = cx + dx;
            int mz = cz + dz;
            if (!Occupied(mx, mz)) continue;
            // Face cull: only emit faces adjacent to empty (UpdateMesh neighbor tests)
            bool xp = !Occupied(mx + 1, mz);
            bool xn = !Occupied(mx - 1, mz);
            bool zp = !Occupied(mx, mz + 1);
            bool zn = !Occupied(mx, mz - 1);
            if (!(xp || xn || zp || zn)) {
                // Fully internal in XZ — still draw top/bottom
            }
            float x0 = ((float)mx - offsetU_) * cellSX_;
            float z0 = ((float)mz - offsetV_) * cellSZ_;
            float x1 = x0 + cellSX_;
            float z1 = z0 + cellSZ_;
            // Emit only exposed faces + always top/bottom for billowing look
            auto quad = [&](float ax, float ay, float az, float bx, float by, float bz,
                            float cx_, float cy, float cz_, float dx_, float dy, float dz_) {
                PCT1Vertex v[4] = {
                    {ax, ay, az, col, 0.f, 0.f},
                    {bx, by, bz, col, 1.f, 0.f},
                    {cx_, cy, cz_, col, 1.f, 1.f},
                    {dx_, dy, dz_, col, 0.f, 1.f},
                };
                mesh_.PushQuad(v);
            };
            // Top + bottom always
            quad(x0,y1,z0, x1,y1,z0, x1,y1,z1, x0,y1,z1);
            quad(x0,y0,z1, x1,y0,z1, x1,y0,z0, x0,y0,z0);
            if (xp) quad(x1,y0,z0, x1,y0,z1, x1,y1,z1, x1,y1,z0);
            if (xn) quad(x0,y0,z1, x0,y0,z0, x0,y1,z0, x0,y1,z1);
            if (zp) quad(x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1);
            if (zn) quad(x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0);
        }
    }
}
