#include "Lighting.h"
// Light storage (AddLight @ 0x95b90 / AddLightUpdate @ 0x94b60):
//   per-cell value: low nibble sky + high nibble block; hierarchy |0x8000 dirty
// Mode bits applied in GetLight @ 0x94480 by binary face (0=XP..5=ZN):
//   0→0, 1→0x800, 2→0x1000, 3→0x1800, 4→0x2000, 5→0x2800
// Mesh ourFace 0=+X..5=-Z maps via GetFaceLight to those modes.
//
// P1: PropagateBlockLights follows AddLightUpdate (0x94b60) for BOTH channels:
//   neighbor max sky/block → decrement → material L (+0x1a) → state 0x2000/0x3c0.

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

// ---------------------------------------------------------------------------
// AddLightUpdate @ 0x94b60 — single-cell recompute from current lightmap neighbours.
// Returns packed sky|block (low/high nibble). Matches ghidra C + disasm.
// ---------------------------------------------------------------------------
static uint16_t ComputeAddLightUpdate(const LightMap& lm, GridData& grid,
                                      int wx, int wy, int wz, int cidx) {
    const int sizeX = grid.sizeX;
    const int sizeY = grid.sizeY;
    const int sizeZ = grid.sizeZ;
    if (cidx < 0 || cidx >= (int)grid.cellTypes.size())
        return 0;

    unsigned char t = grid.cellTypes[cidx];
    unsigned short st = (cidx < (int)grid.cellStates.size()) ? grid.cellStates[cidx] : 0;

    // --- Opaque solid with FTL==0: keep existing light (binary early-out) ---
    if (t != 0) {
        const Material* mat = grid.GetMaterial((int)t);
        if (mat && mat->flowPerm(0) == 0) {
            // Binary: if (+0x1b)==0 goto keep current and return
            return lm.GetLight(wx, wy, wz) & 0x00ff; // sky|block only
        }
    }

    // Gather max sky (low nibble) and max block (high nibble, units 0x10) from 6 neighbours.
    // Binary only accepts a neighbour if the shared face allows flow.
    int sky = 0;   // 0..15
    int block = 0; // 0..15 (we store nibble; binary uses 0x10 steps internally)

    auto sample = [&](int nx, int ny, int nz, GridData::FlowType outFlow, GridData::FlowType inFlow) {
        int ni = grid.CoordToIndex(nx, ny, nz);
        if (ni < 0) return;
        // Outgoing flow from this cell toward neighbour
        if (t != 0 && grid.IsFlowAllowed(cidx, outFlow) == 0) return;
        // Incoming flow from neighbour into this cell
        unsigned char nt = grid.cellTypes[ni];
        if (nt != 0) {
            if (grid.IsFlowAllowed(ni, inFlow) == 0) return;
        }
        uint16_t lv = lm.GetLight(nx, ny, nz);
        int ns = LightPack::Sky(lv);
        int nb = LightPack::Block(lv);
        if (ns > sky) sky = ns;
        if (nb > block) block = nb;
    };

    // +X (neighbour x+1): out XP, in XN
    if (wx + 1 < grid.originX + sizeX)
        sample(wx + 1, wy, wz, GridData::FlowXP, GridData::FlowXN);
    // -X
    if (wx - 1 >= grid.originX)
        sample(wx - 1, wy, wz, GridData::FlowXN, GridData::FlowXP);
    // +Y
    if (wy + 1 < grid.originY + sizeY)
        sample(wx, wy + 1, wz, GridData::FlowYP, GridData::FlowYN);
    else {
        // Open sky above world — binary sets local_8c = 0xf when y+1 >= sizeY
        if (sky < 15) sky = 15;
    }
    // -Y
    if (wy - 1 >= grid.originY)
        sample(wx, wy - 1, wz, GridData::FlowYN, GridData::FlowYP);
    // +Z
    if (wz + 1 < grid.originZ + sizeZ)
        sample(wx, wy, wz + 1, GridData::FlowZP, GridData::FlowZN);
    // -Z
    if (wz - 1 >= grid.originZ)
        sample(wx, wy, wz - 1, GridData::FlowZN, GridData::FlowZP);

    // Decrement one step (binary: sky = sky==0 ? 0 : sky-1; block -= 1 nibble)
    if (sky > 0) sky -= 1;
    if (block > 0) block -= 1;

    // Material L at +0x1a (signed). For type==0 binary still reads materials[0].L (=0).
    int sL = 0;
    if (t != 0) {
        const Material* mat = grid.GetMaterial((int)t);
        sL = SignedL(mat);
    } else {
        // Air slot L is 0; water/lava handled via state below, not material L.
        sL = 0;
    }
    if (sL != 0) {
        if (sL < 0) {
            int al = -sL;
            sky = (sky > al) ? (sky - al) : 0;
            block = (block > al) ? (block - al) : 0;
        } else {
            // L > 0: emit — block = max(block, L)
            if (block < sL) block = sL;
        }
    }

    // State branches (binary after L):
    //   !(state & 0x2000):
    //     (state & 0x3c0)==0 → pack = (block<<4)|sky
    //     else               → pack = 0xff  (lava / logged liquid)
    //   (state & 0x2000):  water fluid — attenuate by 1 or 2 from bit12 of state
    uint16_t packed;
    if ((st & GridData::STATE_BIT_2000) == 0) {
        if ((st & GridData::STATE_BIT_3C0) == 0) {
            packed = (uint16_t)((sky & 0xf) | ((block & 0xf) << 4));
        } else {
            packed = 0x00ff; // full sky+block
        }
    } else {
        // atten = (state bit12 ? 1 : 2);  bit12 = (state & 0x1000) != 0
        // from: (int)(state << 19) >> 31  → -1 if bit12 else 0; then +2
        int atten = ((st & 0x1000) != 0) ? 1 : 2;
        int outSky = 0, outBlk = 0;
        if (sky > atten) outSky = sky - atten;
        if (block > atten) outBlk = block - atten;
        packed = (uint16_t)((outSky & 0xf) | ((outBlk & 0xf) << 4));
    }
    return packed;
}

void LightMap::PropagateBlockLights(GridData& grid) {
    // Full dual-channel rebuild via AddLightUpdate semantics (no hierarchy queue).
    // 1) clear  2) seed open-sky top + L>0 emitters  3) BFS relax until stable
    if (data_.empty() || sizeX_ <= 0 || sizeY_ <= 0 || sizeZ_ <= 0) return;

    data_.assign(data_.size(), 0);
    std::queue<int> q;
    std::vector<uint8_t> inq(data_.size(), 0);

    auto enqueue = [&](int idx) {
        if (idx < 0 || idx >= (int)data_.size()) return;
        if (inq[(size_t)idx]) return;
        inq[(size_t)idx] = 1;
        q.push(idx);
    };

    // Seed: top layer open to sky (binary y+1 >= sizeY → sky 0xf before decrement)
    for (int z = 0; z < sizeZ_; ++z) {
        for (int x = 0; x < sizeX_; ++x) {
            int wy = oy_ + sizeY_ - 1;
            int wx = ox_ + x, wz = oz_ + z;
            int cidx = grid.CoordToIndex(wx, wy, wz);
            if (cidx < 0) continue;
            unsigned char t = grid.cellTypes[cidx];
            // Only seed through non-FTL-blocked cells
            if (t != 0) {
                const Material* m = grid.GetMaterial((int)t);
                if (m && m->flowPerm(0) == 0) continue;
            }
            // After Compute: sky from above-world 15 → decrement → 14, unless L/state
            data_[(size_t)(x + sizeX_ * ((sizeY_ - 1) + sizeY_ * z))] =
                ComputeAddLightUpdate(*this, grid, wx, wy, wz, cidx);
            enqueue(x + sizeX_ * ((sizeY_ - 1) + sizeY_ * z));
        }
    }

    // Seed: positive-L emitters
    const size_t n = grid.cellTypes.size();
    for (size_t ci = 0; ci < n; ++ci) {
        unsigned char t = grid.cellTypes[ci];
        if (t == 0) continue;
        int emit = EmissionLevel(grid, t);
        if (emit <= 0) continue;
        int lx = (int)(ci % (size_t)grid.sizeX);
        int t1 = (int)(ci / (size_t)grid.sizeX);
        int ly = t1 % grid.sizeY;
        int lz = t1 / grid.sizeY;
        int wx = lx + grid.originX, wy = ly + grid.originY, wz = lz + grid.originZ;
        int idx = Index(wx, wy, wz);
        if (idx < 0) continue;
        // Apply full formula (will max block with L)
        uint16_t packed = ComputeAddLightUpdate(*this, grid, wx, wy, wz, (int)ci);
        // Ensure emit present even if neighbours empty
        int sky = LightPack::Sky(packed);
        int blk = LightPack::Block(packed);
        if (blk < emit) blk = emit;
        data_[(size_t)idx] = LightPack::Make(sky, blk);
        enqueue(idx);
    }

    const int dx[6] = {1, -1, 0, 0, 0, 0};
    const int dy[6] = {0, 0, 1, -1, 0, 0};
    const int dz[6] = {0, 0, 0, 0, 1, -1};

    // BFS: when a cell changes, recompute its 6 neighbours
    while (!q.empty()) {
        int idx = q.front();
        q.pop();
        inq[(size_t)idx] = 0;

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
            if (cidx < 0) continue;

            uint16_t neu = ComputeAddLightUpdate(*this, grid, wx, wy, wz, cidx);
            uint16_t old = data_[(size_t)nidx] & 0x00ff;
            if (neu != old) {
                data_[(size_t)nidx] = neu;
                enqueue(nidx);
            }
        }
    }
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
