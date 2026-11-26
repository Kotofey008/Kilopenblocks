#include "Lighting.h"
// INTENTIONAL DEVIATION: MCPE / classic dual-channel flood-fill propagation.
// See Lighting.h banner. Public packing + GetFaceLight + GetLightColor stay
// original-compatible for mesher and future RE work.

#include <algorithm>
#include <cstring>
#include <queue>
#include <vector>

// ---------------------------------------------------------------------------
// Material helpers (project Material table)
// ---------------------------------------------------------------------------
static int SignedL(const Material* m) {
    if (!m) return 0;
    int v = (int)(int8_t)m->sizeL();
    if (v < -15) v = -15;
    if (v > 15) v = 15;
    return v;
}

// Blocks that stop sky and block light (solid, not leaves/glass/cross/torch).
static bool IsOpaque(const GridData& grid, unsigned char t) {
    if (t == 0) return false;
    const Material* m = grid.GetMaterial((int)t);
    if (!m) return true;
    if (!m->col()) return false;
    uint8_t cls = m->prop01();
    // 1=cross, 2=torch, 3=ladder, 4=… non-cube — light passes
    if (cls == 1 || cls == 2 || cls == 3 || cls == 4) return false;
    // Negative L (leaves, ice) — attenuate, do not fully occlude
    if (SignedL(m) < 0) return false;
    return true;
}

static int Emission(const GridData& grid, unsigned char t) {
    if (t == 0) return 0;
    const Material* m = grid.GetMaterial((int)t);
    int L = SignedL(m);
    return (L > 0) ? LightPack::Clamp15(L) : 0;
}

// Attenuation when light enters this cell (leaves L=-1 → −1 extra, water/ice −2).
static int CellAtten(const GridData& grid, int cidx, unsigned char t) {
    if (t != 0) {
        int L = SignedL(grid.GetMaterial((int)t));
        if (L < 0) return -L;
        return 0;
    }
    // Air with fluid bits (host water stored as type0 + state).
    // Stronger atten so underwater closed volumes go dark (esp. at night).
    if (cidx >= 0 && cidx < (int)grid.cellStates.size()) {
        unsigned short st = grid.cellStates[cidx];
        if ((st & GridData::STATE_BIT_2000) != 0)
            return 2;
    }
    return 0;
}

// ---------------------------------------------------------------------------
int LightMap::Index(int x, int y, int z) const {
    int lx = x - ox_, ly = y - oy_, lz = z - oz_;
    if (lx < 0 || ly < 0 || lz < 0 || lx >= sizeX_ || ly >= sizeY_ || lz >= sizeZ_)
        return -1;
    return lx + sizeX_ * (ly + sizeY_ * lz);
}

void LightMap::InitDefaultCurves() {
    for (int i = 0; i < 16; ++i) {
        float t = (float)i / 15.f;
        curveR_[i] = curveG_[i] = curveB_[i] = curveRow_[i] = t;
    }
}

void LightMap::Init(GridData& grid) {
    ox_ = grid.originX; oy_ = grid.originY; oz_ = grid.originZ;
    sizeX_ = grid.sizeX > 0 ? grid.sizeX : (int)(grid.worldMax.x - grid.worldMin.x);
    sizeY_ = grid.sizeY > 0 ? grid.sizeY : (int)(grid.worldMax.y - grid.worldMin.y);
    sizeZ_ = grid.sizeZ > 0 ? grid.sizeZ : (int)(grid.worldMax.z - grid.worldMin.z);
    if (sizeX_ <= 0) { ox_ = (int)grid.worldMin.x; sizeX_ = (int)(grid.worldMax.x - grid.worldMin.x); }
    if (sizeY_ <= 0) { oy_ = (int)grid.worldMin.y; sizeY_ = (int)(grid.worldMax.y - grid.worldMin.y); }
    if (sizeZ_ <= 0) { oz_ = (int)grid.worldMin.z; sizeZ_ = (int)(grid.worldMax.z - grid.worldMin.z); }
    data_.assign((size_t)sizeX_ * (size_t)sizeY_ * (size_t)sizeZ_, 0);
    dirty_ = true;
    InitDefaultCurves();
    brightnessApplied_ = -1.f;
    UpdateLightMap();
    PropagateBlockLights(grid);
}

// ---------------------------------------------------------------------------
// Sky: column top-down seed + BFS flood (MCPE)
// ---------------------------------------------------------------------------
void LightMap::PropagateSky(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1) {
    // Clear sky nibble only in region (keep block)
    for (int z = lz0; z < lz1; ++z)
        for (int y = ly0; y < ly1; ++y)
            for (int x = lx0; x < lx1; ++x) {
                int idx = x + sizeX_ * (y + sizeY_ * z);
                int blk = LightPack::Block(data_[(size_t)idx]);
                data_[(size_t)idx] = LightPack::Make(0, blk);
            }

    std::queue<int> q;
    auto inR = [&](int x, int y, int z) {
        return x >= lx0 && x < lx1 && y >= ly0 && y < ly1 && z >= lz0 && z < lz1;
    };
    auto setSky = [&](int x, int y, int z, int sky) {
        sky = LightPack::Clamp15(sky);
        int idx = x + sizeX_ * (y + sizeY_ * z);
        int blk = LightPack::Block(data_[(size_t)idx]);
        int cur = LightPack::Sky(data_[(size_t)idx]);
        if (sky > cur) {
            data_[(size_t)idx] = LightPack::Make(sky, blk);
            q.push(idx);
        }
    };

    // 1) Vertical column seed: 15 from open top until opaque.
    // If rebuild band is below world top, inherit sky from cell above ly1.
    for (int z = lz0; z < lz1; ++z) {
        for (int x = lx0; x < lx1; ++x) {
            int sky = (ly1 >= sizeY_)
                          ? 15
                          : LightPack::Sky(GetLight(x + ox_, oy_ + ly1, z + oz_));
            for (int y = ly1 - 1; y >= ly0; --y) {
                int wx = x + ox_, wy = y + oy_, wz = z + oz_;
                int cidx = grid.CoordToIndex(wx, wy, wz);
                unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : 0;
                if (IsOpaque(grid, t)) {
                    sky = 0;
                    int idx = x + sizeX_ * (y + sizeY_ * z);
                    int blk = LightPack::Block(data_[(size_t)idx]);
                    data_[(size_t)idx] = LightPack::Make(0, blk);
                    continue;
                }
                int atten = CellAtten(grid, cidx, t);
                int level = sky - atten;
                if (level < 0) level = 0;
                setSky(x, y, z, level);
                // MCPE: open air keeps sky level downward (no -1 per air cell)
                sky = level;
            }
        }
    }

    // 2) Horizontal + remaining vertical flood
    static const int dx[6] = {1,-1,0,0,0,0};
    static const int dy[6] = {0,0,1,-1,0,0};
    static const int dz[6] = {0,0,0,0,1,-1};
    while (!q.empty()) {
        int idx = q.front();
        q.pop();
        int level = LightPack::Sky(data_[(size_t)idx]);
        if (level <= 1) continue;
        int x = idx % sizeX_;
        int y = (idx / sizeX_) % sizeY_;
        int z = idx / (sizeX_ * sizeY_);
        for (int d = 0; d < 6; ++d) {
            int nx = x + dx[d], ny = y + dy[d], nz = z + dz[d];
            if (!inR(nx, ny, nz)) continue;
            int wx = nx + ox_, wy = ny + oy_, wz = nz + oz_;
            int cidx = grid.CoordToIndex(wx, wy, wz);
            unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : 0;
            if (IsOpaque(grid, t)) continue;
            int atten = CellAtten(grid, cidx, t);
            int nl = level - 1 - atten;
            if (nl < 0) nl = 0;
            setSky(nx, ny, nz, nl);
        }
    }
}

// ---------------------------------------------------------------------------
// Block: emitter seeds + BFS flood (MCPE)
// ---------------------------------------------------------------------------
void LightMap::PropagateBlock(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1) {
    for (int z = lz0; z < lz1; ++z)
        for (int y = ly0; y < ly1; ++y)
            for (int x = lx0; x < lx1; ++x) {
                int idx = x + sizeX_ * (y + sizeY_ * z);
                int sky = LightPack::Sky(data_[(size_t)idx]);
                data_[(size_t)idx] = LightPack::Make(sky, 0);
            }

    std::queue<int> q;
    auto inR = [&](int x, int y, int z) {
        return x >= lx0 && x < lx1 && y >= ly0 && y < ly1 && z >= lz0 && z < lz1;
    };
    auto setBlk = [&](int x, int y, int z, int blk) {
        blk = LightPack::Clamp15(blk);
        int idx = x + sizeX_ * (y + sizeY_ * z);
        int sky = LightPack::Sky(data_[(size_t)idx]);
        int cur = LightPack::Block(data_[(size_t)idx]);
        if (blk > cur) {
            data_[(size_t)idx] = LightPack::Make(sky, blk);
            q.push(idx);
        }
    };

    for (int z = lz0; z < lz1; ++z) {
        for (int y = ly0; y < ly1; ++y) {
            for (int x = lx0; x < lx1; ++x) {
                int wx = x + ox_, wy = y + oy_, wz = z + oz_;
                int cidx = grid.CoordToIndex(wx, wy, wz);
                if (cidx < 0) continue;
                int emit = Emission(grid, grid.cellTypes[cidx]);
                if (emit > 0)
                    setBlk(x, y, z, emit);
            }
        }
    }

    // Seed from block light just outside the cleared AABB so local updates
    // blend with the rest of the world (torch near region edge).
    auto pullOutside = [&](int x, int y, int z, int ox2, int oy2, int oz2) {
        if (!inR(x, y, z)) return;
        if (ox2 >= lx0 && ox2 < lx1 && oy2 >= ly0 && oy2 < ly1 && oz2 >= lz0 && oz2 < lz1)
            return;
        if (ox2 < 0 || oy2 < 0 || oz2 < 0 || ox2 >= sizeX_ || oy2 >= sizeY_ || oz2 >= sizeZ_)
            return;
        int oiLocal = ox2 + sizeX_ * (oy2 + sizeY_ * oz2);
        int b = LightPack::Block(data_[(size_t)oiLocal]);
        if (b <= 1) return;
        int wx = x + ox_, wy = y + oy_, wz = z + oz_;
        int cidx = grid.CoordToIndex(wx, wy, wz);
        unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : 0;
        if (IsOpaque(grid, t)) return;
        int atten = CellAtten(grid, cidx, t);
        int nl = b - 1 - atten;
        if (nl > 0) setBlk(x, y, z, nl);
    };
    for (int z = lz0; z < lz1; ++z)
        for (int y = ly0; y < ly1; ++y) {
            pullOutside(lx0, y, z, lx0 - 1, y, z);
            pullOutside(lx1 - 1, y, z, lx1, y, z);
        }
    for (int x = lx0; x < lx1; ++x)
        for (int y = ly0; y < ly1; ++y) {
            pullOutside(x, y, lz0, x, y, lz0 - 1);
            pullOutside(x, y, lz1 - 1, x, y, lz1);
        }
    for (int x = lx0; x < lx1; ++x)
        for (int z = lz0; z < lz1; ++z) {
            pullOutside(x, ly0, z, x, ly0 - 1, z);
            pullOutside(x, ly1 - 1, z, x, ly1, z);
        }

    static const int dx[6] = {1,-1,0,0,0,0};
    static const int dy[6] = {0,0,1,-1,0,0};
    static const int dz[6] = {0,0,0,0,1,-1};
    while (!q.empty()) {
        int idx = q.front();
        q.pop();
        int level = LightPack::Block(data_[(size_t)idx]);
        if (level <= 1) continue;
        int x = idx % sizeX_;
        int y = (idx / sizeX_) % sizeY_;
        int z = idx / (sizeX_ * sizeY_);
        for (int d = 0; d < 6; ++d) {
            int nx = x + dx[d], ny = y + dy[d], nz = z + dz[d];
            if (!inR(nx, ny, nz)) continue;
            int wx = nx + ox_, wy = ny + oy_, wz = nz + oz_;
            int cidx = grid.CoordToIndex(wx, wy, wz);
            unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : 0;
            if (IsOpaque(grid, t)) continue;
            int atten = CellAtten(grid, cidx, t);
            int nl = level - 1 - atten;
            if (nl < 0) nl = 0;
            setBlk(nx, ny, nz, nl);
        }
    }
}

void LightMap::RebuildRegion(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1) {
    if (data_.empty() || sizeX_ <= 0) return;
    if (lx0 < 0) lx0 = 0;
    if (ly0 < 0) ly0 = 0;
    if (lz0 < 0) lz0 = 0;
    if (lx1 > sizeX_) lx1 = sizeX_;
    if (ly1 > sizeY_) ly1 = sizeY_;
    if (lz1 > sizeZ_) lz1 = sizeZ_;
    if (lx0 >= lx1 || ly0 >= ly1 || lz0 >= lz1) return;
    PropagateSky(grid, lx0, lx1, ly0, ly1, lz0, lz1);
    PropagateBlock(grid, lx0, lx1, ly0, ly1, lz0, lz1);
    dirty_ = false;
}

void LightMap::PropagateBlockLights(GridData& grid) {
    RebuildRegion(grid, 0, sizeX_, 0, sizeY_, 0, sizeZ_);
}

void LightMap::PropagateBlockLightsLocal(GridData& grid, int minX, int maxX, int minY, int maxY,
                                         int minZ, int maxZ) {
    const int R = 15; // max light travel
    int lx0 = (minX - R) - ox_;
    int lx1 = (maxX + R) - ox_;
    int ly0 = (minY - R) - oy_;
    int ly1 = (maxY + R) - oy_;
    int lz0 = (minZ - R) - oz_;
    int lz1 = (maxZ + R) - oz_;
    RebuildRegion(grid, lx0, lx1, ly0, ly1, lz0, lz1);
}

void LightMap::UpdateSunRays(GridData& grid) {
    // Compatibility: full sky+block rebuild
    PropagateBlockLights(grid);
}

void LightMap::UpdateSunRaysLocal(GridData& grid, int minX, int maxX, int minZ, int maxZ) {
    PropagateBlockLightsLocal(grid, minX, maxX, oy_, oy_ + sizeY_, minZ, maxZ);
}

void LightMap::InvalidateSunRays(int /*worldX*/, int /*worldZ*/) {
    dirty_ = true;
}

void LightMap::AddLight(int x, int y, int z, uint16_t packed) {
    int i = Index(x, y, z);
    if (i < 0) return;
    int nb = LightPack::Block(packed);
    int sky = LightPack::Sky(data_[(size_t)i]);
    int cur = LightPack::Block(data_[(size_t)i]);
    if (nb > cur)
        data_[(size_t)i] = LightPack::Make(sky, nb);
}

// ---------------------------------------------------------------------------
uint16_t LightMap::GetLight(int x, int y, int z) const {
    int i = Index(x, y, z);
    if (i < 0) {
        // Open void above world = full sky (mesher top faces)
        if (y >= oy_ + sizeY_) return LightPack::Make(15, 0);
        return 0;
    }
    return data_[(size_t)i];
}

uint16_t LightMap::GetFaceLight(int x, int y, int z, int face) const {
    // Sample cell outside the face; mode bits from binary face map.
    // ourFace: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
    int nx = x, ny = y, nz = z;
    uint16_t mode = LightPack::MODE_DEFAULT;
    switch (face) {
        case 0: nx++; mode = LightPack::MODE_DEFAULT; break;
        case 1: nx--; mode = LightPack::MODE_1800;    break;
        case 2: ny++; mode = LightPack::MODE_0800;    break;
        case 3: ny--; mode = LightPack::MODE_2000;    break;
        case 4: nz++; mode = LightPack::MODE_1000;    break;
        case 5: nz--; mode = LightPack::MODE_2800;    break;
        default: break;
    }
    return (uint16_t)((GetLight(nx, ny, nz) & ~LightPack::MODE_MASK) | mode);
}

void LightMap::SetBrightness(float b) {
    if (b < 0.f) b = 0.f;
    if (b > 1.f) b = 1.f;
    brightness_ = b;
}

void LightMap::UpdateLightMap() {
    // Day/night only touches the 16×16 color LUT — never re-propagates light.
    if (brightnessApplied_ >= 0.f) {
        float d = brightness_ - brightnessApplied_;
        if (d < 0.f) d = -d;
        if (d < 0.004f) return;
    }
    brightnessApplied_ = brightness_;
    const float kMul1 = 0.53f, kAdd1 = 0.47f;
    const float kMul2 = 0.35f, kAdd2 = 0.65f;
    const float kF3 = 1.5f, kScale = 239.f, kBias = 16.f, kClamp = 255.f;
    float b = brightness_;
    float termR = b * kMul1 + kAdd1;
    float termG = b * kMul2 + kAdd2;
    for (int sky = 0; sky < 16; ++sky) {
        float rowF = b * curveRow_[sky];
        for (int block = 0; block < 16; ++block) {
            float rf = (curveR_[block] * kF3 + termR * rowF) * kScale + kBias;
            float gf = (curveG_[block] * kF3 + termG * rowF) * kScale + kBias;
            float bf = (curveB_[block] * kF3 + rowF) * kScale + kBias;
            auto ch = [&](float v) -> uint32_t {
                if (v < 0.f) return 0;
                if (v > kClamp) return 255u;
                return (uint32_t)v;
            };
            colorTable_[sky * 16 + block] =
                0xFF000000u | (ch(rf) << 16) | (ch(gf) << 8) | ch(bf);
        }
    }
    tableDirty_ = true;
}

uint32_t LightMap::GetLightColor(uint16_t packed) const {
    // Binary GetLightColor mode scales (verified earlier from .so).
    int sky = LightPack::Sky(packed);
    int block = LightPack::Block(packed);
    // INTENTIONAL: LUT has +16 bias even for (0,0) → grey residual on cave/water
    // faces at night. True unlit cells must be black.
    if (sky == 0 && block == 0)
        return 0xFF000000u;
    uint32_t uVar1 = colorTable_[(sky << 4) | block];
    uint32_t mode = (uint32_t)(packed & LightPack::MODE_MASK);
    int scale;
    if (mode == 0x2000) scale = 0x94;
    else if (mode == 0x2800 || mode == 0x1000) scale = 0xd2;
    else if (mode == 0x1800 || mode == 0) scale = 0xa4;
    else return uVar1; // MODE_0800 (YP): unscaled in binary fall-through
    uint32_t r = ((uVar1 >> 16) & 0xff) * scale >> 8;
    uint32_t g = ((uVar1 >> 8) & 0xff) * scale >> 8;
    uint32_t b = (uVar1 & 0xff) * scale >> 8;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
