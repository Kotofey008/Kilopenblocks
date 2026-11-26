#include "Lighting.h"
// Light storage (AddLight @ 0x95b90 / AddLightUpdate @ 0x94b60):
//   per-cell value: low nibble sky + high nibble block; hierarchy |0x8000 dirty
// Mode bits applied in GetLight @ 0x94480 by binary face (0=XP..5=ZN):
//   0→0, 1→0x800, 2→0x1000, 3→0x1800, 4→0x2000, 5→0x2800
// Mesh ourFace 0=+X..5=-Z maps via GetFaceLight to those modes.
//
// Sky: UpdateSunRays column top-down + L atten. Block: BFS with L + state 0x2000/0x3c0
// (AddLightUpdate packing). Local path expands AABB by 15 (max nibble travel).

#include "TreeBuilder.h"
#include <algorithm>
#include <cstring>
#include <queue>
#include <vector>

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

// L at mat[+0x1a] is stored as uint8_t; signed values -1/-2 encode as 0xFF/0xFE.
// Always interpret via int8_t. Light channels are 0..15 with saturating arithmetic
// (binary nibbles & 0x0f / & 0xf0 — no wrap on underflow).
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
    // L<0 (leaves/water): attenuate sky, do not fully occlude
    if (SignedL(m) < 0) return false;
    return true;
}

// Emission only for L in 1..15 (torch 14, lamp 15, …). L<=0 never emits.
static int EmissionLevel(const GridData& grid, unsigned char t) {
    if (t == 0) return 0;
    const Material* m = grid.GetMaterial((int)t);
    if (!m) return 0;
    int L = SignedL(m);
    if (L < 1) return 0;
    return LightPack::Clamp15(L);
}

// How much to subtract from sky when the ray enters this cell (L=-1 → 1, L=-2 → 2).
static int SkyAtten(const GridData& grid, unsigned char t, int cidx) {
    if (t == 0) {
        if (cidx >= 0 && cidx < (int)grid.cellStates.size() &&
            (grid.cellStates[cidx] & 0x3fc0) != 0)
            return 2; // water/ice L=-2
        return 0;
    }
    const Material* m = grid.GetMaterial((int)t);
    if (!m) return 0;
    int L = SignedL(m);
    if (L >= 0) return 0;
    return -L; // 1 or 2
}

// Apply signed L to a 0..15 sky sample: saturate, never wrap.
//   level=0, L=-2 → 0;  level=2, L=-2 → 0;  level=3, L=-2 → 1
static int ApplySkyL(int skyLevel, int signedL) {
    if (signedL >= 0) return LightPack::Clamp15(skyLevel);
    return LightPack::Clamp15(skyLevel + signedL);
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
                if (IsOpaque(grid, t)) {
                    sky = 0;
                } else {
                    // Saturating add of signed L into sky (L=-2 at 0 stays 0, not 14/15).
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
                int block = LightPack::Clamp15(LightPack::Block(data_[idx]));
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
                if (IsOpaque(grid, t)) {
                    sky = 0;
                } else {
                    // Saturating add of signed L into sky (L=-2 at 0 stays 0, not 14/15).
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
                int block = LightPack::Clamp15(LightPack::Block(data_[idx]));
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

// Apply AddLightUpdate state/L packing to sky+block nibbles already decremented.
// Binary after neighbor-max and -1: material L, then state 0x2000 / 0x3c0.
static uint16_t ApplyLAndState(GridData& grid, int cidx, int sky, int block) {
    sky = LightPack::Clamp15(sky);
    block = LightPack::Clamp15(block);
    unsigned char t = (cidx >= 0 && cidx < (int)grid.cellTypes.size())
                          ? grid.cellTypes[cidx] : 0;
    unsigned short st = (cidx >= 0 && cidx < (int)grid.cellStates.size())
                            ? grid.cellStates[cidx] : 0;

    int sL = 0;
    if (t != 0) {
        const Material* mat = grid.GetMaterial((int)t);
        sL = SignedL(mat);
    }
    if (sL < 0) {
        int al = -sL;
        sky = (sky > al) ? (sky - al) : 0;
        block = (block > al) ? (block - al) : 0;
    } else if (sL > 0) {
        if (block < sL) block = sL;
    }

    // state & 0x2000 → water fluid atten (bit12 → 1 else 2)
    // state & 0x3c0 (and not 0x2000) → pack 0xff (lava)
    if ((st & GridData::STATE_BIT_2000) != 0) {
        int atten = ((st & 0x1000) != 0) ? 1 : 2;
        int outSky = (sky > atten) ? (sky - atten) : 0;
        int outBlk = (block > atten) ? (block - atten) : 0;
        return LightPack::Make(outSky, outBlk);
    }
    if ((st & GridData::STATE_BIT_3C0) != 0) {
        return LightPack::Make(15, 15);
    }
    return LightPack::Make(sky, block);
}

// Block-channel BFS inside [lx0,lx1)×[ly0,ly1)×[lz0,lz1) lightmap-local bounds.
// Preserves sky from UpdateSunRays*; applies L + water state at each write.
static void PropagateBlockRegion(LightMap& self, GridData& grid,
                                 int lx0, int lx1, int ly0, int ly1, int lz0, int lz1,
                                 int sizeX, int sizeY, int sizeZ,
                                 int ox, int oy, int oz,
                                 std::vector<uint16_t>& data) {
    // Clear block nibble only inside region (keep sky from sun rays).
    for (int z = lz0; z < lz1; ++z)
        for (int y = ly0; y < ly1; ++y)
            for (int x = lx0; x < lx1; ++x) {
                int idx = x + sizeX * (y + sizeY * z);
                int sky = LightPack::Sky(data[(size_t)idx]);
                data[(size_t)idx] = LightPack::Make(sky, 0);
            }

    std::queue<int> q;
    auto inBounds = [&](int x, int y, int z) {
        return x >= lx0 && x < lx1 && y >= ly0 && y < ly1 && z >= lz0 && z < lz1;
    };

    // Seed emitters whose cells fall in region
    for (int z = lz0; z < lz1; ++z) {
        for (int y = ly0; y < ly1; ++y) {
            for (int x = lx0; x < lx1; ++x) {
                int wx = x + ox, wy = y + oy, wz = z + oz;
                int cidx = grid.CoordToIndex(wx, wy, wz);
                if (cidx < 0) continue;
                unsigned char t = grid.cellTypes[cidx];
                if (t == 0) continue;
                int emit = EmissionLevel(grid, t);
                if (emit <= 0) continue;
                int idx = x + sizeX * (y + sizeY * z);
                int sky = LightPack::Sky(data[(size_t)idx]);
                // Apply L/state packing for the emitter cell
                data[(size_t)idx] = ApplyLAndState(grid, cidx, sky, emit);
                q.push(idx);
            }
        }
    }
    if (q.empty()) return;

    const int dx[6] = {1, -1, 0, 0, 0, 0};
    const int dy[6] = {0, 0, 1, -1, 0, 0};
    const int dz[6] = {0, 0, 0, 0, 1, -1};

    while (!q.empty()) {
        int idx = q.front();
        q.pop();
        int level = LightPack::Block(data[(size_t)idx]);
        if (level <= 1) continue;
        int lx = idx % sizeX;
        int ly = (idx / sizeX) % sizeY;
        int lz = idx / (sizeX * sizeY);
        for (int d = 0; d < 6; ++d) {
            int nx = lx + dx[d], ny = ly + dy[d], nz = lz + dz[d];
            if (!inBounds(nx, ny, nz)) continue;
            int nidx = nx + sizeX * (ny + sizeY * nz);
            int wx = nx + ox, wy = ny + oy, wz = nz + oz;
            int cidx = grid.CoordToIndex(wx, wy, wz);
            unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : (unsigned char)0;
            if (IsOpaque(grid, t)) continue;

            int newLevel = LightPack::Clamp15(level - 1);
            int sky = LightPack::Sky(data[(size_t)nidx]);
            uint16_t packed = ApplyLAndState(grid, cidx, sky, newLevel);
            int finalBlk = LightPack::Block(packed);
            int cur = LightPack::Block(data[(size_t)nidx]);
            if (finalBlk > cur) {
                data[(size_t)nidx] = packed;
                q.push(nidx);
            }
        }
    }
    (void)self;
}

void LightMap::PropagateBlockLights(GridData& grid) {
    // Full-world block BFS after sky columns are filled by UpdateSunRays.
    if (data_.empty() || sizeX_ <= 0 || sizeY_ <= 0 || sizeZ_ <= 0) return;
    PropagateBlockRegion(*this, grid, 0, sizeX_, 0, sizeY_, 0, sizeZ_,
                         sizeX_, sizeY_, sizeZ_, ox_, oy_, oz_, data_);
}

void LightMap::PropagateBlockLightsLocal(GridData& grid, int minX, int maxX, int minY, int maxY,
                                         int minZ, int maxZ) {
    // Expand by 15 (max nibble travel) — binary light cannot jump farther.
    if (data_.empty() || sizeX_ <= 0) return;
    const int margin = 15;
    int lx0 = (minX - margin) - ox_; if (lx0 < 0) lx0 = 0;
    int lx1 = (maxX + margin) - ox_; if (lx1 > sizeX_) lx1 = sizeX_;
    int ly0 = (minY - margin) - oy_; if (ly0 < 0) ly0 = 0;
    int ly1 = (maxY + margin) - oy_; if (ly1 > sizeY_) ly1 = sizeY_;
    int lz0 = (minZ - margin) - oz_; if (lz0 < 0) lz0 = 0;
    int lz1 = (maxZ + margin) - oz_; if (lz1 > sizeZ_) lz1 = sizeZ_;
    if (lx0 >= lx1 || ly0 >= ly1 || lz0 >= lz1) return;

    // Refresh sky columns in the XZ span (full Y — binary column is full height).
    UpdateSunRaysLocal(grid, lx0 + ox_, lx1 + ox_, lz0 + oz_, lz1 + oz_);
    PropagateBlockRegion(*this, grid, lx0, lx1, ly0, ly1, lz0, lz1,
                         sizeX_, sizeY_, sizeZ_, ox_, oy_, oz_, data_);
}

uint16_t LightMap::GetLight(int x, int y, int z) const {
    int i = Index(x, y, z);
    if (i < 0) {
        // Open sky only above lightmap; below/side → no sky (caves at edges).
        if (y >= oy_ + sizeY_) return LightPack::Make(15, 0);
        return LightPack::Make(0, 0);
    }
    return data_[i];
}

uint16_t LightMap::GetFaceLight(int x, int y, int z, int face) const {
    // Mesh ourFace: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
    // Binary GetLight face: 0=XP 1=YP 2=ZP 3=XN 4=YN 5=ZN
    // our→bin modes: +X→0, -X→0x1800, +Y→0x800, -Y→0x2000, +Z→0x1000, -Z→0x2800
    // Sample the cell outside the face (same as binary neighbor select).
    int nx = x, ny = y, nz = z;
    uint16_t mode = LightPack::MODE_DEFAULT;
    switch (face) {
        case 0: nx++; mode = LightPack::MODE_DEFAULT; break; // XP
        case 1: nx--; mode = LightPack::MODE_1800;    break; // XN
        case 2: ny++; mode = LightPack::MODE_0800;    break; // YP
        case 3: ny--; mode = LightPack::MODE_2000;    break; // YN
        case 4: nz++; mode = LightPack::MODE_1000;    break; // ZP
        case 5: nz--; mode = LightPack::MODE_2800;    break; // ZN
        default: break;
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
    // Epsilon: continuous time-of-day must not rebuild LUT + remesh every frame.
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
