#include "Lighting.h"
// Light storage (AddLight @ 0x95b90 / AddLightUpdate @ 0x94b60):
//   per-cell value: low nibble 0x0f + high nibble 0xf0 (sky/block hypothesis)
//   hierarchy maps at +0x280.. use value | 0x8000 as dirty/valid flag
// Mode bits applied in GetLight @ 0x94480 by face arg (4th param):
//   face 5: or ah,0x28 → mode 0x2800; face 4: returns 0x200e (mode 0x2000)
// GetLightColor @ 0x947e0 only consumes the ushort from GetLight (AddBlockFace callers)
// consumption (RGB scale). No OR of these constants found in AddLight/SetAddLights
// bodies examined so far — assignment site still open (may be external context:
// time-of-day / biome applied at color lookup, not stored per cell).

#include "TreeBuilder.h"
#include <algorithm>
#include <cstring>
#include <queue>

int LightMap::Index(int x, int y, int z) const {
    int lx = x - ox_, ly = y - oy_, lz = z - oz_;
    if (lx < 0 || ly < 0 || lz < 0 || lx >= sizeX_ || ly >= sizeY_ || lz >= sizeZ_)
        return -1;
    return lx + sizeX_ * (ly + sizeY_ * lz);
}

void LightMap::Init(GridData& grid) {
    ox_ = grid.originX; oy_ = grid.originY; oz_ = grid.originZ;
    sizeX_ = grid.sizeX > 0 ? grid.sizeX : (int)(grid.worldMax.x - grid.worldMin.x);
    sizeY_ = grid.sizeY > 0 ? grid.sizeY : (int)(grid.worldMax.y - grid.worldMin.y);
    sizeZ_ = grid.sizeZ > 0 ? grid.sizeZ : (int)(grid.worldMax.z - grid.worldMin.z);
    if (sizeX_ <= 0) { ox_ = (int)grid.worldMin.x; sizeX_ = (int)(grid.worldMax.x - grid.worldMin.x); }
    if (sizeY_ <= 0) { oy_ = (int)grid.worldMin.y; sizeY_ = (int)(grid.worldMax.y - grid.worldMin.y); }
    if (sizeZ_ <= 0) { oz_ = (int)grid.worldMin.z; sizeZ_ = (int)(grid.worldMax.z - grid.worldMin.z); }
    data_.assign((size_t)sizeX_ * sizeY_ * sizeZ_, 0);
    dirty_ = true;
    InitDefaultCurves();
    brightnessApplied_ = -1.f;
    UpdateLightMap();
    UpdateSunRays(grid);
    PropagateBlockLights(grid);
}

static bool IsOpaque(unsigned char t) {
    // Liquid is type 0 + state — not a solid type id.
    if (t == BlockId::Air) return false;
    if (t == BlockId::Leaves1 || t == BlockId::Leaves2) return false;
    if (t == BlockId::Flower1 || t == BlockId::Mushroom1 || t == BlockId::Mushroom2) return false;
    if (t == 11) return false; // Glass
    if (t == 17 || t == 18) return false; // Torch / Active Torch
    return true;
}

// Emission level for light sources (from BMT/block type evidence)
static int EmissionLevel(unsigned char t) {
    if (t == 18) return 14; // Active Torch (observed AddTorch)
    if (t == 17) return 14; // Torch
    // Lava if present as fluid type with emission — check later
    return 0;
}

void LightMap::UpdateSunRays(GridData& grid) {
    // Fast path: lightmap shares origin/size with grid → direct index into cellTypes
    const bool aligned = (ox_ == grid.originX && oy_ == grid.originY && oz_ == grid.originZ &&
                          sizeX_ == grid.sizeX && sizeY_ == grid.sizeY && sizeZ_ == grid.sizeZ &&
                          !grid.cellTypes.empty());
    for (int z = 0; z < sizeZ_; ++z) {
        for (int x = 0; x < sizeX_; ++x) {
            int sky = 15;
            for (int y = sizeY_ - 1; y >= 0; --y) {
                int idx = x + sizeX_ * (y + sizeY_ * z);
                unsigned char t;
                int cidx = -1;
                if (aligned) {
                    t = grid.cellTypes[(size_t)idx];
                    cidx = idx;
                } else {
                    cidx = grid.CoordToIndex(x + ox_, y + oy_, z + oz_);
                    t = (cidx >= 0) ? grid.cellTypes[cidx] : BlockId::Air;
                }
                if (IsOpaque(t)) {
                    sky = 0;
                } else if (t == BlockId::Leaves1 || t == BlockId::Leaves2) {
                    if (sky > 0) sky -= 1;
                } else if (cidx >= 0 && cidx < (int)grid.cellStates.size() &&
                           (grid.cellStates[cidx] & 0x3fc0) != 0) {
                    if (sky > 0) sky -= 1;
                }
                int block = LightPack::Block(data_[idx]);
                data_[idx] = LightPack::Make(sky, block);
            }
        }
    }
    dirty_ = false;
}

void LightMap::UpdateSunRaysLocal(GridData& grid, int minX, int maxX, int minZ, int maxZ) {
    if (data_.empty() || sizeX_ <= 0 || sizeY_ <= 0 || sizeZ_ <= 0) return;
    // Clamp to lightmap bounds (local indices)
    int lx0 = minX - ox_; if (lx0 < 0) lx0 = 0;
    int lx1 = maxX - ox_; if (lx1 > sizeX_) lx1 = sizeX_;
    int lz0 = minZ - oz_; if (lz0 < 0) lz0 = 0;
    int lz1 = maxZ - oz_; if (lz1 > sizeZ_) lz1 = sizeZ_;
    if (lx0 >= lx1 || lz0 >= lz1) return;

    for (int z = lz0; z < lz1; ++z) {
        for (int x = lx0; x < lx1; ++x) {
            int sky = 15;
            for (int y = sizeY_ - 1; y >= 0; --y) {
                int idx = x + sizeX_ * (y + sizeY_ * z);
                int wx = x + ox_, wy = y + oy_, wz = z + oz_;
                int cidx = grid.CoordToIndex(wx, wy, wz);
                unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : BlockId::Air;
                if (IsOpaque(t)) {
                    sky = 0;
                } else if (t == BlockId::Leaves1 || t == BlockId::Leaves2) {
                    if (sky > 0) sky -= 1;
                } else if (cidx >= 0 && cidx < (int)grid.cellStates.size() &&
                           (grid.cellStates[cidx] & 0x3fc0) != 0) {
                    if (sky > 0) sky -= 1;
                }
                int block = LightPack::Block(data_[idx]);
                data_[idx] = LightPack::Make(sky, block);
            }
        }
    }
}

void LightMap::AddLight(int x, int y, int z, uint16_t packed) {
    // Matches GridData::AddLight(x,y,z,ushort) — seeds block-light channel
    int i = Index(x, y, z);
    if (i < 0) return;
    int newBlock = LightPack::Block(packed);
    int sky = LightPack::Sky(data_[i]);
    int cur = LightPack::Block(data_[i]);
    if (newBlock > cur)
        data_[i] = LightPack::Make(sky, newBlock);
}

void LightMap::PropagateBlockLights(GridData& grid) {
    // BFS outward from emitters; level decreases by 1 per step through air.
    // Only scan cellTypes for non-zero types that can emit — skip pure air.
    std::queue<int> q;
    const size_t n = grid.cellTypes.size();
    for (size_t ci = 0; ci < n; ++ci) {
        unsigned char t = grid.cellTypes[ci];
        if (t == 0) continue;
        int emit = EmissionLevel(t);
        if (emit <= 0) continue;
        // Decode linear index → lightmap local
        int lx = (int)(ci % (size_t)grid.sizeX);
        int t1 = (int)(ci / (size_t)grid.sizeX);
        int ly = t1 % grid.sizeY;
        int lz = t1 / grid.sizeY;
        int wx = lx + grid.originX, wy = ly + grid.originY, wz = lz + grid.originZ;
        int idx = Index(wx, wy, wz);
        if (idx < 0) continue;
        int sky = LightPack::Sky(data_[idx]);
        data_[idx] = LightPack::Make(sky, emit);
        q.push(idx);
    }
    if (q.empty()) return; // common case: no torches at world gen

    const int dx[6] = {1,-1,0,0,0,0};
    const int dy[6] = {0,0,1,-1,0,0};
    const int dz[6] = {0,0,0,0,1,-1};

    while (!q.empty()) {
        int idx = q.front(); q.pop();
        int level = LightPack::Block(data_[idx]);
        if (level <= 1) continue;
        int lx = idx % sizeX_;
        int ly = (idx / sizeX_) % sizeY_;
        int lz = idx / (sizeX_ * sizeY_);
        for (int d = 0; d < 6; ++d) {
            int nx = lx + dx[d], ny = ly + dy[d], nz = lz + dz[d];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= sizeX_ || ny >= sizeY_ || nz >= sizeZ_)
                continue;
            int nidx = nx + sizeX_ * (ny + sizeY_ * nz);
            int wx = nx + ox_, wy = ny + oy_, wz = nz + oz_;
            int cidx = grid.CoordToIndex(wx, wy, wz);
            unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : BlockId::Air;
            if (IsOpaque(t)) continue;
            int newLevel = level - 1;
            int cur = LightPack::Block(data_[nidx]);
            if (newLevel > cur) {
                int sky = LightPack::Sky(data_[nidx]);
                data_[nidx] = LightPack::Make(sky, newLevel);
                q.push(nidx);
            }
        }
    }
}

uint16_t LightMap::GetLight(int x, int y, int z) const {
    int i = Index(x, y, z);
    if (i < 0) return LightPack::Make(15, 0);
    return data_[i];
}

uint16_t LightMap::GetFaceLight(int x, int y, int z, int face) const {
    // Sample neighbor cell (binary GetLight face arg selects adjacent cell + mode).
    int nx = x, ny = y, nz = z;
    // ourFace: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
    // binary face: 0=XP 1=YP 2=ZP 3=XN 4=YN 5=ZN — mode OR on face:
    //   bin face 5 (ZN): mode 0x2800; bin face 4 (YN): mode 0x2000
    uint16_t mode = 0;
    switch (face) {
        case 0: nx++; break;
        case 1: nx--; break;
        case 2: ny++; break;
        case 3: ny--; mode = LightPack::MODE_2000; break; // ~YN
        case 4: nz++; break;
        case 5: nz--; mode = LightPack::MODE_2800; break; // ~ZN
    }
    uint16_t v = GetLight(nx, ny, nz);
    return (uint16_t)((v & ~LightPack::MODE_MASK) | mode);
}

void LightMap::InitDefaultCurves() {
    // Binary stores 16 floats at +0xf4 / +0x134 / +0x174 / +0x1b4.
    // Ctor does not fill them; linear 0..1 ramps produce a valid UpdateLightMap table.
    for (int i = 0; i < 16; ++i) {
        float t = (float)i / 15.f;
        curveR_[i] = t;
        curveG_[i] = t;
        curveB_[i] = t;
        curveRow_[i] = t;
    }
}

void LightMap::SetBrightness(float b) {
    if (b < 0.f) b = 0.f;
    if (b > 1.f) b = 1.f;
    brightness_ = b;
}

void LightMap::UpdateLightMap() {
    // Binary GridData::UpdateLightMap @ 0x94920 (objdump-verified constants):
    //   skip if brightness (+0x1f4) == applied (+0x1f8)
    //   mul1=0.53 add1=0.47  mul2=0.35 add2=0.65  f3=1.5  scale=239  bias=16  clamp=255
    //   for row j, col i:
    //     rowF = brightness * curveRow[j]
    //     R = (curveR[i]*1.5 + (brightness*0.53+0.47)*rowF) * 239 + 16
    //     G = (curveG[i]*1.5 + (brightness*0.35+0.65)*rowF) * 239 + 16
    //     B = (curveB[i]*1.5 + rowF) * 239 + 16
    //   index in table = j*16+i  matching GetLightColor (sky<<4)|block with sky=j block=i
    //   wait: index = (sky)*16 + block → outer sky (row), inner block (col)
    if (brightness_ == brightnessApplied_) return;
    brightnessApplied_ = brightness_;
    const float kMul1 = 0.53f, kAdd1 = 0.47f;
    const float kMul2 = 0.35f, kAdd2 = 0.65f;
    const float kF3 = 1.5f;
    const float kScale = 239.f, kBias = 16.f, kClamp = 255.f;
    float b = brightness_;
    float termR = b * kMul1 + kAdd1; // xmm after mul/add for R path
    float termG = b * kMul2 + kAdd2;
    for (int sky = 0; sky < 16; ++sky) {
        float rowF = b * curveRow_[sky];
        float rBase = termR * rowF;
        float gBase = termG * rowF;
        float bBase = rowF;
        for (int block = 0; block < 16; ++block) {
            float rf = (curveR_[block] * kF3 + rBase) * kScale + kBias;
            float gf = (curveG_[block] * kF3 + gBase) * kScale + kBias;
            float bf = (curveB_[block] * kF3 + bBase) * kScale + kBias;
            auto ch = [&](float v) -> uint32_t {
                if (v < 0.f) return 0;
                if (v > kClamp) return 255u;
                return (uint32_t)v;
            };
            uint32_t R = ch(rf), G = ch(gf), B = ch(bf);
            // ARGB as packed in binary OR path: A fixed 0xFF, R in low? GetLightColor
            // returns (uVar1 & 0xff) as blue-ish channel with *scale — Windows-style
            // 0xAARRGGBB with R in bits 16-23. Binary scales:
            //   (uVar1 & 0xff) , (uVar1 & 0xff00), (uVar1 & 0xff0000)
            // Store 0xFFBBGGRR is wrong; use 0xAARRGGBB: R<<16|G<<8|B
            colorTable_[sky * 16 + block] = 0xFF000000u | (R << 16) | (G << 8) | B;
        }
    }
    tableDirty_ = true;
}

uint32_t LightMap::GetLightColor(uint16_t packed) const {
    // Binary GetLightColor @ 0x947e0:
    //   uVar1 = table[(sky*16)+block]
    //   mode = packed & 0x3800
    //   0 / 0x1800 → *0xA4 ; 0x1000/0x2800 → *0xD2 ; 0x2000 → *0x94
    int sky = LightPack::Sky(packed);
    int block = LightPack::Block(packed);
    uint32_t uVar1 = colorTable_[(sky << 4) | block];
    uint32_t mode = (uint32_t)(packed & LightPack::MODE_MASK);
    int scale;
    if (mode == 0x2000)
        scale = 0x94;
    else if (mode == 0x2800 || mode == 0x1000)
        scale = 0xd2;
    else if (mode == 0x1800 || mode == 0)
        scale = 0xa4;
    else
        return uVar1;
    uint32_t r = ((uVar1 >> 16) & 0xff) * scale >> 8;
    uint32_t g = ((uVar1 >> 8) & 0xff) * scale >> 8;
    uint32_t b = (uVar1 & 0xff) * scale >> 8;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
