#include "Lighting.h"
// Binary-aligned lighting (P2):
//   UpdateSunRays builds skyHeight_ (+0x68) and sky nibble
//   InvalidateSunRays marks sunRayDirty_ (+0x84)
//   SetAddLights seeds lightQueue_ from column height / emitters
//   AddLightUpdate recomputes one cell (L + state 0x2000/0x3c0)
//   AddLights drains queue (binary hierarchy walk → queue drain)

#include "TreeBuilder.h"
#include <algorithm>
#include <cstring>
#include <cmath>

int LightMap::Index(int x, int y, int z) const {
    int lx = x - ox_, ly = y - oy_, lz = z - oz_;
    if (lx < 0 || ly < 0 || lz < 0 || lx >= sizeX_ || ly >= sizeY_ || lz >= sizeZ_)
        return -1;
    return lx + sizeX_ * (ly + sizeY_ * lz);
}

void LightMap::EnsureAuxSize() {
    size_t xz = (size_t)sizeX_ * (size_t)sizeZ_;
    if (skyHeight_.size() != xz) skyHeight_.assign(xz, (uint8_t)sizeY_);
    if (sunRayDirty_.size() != xz) sunRayDirty_.assign(xz, 1);
    if (inQueue_.size() != data_.size()) inQueue_.assign(data_.size(), 0);
    regionCountX_ = (sizeX_ + 15) >> 4;
    regionCountZ_ = (sizeZ_ + 15) >> 4;
}

void LightMap::Enqueue(int lightIdx) {
    if (lightIdx < 0 || lightIdx >= (int)data_.size()) return;
    if (lightIdx >= (int)inQueue_.size()) inQueue_.resize(data_.size(), 0);
    if (inQueue_[(size_t)lightIdx]) return;
    inQueue_[(size_t)lightIdx] = 1;
    lightQueue_.push(lightIdx);
}

void LightMap::InitDefaultCurves() {
    for (int i = 0; i < 16; ++i) {
        float t = (float)i / 15.f;
        curveR_[i] = t;
        curveG_[i] = t;
        curveB_[i] = t;
        curveRow_[i] = t;
    }
}

static int SignedL(const Material* m) {
    if (!m) return 0;
    int v = (int)(int8_t)m->sizeL();
    if (v < -15) v = -15;
    if (v > 15) v = 15;
    return v;
}

static bool IsOpaque(const GridData& grid, unsigned char t) {
    if (t == 0) return false;
    const Material* m = grid.GetMaterial((int)t);
    if (!m) return t != 0;
    uint8_t cls = m->prop01();
    if (cls == 1 || cls == 2 || cls == 4) return false;
    if (!m->col()) return false;
    if (SignedL(m) < 0) return false;
    return true;
}

static int EmissionLevel(const GridData& grid, unsigned char t) {
    if (t == 0) return 0;
    const Material* m = grid.GetMaterial((int)t);
    if (!m) return 0;
    int L = SignedL(m);
    if (L < 1) return 0;
    return LightPack::Clamp15(L);
}

static int ApplySkyL(int skyLevel, int signedL) {
    if (signedL >= 0) return LightPack::Clamp15(skyLevel);
    return LightPack::Clamp15(skyLevel + signedL);
}

// ---------------------------------------------------------------------------
// Init / InvalidateLighting path
// ---------------------------------------------------------------------------
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
    EnsureAuxSize();
    UpdateLightMap();
    // InvalidateLighting: zero lights, UpdateSunRays all, SetAddLights all, AddLights
    PropagateBlockLights(grid);
}

// ---------------------------------------------------------------------------
// UpdateSunRays — sky height map + sky nibble (binary 0x92440 column walk)
// ---------------------------------------------------------------------------
void LightMap::UpdateSunRaysColumn(GridData& grid, int lx, int lz) {
    if (lx < 0 || lz < 0 || lx >= sizeX_ || lz >= sizeZ_) return;
    int sky = 15;
    int firstOpaque = sizeY_; // height map: y of first opaque from top, or sizeY if none
    for (int y = sizeY_ - 1; y >= 0; --y) {
        int idx = lx + sizeX_ * (y + sizeY_ * lz);
        int wx = lx + ox_, wy = y + oy_, wz = lz + oz_;
        int cidx = grid.CoordToIndex(wx, wy, wz);
        unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : 0;
        if (IsOpaque(grid, t)) {
            if (firstOpaque == sizeY_) firstOpaque = y;
            sky = 0;
        } else {
            const Material* mm = (t != 0) ? grid.GetMaterial((int)t) : nullptr;
            int sL = 0;
            if (t == 0 && cidx >= 0 && cidx < (int)grid.cellStates.size() &&
                (grid.cellStates[cidx] & 0x3fc0) != 0)
                sL = -2;
            else if (mm)
                sL = SignedL(mm);
            sky = ApplySkyL(sky, sL);
        }
        sky = LightPack::Clamp15(sky);
        int block = LightPack::Block(data_[(size_t)idx]);
        data_[(size_t)idx] = LightPack::Make(sky, block);
    }
    skyHeight_[(size_t)XZIndex(lx, lz)] = (uint8_t)(firstOpaque > 255 ? 255 : firstOpaque);
    sunRayDirty_[(size_t)XZIndex(lx, lz)] = 0;
}

void LightMap::UpdateSunRays(GridData& grid) {
    EnsureAuxSize();
    for (int z = 0; z < sizeZ_; ++z)
        for (int x = 0; x < sizeX_; ++x)
            UpdateSunRaysColumn(grid, x, z);
    dirty_ = false;
}

void LightMap::UpdateSunRaysLocal(GridData& grid, int minX, int maxX, int minZ, int maxZ) {
    EnsureAuxSize();
    int lx0 = minX - ox_; if (lx0 < 0) lx0 = 0;
    int lx1 = maxX - ox_; if (lx1 > sizeX_) lx1 = sizeX_;
    int lz0 = minZ - oz_; if (lz0 < 0) lz0 = 0;
    int lz1 = maxZ - oz_; if (lz1 > sizeZ_) lz1 = sizeZ_;
    for (int z = lz0; z < lz1; ++z)
        for (int x = lx0; x < lx1; ++x)
            UpdateSunRaysColumn(grid, x, z);
}

void LightMap::InvalidateSunRays(int worldX, int worldZ) {
    // Binary InvalidateSunRays(param_1, param_2): region column coords, marks 16×16
    // height cells 0xff and dirty flag at +0x84.
    EnsureAuxSize();
    int lx0 = (worldX - ox_) & ~15;
    int lz0 = (worldZ - oz_) & ~15;
    for (int z = lz0; z < lz0 + 16 && z < sizeZ_; ++z) {
        if (z < 0) continue;
        for (int x = lx0; x < lx0 + 16 && x < sizeX_; ++x) {
            if (x < 0) continue;
            skyHeight_[(size_t)XZIndex(x, z)] = 0xff;
            sunRayDirty_[(size_t)XZIndex(x, z)] = 1;
        }
    }
}

int LightMap::GetSkyHeight(int wx, int wz) const {
    int lx = wx - ox_, lz = wz - oz_;
    if (lx < 0 || lz < 0 || lx >= sizeX_ || lz >= sizeZ_) return sizeY_;
    if (skyHeight_.empty()) return sizeY_;
    return (int)skyHeight_[(size_t)XZIndex(lx, lz)];
}

// ---------------------------------------------------------------------------
// AddLightUpdate @ 0x94b60 — single cell from neighbours + L + state
// ---------------------------------------------------------------------------
bool LightMap::AddLightUpdate(GridData& grid, int x, int y, int z) {
    int idx = Index(x, y, z);
    if (idx < 0) return false;
    int cidx = grid.CoordToIndex(x, y, z);
    if (cidx < 0) return false;

    unsigned char t = grid.cellTypes[cidx];
    unsigned short st = (cidx < (int)grid.cellStates.size()) ? grid.cellStates[cidx] : 0;

    // Binary: type!=0 && FTL(+0x1b)==0 → keep current light, clear hierarchy dirty only
    if (t != 0) {
        const Material* mat = grid.GetMaterial((int)t);
        if (mat && mat->flowPerm(0) == 0) {
            // Solids store 0; faces sample outside via GetFaceLight
            uint16_t keep = data_[(size_t)idx] & 0x00ff;
            // still allow emission if L>0 even on "solid" with FTL0? Binary early-outs
            // before L. Emitters have FTL != 0.
            return false;
        }
    }

    int sky = 0, block = 0;

    auto take = [&](int nx, int ny, int nz, GridData::FlowType outF, GridData::FlowType inF) {
        int ni = grid.CoordToIndex(nx, ny, nz);
        if (ni < 0) {
            // Outside +Y = open sky (binary y+1 >= sizeY)
            if (ny >= oy_ + sizeY_ && sky < 15) sky = 15;
            return;
        }
        if (t != 0 && grid.IsFlowAllowed(cidx, outF) == 0) return;
        unsigned char nt = grid.cellTypes[ni];
        if (nt != 0 && grid.IsFlowAllowed(ni, inF) == 0) return;
        uint16_t lv = GetLight(nx, ny, nz);
        int ns = LightPack::Sky(lv);
        int nb = LightPack::Block(lv);
        if (ns > sky) sky = ns;
        if (nb > block) block = nb;
    };

    take(x + 1, y, z, GridData::FlowXP, GridData::FlowXN);
    take(x - 1, y, z, GridData::FlowXN, GridData::FlowXP);
    take(x, y + 1, z, GridData::FlowYP, GridData::FlowYN);
    take(x, y - 1, z, GridData::FlowYN, GridData::FlowYP);
    take(x, y, z + 1, GridData::FlowZP, GridData::FlowZN);
    take(x, y, z - 1, GridData::FlowZN, GridData::FlowZP);

    // Also: if this cell is at/above sky height column open — seed sky from height map
    // (GetLightInside path: y relative to +0x68 height).
    int h = GetSkyHeight(x, z);
    // h = first opaque y in lightmap local; world y = h + oy_
    // Cells with local ly >= h are "above/at first solid from top" when h was found walking
    // from top — actually firstOpaque is the y index of first solid. Air above it has ly > firstOpaque.
    int ly = y - oy_;
    if (ly > h && sky < 15) {
        // Between top of world and first solid — open sky column already filled by UpdateSunRays;
        // neighbour gather should have gotten it. Reinforce open top:
        if (ly >= sizeY_ - 1) sky = 15;
    }

    // Decrement
    if (sky > 0) sky -= 1;
    if (block > 0) block -= 1;

    // Material L (+0x1a)
    int sL = 0;
    if (t != 0) sL = SignedL(grid.GetMaterial((int)t));
    if (sL < 0) {
        int al = -sL;
        sky = (sky > al) ? sky - al : 0;
        block = (block > al) ? block - al : 0;
    } else if (sL > 0) {
        if (block < sL) block = sL;
    }

    // State branches
    uint16_t packed;
    if ((st & GridData::STATE_BIT_2000) != 0) {
        int atten = ((st & 0x1000) != 0) ? 1 : 2;
        int os = (sky > atten) ? sky - atten : 0;
        int ob = (block > atten) ? block - atten : 0;
        packed = LightPack::Make(os, ob);
    } else if ((st & GridData::STATE_BIT_3C0) != 0) {
        packed = LightPack::Make(15, 15);
    } else {
        packed = LightPack::Make(sky, block);
    }

    uint16_t old = data_[(size_t)idx] & 0x00ff;
    if (packed == old) return false;
    data_[(size_t)idx] = packed;
    return true;
}

void LightMap::AddLight(GridData& grid, int x, int y, int z, uint16_t packed) {
    int i = Index(x, y, z);
    if (i < 0) return;
    int newBlock = LightPack::Block(packed);
    int sky = LightPack::Sky(data_[(size_t)i]);
    int cur = LightPack::Block(data_[(size_t)i]);
    if (newBlock > cur) {
        data_[(size_t)i] = LightPack::Make(sky, newBlock);
        Enqueue(i);
        // Binary also marks 6-neighbour hierarchy dirty via |0x8000
        const int d[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (int k = 0; k < 6; ++k)
            Enqueue(Index(x + d[k][0], y + d[k][1], z + d[k][2]));
    }
    (void)grid;
}

// ---------------------------------------------------------------------------
// SetAddLights(rx, rz) — seed queue for 16×16 column from height + emitters
// ---------------------------------------------------------------------------
void LightMap::SetAddLights(GridData& grid, int regionX, int regionZ) {
    // regionX/Z are world-region indices (floor div 16 of world coords) OR
    // local region indices 0..count-1. Binary uses param as region column index
    // into maps sized by +0x58/+0x60. We accept world-aligned region origin.
    EnsureAuxSize();
    int lx0 = regionX * 16;
    int lz0 = regionZ * 16;
    // If args look like world coords already aligned:
    if (regionX >= sizeX_ || regionZ >= sizeZ_) {
        lx0 = (regionX - ox_) & ~15;
        lz0 = (regionZ - oz_) & ~15;
    }
    if (lx0 < 0) lx0 = 0;
    if (lz0 < 0) lz0 = 0;

    for (int lz = lz0; lz < lz0 + 16 && lz < sizeZ_; ++lz) {
        for (int lx = lx0; lx < lx0 + 16 && lx < sizeX_; ++lx) {
            // Ensure sky column current
            if (sunRayDirty_[(size_t)XZIndex(lx, lz)])
                UpdateSunRaysColumn(grid, lx, lz);

            for (int ly = 0; ly < sizeY_; ++ly) {
                int idx = lx + sizeX_ * (ly + sizeY_ * lz);
                int wx = lx + ox_, wy = ly + oy_, wz = lz + oz_;
                int cidx = grid.CoordToIndex(wx, wy, wz);
                if (cidx < 0) continue;
                unsigned char t = grid.cellTypes[cidx];
                int emit = EmissionLevel(grid, t);
                if (emit > 0) {
                    int sky = LightPack::Sky(data_[(size_t)idx]);
                    data_[(size_t)idx] = LightPack::Make(sky, emit);
                    Enqueue(idx);
                }
                // Queue cells that have any sky or may need neighbour pull
                if (LightPack::Sky(data_[(size_t)idx]) > 0 || emit > 0)
                    Enqueue(idx);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// AddLights — drain queue (binary walks hierarchy then processes queue entries)
// ---------------------------------------------------------------------------
void LightMap::AddLights(GridData& grid, int maxSteps) {
    EnsureAuxSize();
    int steps = 0;
    const int d[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    while (!lightQueue_.empty() && steps < maxSteps) {
        int idx = lightQueue_.front();
        lightQueue_.pop();
        if (idx >= 0 && idx < (int)inQueue_.size())
            inQueue_[(size_t)idx] = 0;
        ++steps;

        int lx = idx % sizeX_;
        int ly = (idx / sizeX_) % sizeY_;
        int lz = idx / (sizeX_ * sizeY_);
        int wx = lx + ox_, wy = ly + oy_, wz = lz + oz_;

        if (!AddLightUpdate(grid, wx, wy, wz))
            continue;

        // Neighbours need recompute (binary AddLight marks them via hierarchy)
        for (int k = 0; k < 6; ++k) {
            int nidx = Index(wx + d[k][0], wy + d[k][1], wz + d[k][2]);
            Enqueue(nidx);
        }
    }
}

void LightMap::PropagateBlockLights(GridData& grid) {
    EnsureAuxSize();
    // Zero block channel, keep structure
    for (size_t i = 0; i < data_.size(); ++i) {
        int sky = LightPack::Sky(data_[i]);
        data_[i] = LightPack::Make(sky, 0);
    }
    while (!lightQueue_.empty()) lightQueue_.pop();
    inQueue_.assign(data_.size(), 0);

    UpdateSunRays(grid);
    // SetAddLights for every region column
    int rcX = (sizeX_ + 15) >> 4;
    int rcZ = (sizeZ_ + 15) >> 4;
    for (int rz = 0; rz < rcZ; ++rz)
        for (int rx = 0; rx < rcX; ++rx)
            SetAddLights(grid, rx, rz);
    AddLights(grid, 1 << 24);
}

void LightMap::PropagateBlockLightsLocal(GridData& grid, int minX, int maxX, int minY, int maxY,
                                         int minZ, int maxZ) {
    EnsureAuxSize();
    const int margin = 15;
    int x0 = minX - margin, x1 = maxX + margin;
    int y0 = minY - margin, y1 = maxY + margin;
    int z0 = minZ - margin, z1 = maxZ + margin;

    // Invalidate + update sun rays for covered columns
    for (int wz = z0; wz < z1; wz += 16)
        for (int wx = x0; wx < x1; wx += 16)
            InvalidateSunRays(wx, wz);
    UpdateSunRaysLocal(grid, x0, x1, z0, z1);

    // Clear block nibble in AABB, queue cells
    int lx0 = x0 - ox_; if (lx0 < 0) lx0 = 0;
    int lx1 = x1 - ox_; if (lx1 > sizeX_) lx1 = sizeX_;
    int ly0 = y0 - oy_; if (ly0 < 0) ly0 = 0;
    int ly1 = y1 - oy_; if (ly1 > sizeY_) ly1 = sizeY_;
    int lz0 = z0 - oz_; if (lz0 < 0) lz0 = 0;
    int lz1 = z1 - oz_; if (lz1 > sizeZ_) lz1 = sizeZ_;

    for (int z = lz0; z < lz1; ++z)
        for (int y = ly0; y < ly1; ++y)
            for (int x = lx0; x < lx1; ++x) {
                int idx = x + sizeX_ * (y + sizeY_ * z);
                int sky = LightPack::Sky(data_[(size_t)idx]);
                data_[(size_t)idx] = LightPack::Make(sky, 0);
            }

    // SetAddLights for each region column touched
    int rx0 = lx0 >> 4, rx1 = (lx1 + 15) >> 4;
    int rz0 = lz0 >> 4, rz1 = (lz1 + 15) >> 4;
    for (int rz = rz0; rz < rz1; ++rz)
        for (int rx = rx0; rx < rx1; ++rx)
            SetAddLights(grid, rx, rz);

    AddLights(grid, 1 << 20);
}

// ---------------------------------------------------------------------------
// GetLight / GetFaceLight / color
// ---------------------------------------------------------------------------
uint16_t LightMap::GetLight(int x, int y, int z) const {
    int i = Index(x, y, z);
    if (i < 0) {
        if (y >= oy_ + sizeY_) return LightPack::Make(15, 0);
        return LightPack::Make(0, 0);
    }
    return data_[(size_t)i];
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
    uint16_t v = GetLight(nx, ny, nz);
    return (uint16_t)((v & ~LightPack::MODE_MASK) | mode);
}

void LightMap::SetBrightness(float b) {
    if (b < 0.f) b = 0.f;
    if (b > 1.f) b = 1.f;
    brightness_ = b;
}

void LightMap::UpdateLightMap() {
    if (brightnessApplied_ >= 0.f) {
        float d = brightness_ - brightnessApplied_;
        if (d < 0.f) d = -d;
        if (d < 0.004f) return;
    }
    brightnessApplied_ = brightness_;
    const float kMul1 = 0.53f, kAdd1 = 0.47f;
    const float kMul2 = 0.35f, kAdd2 = 0.65f;
    const float kF3 = 1.5f;
    const float kScale = 239.f, kBias = 16.f, kClamp = 255.f;
    float b = brightness_;
    float termR = b * kMul1 + kAdd1;
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
            colorTable_[sky * 16 + block] = 0xFF000000u | (R << 16) | (G << 8) | B;
        }
    }
    tableDirty_ = true;
}

uint32_t LightMap::GetLightColor(uint16_t packed) const {
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
