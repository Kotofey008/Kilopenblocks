#include "Assets.h"
#include "Clouds.h"
#include "Scene.h"
#include <cmath>
#include <algorithm>

bool Clouds::Occupied(int mx, int mz) const {
    if (mapW_ <= 0 || mapH_ <= 0 || bits_.empty()) return false;
    int x = mx % mapW_; if (x < 0) x += mapW_;
    int z = mz % mapH_; if (z < 0) z += mapH_;
    int idx = z * mapW_ + x;
    return (bits_[(unsigned)idx >> 5] >> (idx & 31)) & 1u;
}

void Clouds::GetCellRGB(int mx, int mz, uint8_t& r, uint8_t& g, uint8_t& b) const {
    if (mapW_ <= 0 || cellR_.empty()) { r = g = b = 255; return; }
    int x = mx % mapW_; if (x < 0) x += mapW_;
    int z = mz % mapH_; if (z < 0) z += mapH_;
    size_t idx = (size_t)z * (size_t)mapW_ + (size_t)x;
    r = cellR_[idx]; g = cellG_[idx]; b = cellB_[idx];
}

void Clouds::PixelUV(int mx, int mz, float& u, float& v) const {
    if (mapW_ <= 0 || mapH_ <= 0) { u = v = 0.5f; return; }
    int x = mx % mapW_; if (x < 0) x += mapW_;
    int z = mz % mapH_; if (z < 0) z += mapH_;
    // Center of that pixel in the clouds bitmap (not the whole atlas stretched)
    u = ((float)x + 0.5f) / (float)mapW_;
    v = ((float)z + 0.5f) / (float)mapH_;
}

void Clouds::ApplyConfig(const SceneCloudsConfig& cfg) {
    if (!cfg.present) return;
    baseY_ = cfg.posY;
    cellSX_ = cfg.scaleX > 0.1f ? cfg.scaleX : 12.f;
    cellSY_ = cfg.scaleY > 0.1f ? cfg.scaleY : 5.f;
    cellSZ_ = cfg.scaleZ > 0.1f ? cfg.scaleZ : 12.f;
    radius_ = cfg.radius > 0 ? cfg.radius : 20;
    windX_ = cfg.windX;
    windY_ = cfg.windY;
    windZ_ = cfg.windZ;
    enabled_ = cfg.enabled;
    if (!cfg.bitmapFileName.empty())
        LoadResources(cfg.bitmapFileName.c_str());
}

void Clouds::LoadResources(const char* texturePath) {
    bits_.clear();
    cellR_.clear(); cellG_.clear(); cellB_.clear();
    mapW_ = mapH_ = 0;
    if (!texture_.LoadPNGAsset(texturePath))
        texture_.LoadPNGAsset("textures/clouds.png");

    // CreateBitmap: white/bright pixels → occupied; black → empty sky
    if (texture_.Valid() && texture_.Width() > 0 && texture_.Height() > 0) {
        mapW_ = (int)texture_.Width();
        mapH_ = (int)texture_.Height();
        const size_t n = (size_t)mapW_ * (size_t)mapH_;
        bits_.assign((n + 31) / 32, 0u);
        cellR_.resize(n);
        cellG_.resize(n);
        cellB_.resize(n);
        const uint8_t* px = texture_.Pixels();
        for (size_t i = 0; i < n; ++i) {
            uint8_t r = px[i * 4 + 0];
            uint8_t g = px[i * 4 + 1];
            uint8_t b = px[i * 4 + 2];
            cellR_[i] = r; cellG_[i] = g; cellB_[i] = b;
            // Bright / high-bit (signed char < 0) → cloud cell
            if (r >= 128 || g >= 128 || b >= 128)
                bits_[i >> 5] |= 1u << (i & 31);
        }
    }

    // Black → transparent in GL texture; each voxel UVs to its pixel
    texture_.ConvertKeyBlackToAlpha();
    texture_.EnsureUploaded();
}

void Clouds::Update(float dt) {
    if (!enabled_) return;
    // Continuous wind scroll in cell units (binary +0x174/+0x178 floats)
    offsetU_ += windX_ * dt;
    offsetV_ += windZ_ * dt;
}

void Clouds::UpdateMesh(float centerX, float centerZ) {
    mesh_.Clear();
    if (!enabled_ || mapW_ <= 0) return;

    // Continuous offsets (binary +0x174 / +0x178). World cell positions:
    //   x = (mapX - offsetU) * scale  → smooth drift as offset increases.
    // Occupancy sample uses mapX directly (wrap). Radius window tracks the
    // player in "offset space" so new cells enter without teleport jumps.
    const float offU = offsetU_;
    const float offV = offsetV_;

    float sampleOriginX = centerX / cellSX_ + offU;
    float sampleOriginZ = centerZ / cellSZ_ + offV;
    int baseMapX = (int)std::floor(sampleOriginX);
    int baseMapZ = (int)std::floor(sampleOriginZ);

    float s = skyLightScale_;
    if (s < 0.05f) s = 0.05f;
    if (s > 1.f) s = 1.f;

    const int R = radius_;
    float y0 = baseY_;
    float y1 = baseY_ + cellSY_;

    for (int dz = -R; dz <= R; ++dz) {
        for (int dx = -R; dx <= R; ++dx) {
            int mx = baseMapX + dx;
            int mz = baseMapZ + dz;
            if (!Occupied(mx, mz)) continue;

            uint8_t cr, cg, cb;
            GetCellRGB(mx, mz, cr, cg, cb);
            uint32_t r8 = (uint32_t)(cr * s + 0.5f);
            uint32_t g8 = (uint32_t)(cg * s + 0.5f);
            uint32_t b8 = (uint32_t)(cb * s + 0.5f);
            if (r8 > 255) r8 = 255;
            if (g8 > 255) g8 = 255;
            if (b8 > 255) b8 = 255;
            // GL color pointer is RGBA bytes in LE uint32 → R | G<<8 | B<<16 | A<<24
            uint32_t col = (0xFFu << 24) | (b8 << 16) | (g8 << 8) | r8;

            float u, v;
            PixelUV(mx, mz, u, v);

            // Continuous world placement — no per-cell snap teleport
            float x0 = ((float)mx - offU) * cellSX_;
            float z0 = ((float)mz - offV) * cellSZ_;
            float x1 = x0 + cellSX_;
            float z1 = z0 + cellSZ_;

            bool xp = !Occupied(mx + 1, mz);
            bool xn = !Occupied(mx - 1, mz);
            bool zp = !Occupied(mx, mz + 1);
            bool zn = !Occupied(mx, mz - 1);

            auto quad = [&](float ax, float ay, float az, float bx, float by, float bz,
                            float cx_, float cy, float cz_, float dx_, float dy, float dz_) {
                PCT1Vertex verts[4] = {
                    {ax, ay, az, col, u, v},
                    {bx, by, bz, col, u, v},
                    {cx_, cy, cz_, col, u, v},
                    {dx_, dy, dz_, col, u, v},
                };
                mesh_.PushQuad(verts);
            };

            quad(x0,y1,z0, x1,y1,z0, x1,y1,z1, x0,y1,z1);
            quad(x0,y0,z1, x1,y0,z1, x1,y0,z0, x0,y0,z0);
            if (xp) quad(x1,y0,z0, x1,y0,z1, x1,y1,z1, x1,y1,z0);
            if (xn) quad(x0,y0,z1, x0,y0,z0, x0,y1,z0, x0,y1,z1);
            if (zp) quad(x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1);
            if (zn) quad(x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0);
        }
    }
}

