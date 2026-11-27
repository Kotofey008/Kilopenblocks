#include "Lighting.h"
// Step 1–2: LightRules (opacity/emission from Material) + heightmap / isSkyLit.
// Propagation still simplified; queue (step 3) not yet. Intentional MCPE-borrowed
// rules adapted to Lite materials/fluids — packing/GetFaceLight/GetLightColor original-compatible.

#include <algorithm>
#include <cstring>
#include <queue>
#include <vector>

// =============================================================================
// LightRules — step 1
// =============================================================================
int LightRules::SignedL(const Material* m) {
    if (!m) return 0;
    // sizeL is stored as uint8 in Material raw; interpret as signed int8.
    uint8_t raw = m->sizeL();
    int v = (raw & 0x80) ? ((int)raw - 256) : (int)raw;
    if (v < -15) v = -15;
    if (v > 15) v = 15;
    return v;
}

int LightRules::Opacity(const GridData& grid, unsigned char type, unsigned short state) {
    // MtlDesc L (user notes):
    //   <0 absorb (relevant when MBI!=0), 0 none, 1..15 emit.
    // FTL/FTW are flow permissions — NOT light opacity (do not use for light).
    if (type == 0) {
        if ((state & GridData::STATE_BIT_2000) != 0)
            return 2; // host water fluid
        return 0;
    }
    const Material* m = grid.GetMaterial((int)type);
    if (!m) return 15;

    int L = SignedL(m);
    uint8_t mbi = m->prop02(); // MBI
    uint8_t cls = m->prop01(); // BMT

    if (L > 0)
        return 0; // emit only; pass-through for propagation

    // Absorb: any L<0 (ice -2, leaves -1, water -2). Magnitude positive only.
    // Floor of light after subtract is 0 — never invert/wrap upward.
    if (L < 0) {
        int absL = -L;
        if (absL < 1) absL = 1;
        if (absL > 15) absL = 15;
        return absL;
    }

    // Non-cube models: light passes (cross/torch/ladder/reed)
    if (cls == 1 || cls == 2 || cls == 3 || cls == 4)
        return 0;

    // No collision → light passes (plants etc.)
    if (!m->col())
        return 0;

    // MBI 1/2 glass-like with L==0: full pass (glass L=0 MBI=1)
    if (mbi == 1 || mbi == 2)
        return 0;

    // Solid full cube (stone/dirt/…)
    return 15;
}

int LightRules::OpacityAt(const GridData& grid, int wx, int wy, int wz) {
    int cidx = grid.CoordToIndex(wx, wy, wz);
    if (cidx < 0) return 0;
    unsigned char t = grid.cellTypes[cidx];
    unsigned short st = (cidx < (int)grid.cellStates.size()) ? grid.cellStates[cidx] : 0;
    return Opacity(grid, t, st);
}

int LightRules::Emission(const GridData& grid, unsigned char type) {
    if (type == 0) return 0;
    const Material* m = grid.GetMaterial((int)type);
    int L = SignedL(m);
    return (L > 0) ? LightPack::Clamp15(L) : 0;
}

int LightRules::EmissionAt(const GridData& grid, int wx, int wy, int wz) {
    int cidx = grid.CoordToIndex(wx, wy, wz);
    if (cidx < 0) return 0;
    return Emission(grid, grid.cellTypes[cidx]);
}

bool LightRules::BlocksSkyHeight(const GridData& grid, unsigned char type, unsigned short state) {
    // Heightmap only stops on full occlusion (opacity 15). Leaves/water reduce
    // sky in-column but do not define "can see sky" threshold (MCPE lightBlock==0 walk).
    return Opacity(grid, type, state) >= 15;
}

// =============================================================================
int LightMap::Index(int x, int y, int z) const {
    int lx = x - ox_, ly = y - oy_, lz = z - oz_;
    if (lx < 0 || ly < 0 || lz < 0 || lx >= sizeX_ || ly >= sizeY_ || lz >= sizeZ_)
        return -1;
    return lx + sizeX_ * (ly + sizeY_ * lz);
}

void LightMap::InitDefaultCurves() {
    // Visual weights tuned so AFTER UpdateLightMap formula:
    //   15==14==13 (plateau), then 12,11,…,0 each a clear step on white blocks.
    // Linear i/15 and the previous 0..12 remap both crushed the top of the range.
    static const float kVis[16] = {
        0.00f, 0.07f, 0.14f, 0.21f, 0.29f, 0.37f, 0.46f, 0.55f,  // 0..7
        0.64f, 0.73f, 0.82f, 0.90f, 0.96f, 1.00f, 1.00f, 1.00f   // 8..12, 13..15
    };
    for (int i = 0; i < 16; ++i)
        curveR_[i] = curveG_[i] = curveB_[i] = curveRow_[i] = kVis[i];
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
    skyHeight_.assign((size_t)sizeX_ * (size_t)sizeZ_, (uint16_t)sizeY_);
    dirty_ = true;
    InitDefaultCurves();
    brightnessApplied_ = -1.f;
    UpdateLightMap();
    RebuildHeightmap(grid);
    PropagateBlockLights(grid);
}

// =============================================================================
// Step 2: heightmap / isSkyLit
// =============================================================================
void LightMap::RecalcHeightColumn(GridData& grid, int lx, int lz) {
    if (lx < 0 || lz < 0 || lx >= sizeX_ || lz >= sizeZ_) return;
    // MCPE LevelChunk::recalcHeightmapOnly: from top, while block below is
    // transparent (opacity==0), lower y. Result = first Y where cell is sky-lit
    // (ly >= height). If whole column transparent, height = 0.
    // Heightmap stopper = full solid only (opacity>=15). Ice/water/leaves
    // attenuate in the vertical seed but must NOT collapse isSkyLit early —
    // that caused "stack N ice then light jumps back" style inconsistency
    // when mixed with local rebuilds.
    int y = sizeY_;
    while (y > 0) {
        int wx = lx + ox_, wy = (y - 1) + oy_, wz = lz + oz_;
        int cidx = grid.CoordToIndex(wx, wy, wz);
        unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : 0;
        unsigned short st = (cidx >= 0 && cidx < (int)grid.cellStates.size())
                                ? grid.cellStates[cidx] : 0;
        if (LightRules::BlocksSkyHeight(grid, t, st))
            break;
        --y;
    }
    skyHeight_[(size_t)XZIndex(lx, lz)] = (uint16_t)y;
}

void LightMap::RebuildHeightmap(GridData& grid) {
    if (sizeX_ <= 0 || sizeZ_ <= 0) return;
    skyHeight_.assign((size_t)sizeX_ * (size_t)sizeZ_, 0);
    for (int z = 0; z < sizeZ_; ++z)
        for (int x = 0; x < sizeX_; ++x)
            RecalcHeightColumn(grid, x, z);
}

int LightMap::GetSkyHeightLocal(int lx, int lz) const {
    if (lx < 0 || lz < 0 || lx >= sizeX_ || lz >= sizeZ_ || skyHeight_.empty())
        return sizeY_;
    return (int)skyHeight_[(size_t)XZIndex(lx, lz)];
}

int LightMap::GetSkyHeightWorld(int wx, int wz) const {
    return GetSkyHeightLocal(wx - ox_, wz - oz_);
}

bool LightMap::IsSkyLit(int wx, int wy, int wz) const {
    // Above world → sky. Below origin → not.
    if (wy >= oy_ + sizeY_) return true;
    if (wy < oy_) return false;
    int lx = wx - ox_, lz = wz - oz_;
    if (lx < 0 || lz < 0 || lx >= sizeX_ || lz >= sizeZ_) return true; // void sides: open
    int ly = wy - oy_;
    return ly >= GetSkyHeightLocal(lx, lz);
}

// =============================================================================
// Sky column using heightmap + opacity falloff (MCPE recalcHeightmap vertical)
// Horizontal sky flood still deferred (step 3 queue) — avoids border glow.
// =============================================================================

void LightMap::RefreshColumnSky(GridData& grid, int worldOx, int worldOz, int colsX, int colsZ) {
    if (data_.empty() || sizeX_ <= 0) return;
    int lx0 = (worldOx - ox_);
    int lz0 = (worldOz - oz_);
    // Expand to cover colsX/colsZ region columns (16-wide each if worldOx aligned).
    int lx1 = lx0 + (colsX > 0 ? colsX * 16 : 16);
    int lz1 = lz0 + (colsZ > 0 ? colsZ * 16 : 16);
    if (lx0 < 0) lx0 = 0;
    if (lz0 < 0) lz0 = 0;
    if (lx1 > sizeX_) lx1 = sizeX_;
    if (lz1 > sizeZ_) lz1 = sizeZ_;
    if (lx0 >= lx1 || lz0 >= lz1) return;
    PropagateSky(grid, lx0, lx1, 0, sizeY_, lz0, lz1);
}

void LightMap::PropagateSky(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1) {
    (void)ly0;
    (void)ly1;
    if (lx0 < 0) lx0 = 0;
    if (lz0 < 0) lz0 = 0;
    if (lx1 > sizeX_) lx1 = sizeX_;
    if (lz1 > sizeZ_) lz1 = sizeZ_;
    if (lx0 >= lx1 || lz0 >= lz1) return;

    for (int z = lz0; z < lz1; ++z) {
        for (int x = lx0; x < lx1; ++x) {
            RecalcHeightColumn(grid, x, z);

            // Vertical sky: start 15 at top. Each cell: br = max(0, br - opacity).
            // Floor is always 0 — further absorb cannot raise light (no wrap / invert).
            // Solid (op>=15): this cell and entire remainder of column = 0.
            int br = 15;
            for (int y = sizeY_ - 1; y >= 0; --y) {
                int idx = x + sizeX_ * (y + sizeY_ * z);
                int wx = x + ox_, wy = y + oy_, wz = z + oz_;
                int op = LightRules::OpacityAt(grid, wx, wy, wz);
                if (op < 0) op = 0; // belt-and-suspenders
                int blk = LightPack::Block(data_[(size_t)idx]);

                if (op >= 15) {
                    for (int yy = y; yy >= 0; --yy) {
                        int i2 = x + sizeX_ * (yy + sizeY_ * z);
                        int b2 = LightPack::Block(data_[(size_t)i2]);
                        data_[(size_t)i2] = LightPack::Make(0, b2);
                    }
                    break;
                }
                // Saturating subtract — never below 0, never wrap to 15 via &0xf
                br = LightPack::SaturateSub(br, op);
                data_[(size_t)idx] = LightPack::Make(br, blk);
                if (br == 0) {
                    for (int yy = y - 1; yy >= 0; --yy) {
                        int i2 = x + sizeX_ * (yy + sizeY_ * z);
                        int b2 = LightPack::Block(data_[(size_t)i2]);
                        data_[(size_t)i2] = LightPack::Make(0, b2);
                    }
                    break;
                }
            }
        }
    }
}

// =============================================================================
// Block channel (unchanged structure; uses LightRules::Emission / Opacity)
// =============================================================================
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

    for (int z = lz0; z < lz1; ++z)
        for (int y = ly0; y < ly1; ++y)
            for (int x = lx0; x < lx1; ++x) {
                int emit = LightRules::EmissionAt(grid, x + ox_, y + oy_, z + oz_);
                if (emit > 0) setBlk(x, y, z, emit);
            }

    // Border seed from outside AABB (existing block light)
    auto pullOutside = [&](int x, int y, int z, int ox2, int oy2, int oz2) {
        if (!inR(x, y, z)) return;
        if (ox2 >= lx0 && ox2 < lx1 && oy2 >= ly0 && oy2 < ly1 && oz2 >= lz0 && oz2 < lz1)
            return;
        if (ox2 < 0 || oy2 < 0 || oz2 < 0 || ox2 >= sizeX_ || oy2 >= sizeY_ || oz2 >= sizeZ_)
            return;
        int oiLocal = ox2 + sizeX_ * (oy2 + sizeY_ * oz2);
        int b = LightPack::Block(data_[(size_t)oiLocal]);
        if (b <= 1) return;
        int op = LightRules::OpacityAt(grid, x + ox_, y + oy_, z + oz_);
        if (op >= 15) return;
        int step = (op <= 0) ? 1 : op; // MCPE: transparent still costs 1 to spread
        int nl = LightPack::SaturateSub(b, step);
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
            int op = LightRules::OpacityAt(grid, nx + ox_, ny + oy_, nz + oz_);
            if (op >= 15) continue;
            int step = (op <= 0) ? 1 : op;
            int nl = LightPack::SaturateSub(level, step);
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
    RebuildHeightmap(grid);
    RebuildRegion(grid, 0, sizeX_, 0, sizeY_, 0, sizeZ_);
}

void LightMap::PropagateBlockLightsLocal(GridData& grid, int minX, int maxX, int minY, int maxY,
                                         int minZ, int maxZ) {
    int lx0 = minX - ox_; if (lx0 < 0) lx0 = 0;
    int lx1 = maxX - ox_; if (lx1 > sizeX_) lx1 = sizeX_;
    int ly0 = minY - oy_; if (ly0 < 0) ly0 = 0;
    int ly1 = maxY - oy_; if (ly1 > sizeY_) ly1 = sizeY_;
    int lz0 = minZ - oz_; if (lz0 < 0) lz0 = 0;
    int lz1 = maxZ - oz_; if (lz1 > sizeZ_) lz1 = sizeZ_;
    if (lx0 >= lx1 || lz0 >= lz1) return;
    // Height + sky for columns; block in Y band
    for (int z = lz0; z < lz1; ++z)
        for (int x = lx0; x < lx1; ++x)
            RecalcHeightColumn(grid, x, z);
    PropagateSky(grid, lx0, lx1, 0, sizeY_, lz0, lz1);
    if (ly0 < ly1)
        PropagateBlock(grid, lx0, lx1, ly0, ly1, lz0, lz1);
}

void LightMap::UpdateSunRays(GridData& grid) {
    RebuildHeightmap(grid);
    PropagateSky(grid, 0, sizeX_, 0, sizeY_, 0, sizeZ_);
}

void LightMap::UpdateSunRaysLocal(GridData& grid, int minX, int maxX, int minZ, int maxZ) {
    int lx0 = minX - ox_; if (lx0 < 0) lx0 = 0;
    int lx1 = maxX - ox_; if (lx1 > sizeX_) lx1 = sizeX_;
    int lz0 = minZ - oz_; if (lz0 < 0) lz0 = 0;
    int lz1 = maxZ - oz_; if (lz1 > sizeZ_) lz1 = sizeZ_;
    for (int z = lz0; z < lz1; ++z)
        for (int x = lx0; x < lx1; ++x)
            RecalcHeightColumn(grid, x, z);
    PropagateSky(grid, lx0, lx1, 0, sizeY_, lz0, lz1);
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

uint16_t LightMap::GetLight(int x, int y, int z) const {
    int i = Index(x, y, z);
    if (i < 0) {
        if (y >= oy_ + sizeY_) return LightPack::Make(15, 0);
        return 0;
    }
    // Re-pack through Clamp15 so corrupt/negative never surfaces as bright nibbles.
    uint16_t v = data_[(size_t)i];
    return LightPack::Make(LightPack::Sky(v), LightPack::Block(v), LightPack::Mode(v));
}

uint16_t LightMap::GetFaceLight(int x, int y, int z, int face) const {
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
    // GLES color arrays ignore glColor — day/night must live in the LUT that
    // GetLightColor reads when meshes are (re)built. Rebuild when brightness
    // moves enough; Frame remeshes a few view columns (not full world).
    if (brightnessApplied_ >= 0.f) {
        float d = brightness_ - brightnessApplied_;
        if (d < 0.f) d = -d;
        if (d < 0.14f) return; // fewer day/night remesh steps → less hitch
    }
    brightnessApplied_ = brightness_;
    const float kMul1 = 0.53f, kAdd1 = 0.47f;
    const float kMul2 = 0.35f, kAdd2 = 0.65f;
    const float kF3 = 1.5f, kScale = 239.f, kBias = 16.f, kClamp = 255.f;
    float b = brightness_;
    if (b < 0.15f) b = 0.15f; // keep torches readable at midnight
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
    int sky = LightPack::Sky(packed);
    int block = LightPack::Block(packed);
    if (sky == 0 && block == 0)
        return 0xFF000000u;
    uint32_t uVar1 = colorTable_[(sky << 4) | block];
    uint32_t mode = (uint32_t)(packed & LightPack::MODE_MASK);
    int scale;
    if (mode == 0x2000) scale = 0x94;
    else if (mode == 0x2800 || mode == 0x1000) scale = 0xd2;
    else if (mode == 0x1800 || mode == 0) scale = 0xa4;
    else return uVar1;
    uint32_t r = ((uVar1 >> 16) & 0xff) * scale >> 8;
    uint32_t g = ((uVar1 >> 8) & 0xff) * scale >> 8;
    uint32_t b = (uVar1 & 0xff) * scale >> 8;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
