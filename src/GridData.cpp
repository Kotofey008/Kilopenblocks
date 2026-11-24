#include <cstdio>
#include <cstdint>
#include <vector>
#include "GridData.h"
#include <sstream>
#include "Assets.h"
#include <cmath>
#include <cstring>
#include <algorithm>

GridData::GridData() {
    // Matching observed ctor defaults: LeavesQuality = 2, various zero-inits
    // Full field init (MeshBuffer, DDSFile, etc.) still being mapped.
}

GridData::~GridData() {}

void GridData::InitDimensionsFromWorldBounds() {
    // Inclusive integer cells covering the float bounds
    originX = (int)worldMin.x;
    originY = (int)worldMin.y;
    originZ = (int)worldMin.z;
    sizeX = (int)worldMax.x - originX;
    sizeY = (int)worldMax.y - originY;
    sizeZ = (int)worldMax.z - originZ;
    if (sizeX < 1) sizeX = 1;
    if (sizeY < 1) sizeY = 1;
    if (sizeZ < 1) sizeZ = 1;
    size_t n = (size_t)sizeX * sizeY * sizeZ;
    cellTypes.assign(n, 0);   // Air
    cellStates.assign(n, 0);
    // Hierarchical 0x4000 maps (sizes cover worst-case shift of linear index)
    aux4000L0_.assign((n >> 4) + 1, 0);
    aux4000L1_.assign((n >> 8) + 1, 0);
    aux4000L2_.assign((n >> 12) + 1, 0);
    aux4000L3_.assign((n >> 16) + 1, 0);
    InitRegions();
}

int GridData::CoordToIndex(int x, int y, int z) const {
    int lx = x - originX;
    int ly = y - originY;
    int lz = z - originZ;
    if (lx < 0 || ly < 0 || lz < 0 || lx >= sizeX || ly >= sizeY || lz >= sizeZ)
        return -1;
    return lx + sizeX * (ly + sizeY * lz);
}

// ---------------------------------------------------------------------------
// SetState (0x77380, 617 B) — recovered algorithm
//
// Verified control flow from pdf:
//   combined = (cellStates[idx] & 0x4000) | (newState & 0xBFFF)
//   Write combined into aux map at +0x230 (word per cell)
//   If bit 0x4000 of newState differs from aux at +0x258:
//     hierarchical byte-count maps updated at shifts >>4, >>8, >>12, >>16
//     maps at +0x278, +0x270, +0x268, +0x260 (and symmetric path at +0x250..)
//     each level: count += (new?1:0) - (old?1:0); propagate if 0↔nonzero
//   Write newState into aux at +0x258
//
// Primary cellStates@+0x78 is also written by SetCell; SetState focuses on
// the parallel aux structures and hierarchical occupancy of bit 0x4000.
// ---------------------------------------------------------------------------
void GridData::UpdateHierarchy4000(int cellIndex, bool bitOn) {
    // Hierarchical occupancy for STATE_BIT_4000.
    // Original uses separate byte arrays per shift level; we keep parallel
    // count arrays sized to the grid (simplified layout, same semantics).
    if (aux4000L0_.empty()) return;
    auto bump = [](std::vector<uint8_t>& arr, int i, int delta) -> bool {
        if (i < 0 || (size_t)i >= arr.size()) return false;
        int v = (int)arr[i] + delta;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        bool was = arr[i] != 0;
        arr[i] = (uint8_t)v;
        bool now = arr[i] != 0;
        return was != now; // true if occupancy toggled → propagate
    };
    int i4  = cellIndex >> 4;
    int i8  = cellIndex >> 8;
    int i12 = cellIndex >> 12;
    int i16 = cellIndex >> 16;
    int d = bitOn ? 1 : -1;
    if (!bump(aux4000L0_, i4, d)) return;
    if (!bump(aux4000L1_, i8, d)) return;
    if (!bump(aux4000L2_, i12, d)) return;
    bump(aux4000L3_, i16, d);
}

void GridData::SetState(int cellIndex, unsigned short newState) {
    if (cellIndex < 0 || (size_t)cellIndex >= cellStates.size()) return;

    unsigned short oldStored = cellStates[cellIndex];
    // Binary: aux230 gets (old & 0x4000) | (new & 0xBFFF)
    //         aux258 gets full newState
    //         hierarchical counts track newState's 0x4000 bit
    // Primary cellStates: store full newState so callers can set/clear 0x4000.
    // (Binary dual-writes; single-array approximation prefers caller-visible value.)
    bool oldBit = (oldStored & STATE_BIT_4000) != 0;
    bool newBit = (newState & STATE_BIT_4000) != 0;
    if (oldBit != newBit) {
        UpdateHierarchy4000(cellIndex, newBit);
    }

    cellStates[cellIndex] = newState;
    // SetState never triggers mesh rebuild. Geometry dirty only via SetCell
    // (which MarkRegionDirty). Raising meshesDirty_ here without region keys
    // caused EnsureRegionMeshes full-world rebuild every frame.
}

// ---------------------------------------------------------------------------
// SetCell (0x77d80, 5910 B / 122 BBs) — see
//   pseudocode/recovered/0x77d80_SetCell_COMPLETE.md
//
// Side-effect order (CONFIRMED §5):
//   type write → dirty maps → region+0x1f → aux/hierarchy → neighbour region dirty
//   → neighbour SetState |0xC000 gated by this+0xf0 + bounds
// Region array is XZ-only (stride 0xa4); Y is inside the column, not a key.
// ---------------------------------------------------------------------------
void GridData::SetCell(int x, int y, int z, unsigned char type, unsigned short state) {
    int idx = CoordToIndex(x, y, z);
    if (idx < 0) return;

    unsigned char oldType = cellTypes[idx];
    unsigned short oldState = cellStates[idx];
    bool typeChanged = (oldType != type);

    cellTypes[idx] = type;
    SetState(idx, state);
    unsigned short newState = cellStates[idx];

    bool bit2000Changed = ((oldState ^ newState) & STATE_BIT_2000) != 0;
    bool bit3c0Changed  = ((oldState ^ newState) & STATE_BIT_3C0) != 0;
    bool levelChanged   = ((oldState ^ newState) & STATE_LEVEL_MASK) != 0;
    // 0x4000 is hierarchy scheduling only — geometry uses type / fluid bits / level.
    bool meshRelevant = typeChanged || bit2000Changed || bit3c0Changed || levelChanged;

    if (meshRelevant) {
        // region[rx,rz] + 0x1f = 1  (XZ column)
        MarkRegionDirty(x, y, z);
        meshesDirty_ = true;

        // Neighbour region dirty only on X/Z faces of the 16-column (binary XZ regions).
        // Y is vertical inside the same column — no separate region key.
        if (typeChanged || bit2000Changed || bit3c0Changed) {
            int lx = x & 15, lz = z & 15;
            if (lx == 0)  MarkRegionDirty(x - 1, y, z);
            if (lx == 15) MarkRegionDirty(x + 1, y, z);
            if (lz == 0)  MarkRegionDirty(x, y, z - 1);
            if (lz == 15) MarkRegionDirty(x, y, z + 1);
        }
    }

    // Neighbour SetState | 0xC000 — gated by this+0xf0 (batchFlag_).
    // Binary full-path / state-only reentry when (old^new)&0x3fff != 0 (not 0x4000 alone).
    // Material predicates PARTIAL — bounds only (Y- requires y>originY).
    // SetState does NOT mark region dirty — mesh dirty is region+0x1f only.
    bool lowBitsChanged = ((oldState ^ newState) & 0x3fff) != 0;
    if (batchFlag_ == 0 && (typeChanged || lowBitsChanged)) {
        auto orNeighbour = [&](int nx, int ny, int nz) {
            int nidx = CoordToIndex(nx, ny, nz);
            if (nidx < 0) return;
            unsigned short ns = cellStates[nidx];
            unsigned short merged = (unsigned short)(ns | 0xC000);
            if (merged != ns)
                SetState(nidx, merged);
        };
        orNeighbour(x - 1, y, z);
        orNeighbour(x + 1, y, z);
        if (y > originY)
            orNeighbour(x, y - 1, z);
        orNeighbour(x, y + 1, z);
        orNeighbour(x, y, z - 1);
        orNeighbour(x, y, z + 1);
    }
}

// ---------------------------------------------------------------------------
// Region array (binary +0x80, stride 0xa4) — XZ index only
// ---------------------------------------------------------------------------
void GridData::InitRegions() {
    // Cover world XZ with 16-aligned region cells.
    int minX = originX & ~15;
    int minZ = originZ & ~15;
    int maxX = (originX + sizeX + 15) & ~15;
    int maxZ = (originZ + sizeZ + 15) & ~15;
    regionOriginRx_ = minX >> 4;
    regionOriginRz_ = minZ >> 4;
    regionCountX_ = (maxX - minX) >> 4;
    regionCountZ_ = (maxZ - minZ) >> 4;
    if (regionCountX_ < 1) regionCountX_ = 1;
    if (regionCountZ_ < 1) regionCountZ_ = 1;
    regions_.assign((size_t)regionCountX_ * (size_t)regionCountZ_, Region{});
    for (int rz = 0; rz < regionCountZ_; ++rz) {
        for (int rx = 0; rx < regionCountX_; ++rx) {
            Region& r = regions_[(size_t)rz * (size_t)regionCountX_ + (size_t)rx];
            int wx = (regionOriginRx_ + rx) * 16;
            int wz = (regionOriginRz_ + rz) * 16;
            r.Init(wx, 0, wz);
            r.active() = 1;
            r.dirty() = 0;
        }
    }
    dirtyRegionIndices_.clear();
    dirtyRegions_.clear();
    waterDeadline_ = 0;
    waterLongDeadline_ = 0;
    waterClockMs_ = 0;
}

GridData::Region* GridData::GetRegionByIndex(int rx, int rz) {
    int ix = rx - regionOriginRx_;
    int iz = rz - regionOriginRz_;
    if (ix < 0 || iz < 0 || ix >= regionCountX_ || iz >= regionCountZ_)
        return nullptr;
    return &regions_[(size_t)iz * (size_t)regionCountX_ + (size_t)ix];
}

GridData::Region* GridData::GetRegionByWorld(int worldX, int worldZ) {
    int ox = worldX & ~15;
    int oz = worldZ & ~15;
    return GetRegionByIndex(ox >> 4, oz >> 4);
}

const GridData::Region* GridData::GetRegionByWorld(int worldX, int worldZ) const {
    int ox = worldX & ~15;
    int oz = worldZ & ~15;
    int ix = (ox >> 4) - regionOriginRx_;
    int iz = (oz >> 4) - regionOriginRz_;
    if (ix < 0 || iz < 0 || ix >= regionCountX_ || iz >= regionCountZ_)
        return nullptr;
    return &regions_[(size_t)iz * (size_t)regionCountX_ + (size_t)ix];
}

void GridData::MarkRegionDirty(int x, int /*y*/, int z) {
    // Binary: region[rx,rz] + 0x1f = 1. Origin = coord & ~15 (two's-complement).
    int ox = x & ~15;
    int oz = z & ~15;
    int ix = (ox >> 4) - regionOriginRx_;
    int iz = (oz >> 4) - regionOriginRz_;
    if (ix < 0 || iz < 0 || ix >= regionCountX_ || iz >= regionCountZ_)
        return;
    int flat = iz * regionCountX_ + ix;
    Region& r = regions_[(size_t)flat];
    if (r.dirty() != 0) return; // already on sparse list
    r.dirty() = 1;
    r.active() = 1;
    dirtyRegionIndices_.push_back(flat);
}

bool GridData::IsRegionDirtyWorld(int worldX, int worldZ) const {
    const Region* r = GetRegionByWorld(worldX, worldZ);
    return r && r->dirty() != 0;
}

int GridData::CollectDirtyRegions(int* outOx, int* outOz, int maxOut) const {
    if (!outOx || !outOz || maxOut <= 0) return 0;
    int n = 0;
    for (size_t i = 0; i < dirtyRegionIndices_.size() && n < maxOut; ++i) {
        int flat = dirtyRegionIndices_[i];
        if (flat < 0 || (size_t)flat >= regions_.size()) continue;
        const Region& r = regions_[(size_t)flat];
        if (r.dirty() == 0) continue;
        outOx[n] = r.x & ~15;
        outOz[n] = r.z & ~15;
        ++n;
    }
    return n;
}

void GridData::ClearRegionDirtyAt(int worldOx, int worldOz) {
    int ox = worldOx & ~15;
    int oz = worldOz & ~15;
    int ix = (ox >> 4) - regionOriginRx_;
    int iz = (oz >> 4) - regionOriginRz_;
    if (ix < 0 || iz < 0 || ix >= regionCountX_ || iz >= regionCountZ_)
        return;
    int flat = iz * regionCountX_ + ix;
    regions_[(size_t)flat].dirty() = 0;
    // Compact sparse list (swap-erase matching flats)
    for (size_t i = 0; i < dirtyRegionIndices_.size(); ) {
        if (dirtyRegionIndices_[i] == flat) {
            dirtyRegionIndices_[i] = dirtyRegionIndices_.back();
            dirtyRegionIndices_.pop_back();
        } else {
            ++i;
        }
    }
}

void GridData::ClearAllRegionDirty() {
    for (int flat : dirtyRegionIndices_) {
        if (flat >= 0 && (size_t)flat < regions_.size())
            regions_[(size_t)flat].dirty() = 0;
    }
    dirtyRegionIndices_.clear();
    dirtyRegions_.clear();
}

void GridData::SyncHierarchyFromStates() {
    // After bulk PutCell / FillCells the primary states may carry 0x4000 while
    // hierarchy maps are zero. Rebuild counts from cellStates so UpdateWater
    // sees gen-time liquid sources (0xE000 / 0xC3C0).
    if (cellStates.empty()) return;
    std::fill(aux4000L0_.begin(), aux4000L0_.end(), 0);
    std::fill(aux4000L1_.begin(), aux4000L1_.end(), 0);
    std::fill(aux4000L2_.begin(), aux4000L2_.end(), 0);
    std::fill(aux4000L3_.begin(), aux4000L3_.end(), 0);
    const size_t n = cellStates.size();
    for (size_t i = 0; i < n; ++i) {
        if (cellStates[i] & STATE_BIT_4000)
            UpdateHierarchy4000((int)i, true);
    }
}

// ---------------------------------------------------------------------------
// FillCells (0x79ac0, 339 B) — CONFIRMED pure bulk fill
//
// Signature: FillCells(x0,y0,z0, x1,y1,z1, type, state)
// Bounds are exclusive: dx=x1-x0, dy=y1-y0, dz=z1-z0; zero-trip if <=0.
// Triple nested loop (Z outer, then Y, then X) writes:
//   types[index]  = type   (uint8)
//   states[index] = state  (uint16)
// Strides: sizeX@+0x44, sizeXZ@+0x50. No SetCell, no dirty, no hierarchy,
// no light/water side effects. is-pure: true.
// ---------------------------------------------------------------------------
void GridData::FillCells(int x0, int y0, int z0, int x1, int y1, int z1, unsigned char type) {
    FillCells(x0, y0, z0, x1, y1, z1, type, static_cast<unsigned short>(0));
}

void GridData::FillCells(int x0, int y0, int z0, int x1, int y1, int z1,
                         unsigned char type, unsigned short state) {
    // Exclusive extents matching binary (sub + jle zero-trip).
    int dx = x1 - x0;
    int dy = y1 - y0;
    int dz = z1 - z0;
    if (dx <= 0 || dy <= 0 || dz <= 0)
        return;
    if (cellTypes.empty() || cellStates.empty())
        return;

    // Linear index base = x0 + sizeX*y0 + sizeXZ*z0
    // sizeX = sizeX_, sizeXZ = sizeX_*sizeY_ (mirrored from this+0x44 / +0x50)
    for (int iz = 0; iz < dz; ++iz) {
        int z = z0 + iz;
        for (int iy = 0; iy < dy; ++iy) {
            int y = y0 + iy;
            for (int ix = 0; ix < dx; ++ix) {
                int x = x0 + ix;
                int idx = CoordToIndex(x, y, z);
                if (idx < 0) continue;
                cellTypes[idx]  = type;
                cellStates[idx] = state;
            }
        }
    }
}

void GridData::FillState(int x0, int y0, int z0, int x1, int y1, int z1, unsigned short state) {
    // Pure state fill (no type change, no SetCell side effects).
    int dx = x1 - x0;
    int dy = y1 - y0;
    int dz = z1 - z0;
    if (dx <= 0 || dy <= 0 || dz <= 0) return;
    for (int iz = 0; iz < dz; ++iz) {
        int z = z0 + iz;
        for (int iy = 0; iy < dy; ++iy) {
            int y = y0 + iy;
            for (int ix = 0; ix < dx; ++ix) {
                int x = x0 + ix;
                int idx = CoordToIndex(x, y, z);
                if (idx >= 0) cellStates[idx] = state;
            }
        }
    }
}

bool GridData::IsUnderwater(const Vector& pos) const {
    // Binary IsUnderwater @ 0x7b350: fluid bits (state & 0x3fc0), then
    // GetWaterBlockFaceYP + triangle test — sample must be under the liquid
    // surface, not merely inside a water-typed cell. Shallow level-7 puddles
    // (height 1/16) therefore do not force Swim when the body sample is above
    // the free surface.
    int bx = (int)std::floor(pos.x);
    int by = (int)std::floor(pos.y);
    int bz = (int)std::floor(pos.z);
    int idx = CoordToIndex(bx, by, bz);
    if (idx < 0) return false;
    // Binary: fluid = (state & 0x3fc0) != 0. World liquid is type 0 + state;
    // material id 5 is not a world liquid carrier (blocks.txt S-Index hidden).
    bool fluid = (idx < (int)cellStates.size() && (cellStates[idx] & 0x3fc0) != 0);
    if (!fluid) return false;

    // Surface height matches GetWaterBlockHeight units (0..16): source 14/16,
    // level n → (15-2n)/16, water above → full cell.
    unsigned short st = (idx < (int)cellStates.size()) ? cellStates[idx] : 0;
    float hFrac = 1.f;
    int above = CoordToIndex(bx, by + 1, bz);
    bool waterAbove = above >= 0 && above < (int)cellStates.size() &&
                      (cellStates[above] & 0x3fc0) != 0;
    if (!waterAbove) {
        int level = (int)((st & 0x1c00) >> 10);
        if ((st & 0xE000) == 0xE000) level = 0;
        int units = (level == 0) ? 0xe : (0xf - 2 * level);
        if (units < 1) units = 1;
        if (units > 16) units = 16;
        hFrac = (float)units / 16.f;
    }
    float surfaceY = (float)by + hFrac;
    return pos.y < surfaceY;
}

bool GridData::IsLiquidCell(int x, int y, int z) const {
    int idx = CoordToIndex(x, y, z);
    if (idx < 0 || idx >= (int)cellStates.size()) return false;
    return HasLiquidState(cellStates[idx]);
}

bool GridData::IsWaterFluidCell(int x, int y, int z) const {
    int idx = CoordToIndex(x, y, z);
    if (idx < 0 || idx >= (int)cellStates.size()) return false;
    return IsWaterFluidState(cellStates[idx]);
}

bool GridData::IsLavaFluidCell(int x, int y, int z) const {
    int idx = CoordToIndex(x, y, z);
    if (idx < 0 || idx >= (int)cellStates.size()) return false;
    return IsLavaFluidState(cellStates[idx]);
}

// OnLadder @ 0x7b090 — class-3 ladder cells overlapping body AABB.
bool GridData::OnLadder(const Vector& feet, const Vector& bodySize, const Vector& /*offset*/) const {
    const float halfW = bodySize.x * 0.5f;
    const float halfD = bodySize.z * 0.5f;
    const float h = bodySize.y > 0.f ? bodySize.y : 1.5f;
    const int x0 = (int)std::floor(feet.x - halfW);
    const int x1 = (int)std::floor(feet.x + halfW - 1e-6f);
    const int y0 = (int)std::floor(feet.y);
    const int y1 = (int)std::floor(feet.y + h - 1e-6f);
    const int z0 = (int)std::floor(feet.z - halfD);
    const int z1 = (int)std::floor(feet.z + halfD - 1e-6f);
    for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x)
    for (int z = z0; z <= z1; ++z) {
        int idx = CoordToIndex(x, y, z);
        if (idx < 0) continue;
        const Material* m = GetMaterial((int)cellTypes[idx]);
        if (m && m->prop01() == 3)
            return true;
    }
    return false;
}

static void IndexToWorld(const GridData& g, int cellIndex, int& x, int& y, int& z) {
    int lx = cellIndex % g.sizeX;
    int t = cellIndex / g.sizeX;
    int ly = t % g.sizeY;
    int lz = t / g.sizeY;
    x = lx + g.originX;
    y = ly + g.originY;
    z = lz + g.originZ;
}

void GridData::GetCellSelectionBox(int cellIndex, Vector& outMin, Vector& outMax) {
    CellAabb boxes[4];
    int n = GetCellCollisionBoxArray(cellIndex, boxes, 4);
    if (n <= 0) {
        int x = 0, y = 0, z = 0;
        IndexToWorld(*this, cellIndex, x, y, z);
        outMin = Vector((float)x, (float)y, (float)z);
        outMax = Vector((float)x + 1.f, (float)y + 1.f, (float)z + 1.f);
        return;
    }
    outMin = boxes[0].mn;
    outMax = boxes[0].mx;
    for (int i = 1; i < n; ++i) {
        outMin.x = std::min(outMin.x, boxes[i].mn.x);
        outMin.y = std::min(outMin.y, boxes[i].mn.y);
        outMin.z = std::min(outMin.z, boxes[i].mn.z);
        outMax.x = std::max(outMax.x, boxes[i].mx.x);
        outMax.y = std::max(outMax.y, boxes[i].mx.y);
        outMax.z = std::max(outMax.z, boxes[i].mx.z);
    }
}

bool GridData::CellHasCollision(int cellIndex) const {
    if (cellIndex < 0 || cellIndex >= (int)cellTypes.size()) return false;
    unsigned char t = cellTypes[cellIndex];
    if (t == 0) return false;
    const Material* m = GetMaterial((int)t);
    if (!m) {
        return t != 5 && t != 11 && t != 10 && t != 20 &&
               t != 12 && t != 13 && t != 14 && t != 17 && t != 18;
    }
    uint8_t cls = m->prop01();
    if (cls == 1 || cls == 2 || cls == 4) return false;
    if (!m->col()) return false;
    return true;
}

int GridData::GetCellCollisionBoxArray(int cellIndex, CellAabb* out, int maxOut) const {
    if (!out || maxOut <= 0 || cellIndex < 0 || cellIndex >= (int)cellTypes.size())
        return 0;
    unsigned char t = cellTypes[cellIndex];
    if (t == 0) return 0;
    const Material* m = GetMaterial((int)t);
    int cx = 0, cy = 0, cz = 0;
    IndexToWorld(*this, cellIndex, cx, cy, cz);
    float ox = (float)cx, oy = (float)cy, oz = (float)cz;
    if (!m) {
        if (!CellHasCollision(cellIndex)) return 0;
        out[0].mn = Vector(ox, oy, oz);
        out[0].mx = Vector(ox + 1.f, oy + 1.f, oz + 1.f);
        return 1;
    }
    uint8_t cls = m->prop01();
    if (cls == 1 || cls == 2 || cls == 4) return 0;
    if (!m->col()) return 0;

    unsigned short st = (cellIndex < (int)cellStates.size()) ? cellStates[cellIndex] : 0;
    int n = 0;
    auto push = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
        if (n >= maxOut) return;
        out[n].mn = Vector(x0, y0, z0);
        out[n].mx = Vector(x1, y1, z1);
        ++n;
    };

    if (cls == 0) {
        int ht = (int)m->height();
        if (ht <= 0 || ht > 16) ht = 16;
        float h = (float)ht / 16.f;
        float y0 = oy, y1 = oy + h;
        if (st & 4) { y1 = oy + 1.f; y0 = y1 - h; }
        push(ox, y0, oz, ox + 1.f, y1, oz + 1.f);
        return n;
    }
    if (cls == 3) {
        int facing = st & 7;
        float th = 3.f / 16.f;
        if (facing == 0) push(ox, oy, oz, ox+1, oy+1, oz+th);
        else if (facing == 2) push(ox, oy, oz, ox+th, oy+1, oz+1);
        else if (facing == 3) push(ox+1-th, oy, oz, ox+1, oy+1, oz+1);
        else push(ox, oy, oz+1-th, ox+1, oy+1, oz+1);
        return n;
    }
    if (cls == 5) {
        push(ox + 6.f/16.f, oy, oz + 6.f/16.f, ox + 10.f/16.f, oy + 1.f, oz + 10.f/16.f);
        return n;
    }
    if (cls == 6) {
        int facing = st & 3;
        bool up = (st & 4) != 0;
        float yLow0 = up ? oy + 0.5f : oy;
        float yLow1 = yLow0 + 0.5f;
        float yHigh0 = up ? oy : oy + 0.5f;
        float yHigh1 = yHigh0 + 0.5f;
        push(ox, yLow0, oz, ox + 1.f, yLow1, oz + 1.f);
        float hx0=ox, hx1=ox+1, hz0=oz, hz1=oz+1;
        if (facing == 0) hz0 = oz + 0.5f;
        else if (facing == 1) hz1 = oz + 0.5f;
        else if (facing == 2) hx0 = ox + 0.5f;
        else hx1 = ox + 0.5f;
        push(hx0, yHigh0, hz0, hx1, yHigh1, hz1);
        return n;
    }
    if (cls == 7) {
        int facing = st & 3;
        if (st & 4) facing = (facing + 1) & 3;
        float th = 3.f / 16.f;
        if (facing == 0) push(ox, oy, oz, ox+1, oy+1, oz+th);
        else if (facing == 1) push(ox, oy, oz+1-th, ox+1, oy+1, oz+1);
        else if (facing == 2) push(ox, oy, oz, ox+th, oy+1, oz+1);
        else push(ox+1-th, oy, oz, ox+1, oy+1, oz+1);
        return n;
    }
    if (cls == 8) {
        push(ox+4.f/16.f, oy+4.f/16.f, oz+4.f/16.f, ox+12.f/16.f, oy+12.f/16.f, oz+12.f/16.f);
        return n;
    }
    if (cls == 9) {
        push(ox + 6.f/16.f, oy, oz + 6.f/16.f, ox + 10.f/16.f, oy + 1.f, oz + 10.f/16.f);
        int facing = st & 3;
        if (facing <= 1)
            push(ox, oy + 5.f/16.f, oz + 6.f/16.f, ox+1, oy + 12.f/16.f, oz + 10.f/16.f);
        else
            push(ox + 6.f/16.f, oy + 5.f/16.f, oz, ox + 10.f/16.f, oy + 12.f/16.f, oz+1);
        return n;
    }
    if (cls == 10) {
        int facing = st & 3;
        bool open = (st & 4) != 0;
        float th = 3.f / 16.f;
        if (!open) {
            float y0 = (st & 8) ? oy + 13.f/16.f : oy;
            push(ox, y0, oz, ox+1, y0 + th, oz+1);
        } else if (facing == 0) push(ox, oy, oz, ox+1, oy+1, oz+th);
        else if (facing == 1) push(ox, oy, oz+1-th, ox+1, oy+1, oz+1);
        else if (facing == 2) push(ox, oy, oz, ox+th, oy+1, oz+1);
        else push(ox+1-th, oy, oz, ox+1, oy+1, oz+1);
        return n;
    }
    push(ox, oy, oz, ox+1, oy+1, oz+1);
    return n;
}

// ActivateCell @ 0xa1a60
// Gate: type in [0x11, 0x11+0x41]; jumptable type-0x11.
// Type-swap pairs (state |= 0xC000):
//   17↔18 torch, 34↔35, 39↔40 furnace, 44↔45
// Open-bit toggle (state bit 0x08, |= 0xC000) for door/trapdoor/gate paths
// (types 63,67,75,78… and BMT 7 vertical pair walk).
bool GridData::ActivateCell(int cellIndex) {
    if (cellIndex < 0 || cellIndex >= (int)cellTypes.size())
        return false;
    unsigned char type = cellTypes[cellIndex];
    if (!IsActiveCellType(type))
        return false;

    unsigned short st = (cellIndex < (int)cellStates.size()) ? cellStates[cellIndex] : 0;
    int wx = 0, wy = 0, wz = 0;
    IndexToWorld(*this, cellIndex, wx, wy, wz);

    auto swapType = [&](unsigned char toType) {
        unsigned short ns = (unsigned short)(st | 0xC000);
        SetCell(wx, wy, wz, toType, ns);
        return true;
    };
    switch (type) {
        case 17: return swapType(18); // Torch → Active Torch
        case 18: return swapType(17);
        case 34: return swapType(35);
        case 35: return swapType(34);
        case 39: return swapType(40); // Furnace ↔ Active Furnace
        case 40: return swapType(39);
        case 44: return swapType(45);
        case 45: return swapType(44);
        default: break;
    }

    // Open-bit toggle (door / trapdoor / gate) @ 0xa1bc0
    unsigned short newSt = (unsigned short)((st & 0x08) ? (st & ~0x08) : (st | 0x08));
    newSt = (unsigned short)(newSt | 0xC000);
    SetCell(wx, wy, wz, type, newSt);

    // Vertical door pair (BMT==7): Y±1 halves with matching facing low bits
    const Material* mat = GetMaterial((int)type);
    if (mat && mat->prop01() == 7) {
        auto togglePair = [&](int dy) {
            int ny = wy + dy;
            int nidx = CoordToIndex(wx, ny, wz);
            if (nidx < 0 || nidx >= (int)cellTypes.size()) return;
            unsigned char nt = cellTypes[nidx];
            const Material* nm = GetMaterial((int)nt);
            if (!nm || nm->prop01() != 7) return;
            unsigned short ns = (nidx < (int)cellStates.size()) ? cellStates[nidx] : 0;
            if ((ns & 7) != (st & 7)) return;
            unsigned short nn = (unsigned short)((ns & 0x08) ? (ns & ~0x08) : (ns | 0x08));
            nn = (unsigned short)(nn | 0xC000);
            SetCell(wx, ny, wz, nt, nn);
        };
        togglePair(+1);
        togglePair(-1);
    }
    return true;
}

bool GridData::IsActiveCellType(unsigned char blockType) const {
    // Binary IsActiveCell @ 0xa1a20: b = type-0x11; if (b > 0x41) return 0;
    // else jump table → only 18 entries land on `mov eax,1; ret` (0xa1a58).
    static const unsigned char kActive[] = {
        17, 18, 34, 35, 39, 40, 44, 45,
        67, 68, 69, 70, 71, 78, 79, 80, 81, 82
    };
    for (unsigned char t : kActive)
        if (t == blockType) return true;
    return false;
}

GridData::LeavesQuality GridData::GetLeavesQuality() const {
    return leavesQuality_; // +0xec
}
void GridData::SetLeavesQuality(LeavesQuality q) {
    // Binary GridData::SetLeavesQuality @ 0x…:
    //   if (+0xec != q) { +0xec = q; for every region slot: *(region+0x1a)=1; }
    // Our Region dirty flag is +0x1f; mark every XZ column + sparse dirty list
    // so EnsureRegionMeshes rebuilds without waiting for SetCell.
    if (leavesQuality_ == q) return;
    leavesQuality_ = q;
    for (size_t i = 0; i < regions_.size(); ++i) {
        Region& r = regions_[i];
        if (r.dirty() == 0) {
            r.dirty() = 1;
            r.active() = 1;
            dirtyRegionIndices_.push_back((int)i);
        }
    }
    meshesDirty_ = true;
}

// GridData::Resize @ 0x81fb0 — align bounds to 16, expand/rebind storage
bool GridData::Resize(int minX, int minY, int minZ, int maxX, int maxY, int maxZ, int /*flags*/) {
    // GridData::Resize @ 0x81fb0 — align each of first three extent args to 16 when misaligned
    auto alignExtent = [](int v) -> int {
        // From 0x81feb..0x82011: if (v & 0xf) != 0, for non-negative: (v+15)&~15
        // (binary also has a +0x10 branch when remainder path taken differently; extents from
        // draw-distance table are already multiples of 16/32 so path usually hits aligned case)
        if ((v & 15) == 0) return v;
        if (v >= 0) return (v + 15) & ~15;
        int n = -v;
        n = (n + 15) & ~15;
        return -n;
    };
    int a0 = alignExtent(minX);
    int a1 = alignExtent(minY);
    int a2 = alignExtent(minZ);
    int a3 = alignExtent(maxX);
    int a4 = alignExtent(maxY);
    int a5 = alignExtent(maxZ);

    Vector newMin{(float)std::min(a0, a3), (float)std::min(a1, a4), (float)std::min(a2, a5)};
    Vector newMax{(float)std::max(a0, a3), (float)std::max(a1, a4), (float)std::max(a2, a5)};

    // Early-out if size fields unchanged (binary cmp this+0x44 / +0x48 family)
    if (newMin.x == worldMin.x && newMin.y == worldMin.y && newMin.z == worldMin.z &&
        newMax.x == worldMax.x && newMax.y == worldMax.y && newMax.z == worldMax.z) {
        return true;
    }

    int ox = originX, oy = originY, oz = originZ;
    int oldSX = sizeX, oldSY = sizeY, oldSZ = sizeZ;
    std::vector<unsigned char> oldTypes = cellTypes;
    std::vector<unsigned short> oldStates = cellStates;

    worldMin = newMin;
    worldMax = newMax;
    originX = (int)worldMin.x;
    originY = (int)worldMin.y;
    originZ = (int)worldMin.z;
    sizeX = (int)(worldMax.x - worldMin.x);
    sizeY = (int)(worldMax.y - worldMin.y);
    sizeZ = (int)(worldMax.z - worldMin.z);
    if (sizeX < 16) sizeX = 16;
    if (sizeY < 16) sizeY = 16;
    if (sizeZ < 16) sizeZ = 16;

    size_t n = (size_t)sizeX * (size_t)sizeY * (size_t)sizeZ;
    cellTypes.assign(n, 0);
    cellStates.assign(n, 0);

    // Copy overlapping volume from previous storage (Store snapshot preferred when present)
    const std::vector<unsigned char>* srcT = storeTypes_.empty() ? &oldTypes : &storeTypes_;
    const std::vector<unsigned short>* srcS = storeStates_.empty() ? &oldStates : &storeStates_;
    int srcOX = storeTypes_.empty() ? ox : storeOriginX_;
    int srcOY = storeTypes_.empty() ? oy : storeOriginY_;
    int srcOZ = storeTypes_.empty() ? oz : storeOriginZ_;
    int srcSX = storeTypes_.empty() ? oldSX : storeSizeX_;
    int srcSY = storeTypes_.empty() ? oldSY : storeSizeY_;
    int srcSZ = storeTypes_.empty() ? oldSZ : storeSizeZ_;
    if (!srcT->empty() && srcSX > 0 && srcSY > 0 && srcSZ > 0) {
        int x0 = std::max(originX, srcOX);
        int y0 = std::max(originY, srcOY);
        int z0 = std::max(originZ, srcOZ);
        int x1 = std::min(originX + sizeX, srcOX + srcSX);
        int y1 = std::min(originY + sizeY, srcOY + srcSY);
        int z1 = std::min(originZ + sizeZ, srcOZ + srcSZ);
        for (int z = z0; z < z1; ++z)
        for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            size_t si = (size_t)(x - srcOX) + (size_t)srcSX * ((size_t)(y - srcOY) + (size_t)srcSY * (size_t)(z - srcOZ));
            size_t di = (size_t)(x - originX) + (size_t)sizeX * ((size_t)(y - originY) + (size_t)sizeY * (size_t)(z - originZ));
            if (si < srcT->size() && di < cellTypes.size()) {
                cellTypes[di] = (*srcT)[si];
                if (si < srcS->size()) cellStates[di] = (*srcS)[si];
            }
        }
    }
    aux4000L0_.assign((n >> 4) + 1, 0);
    aux4000L1_.assign((n >> 8) + 1, 0);
    aux4000L2_.assign((n >> 12) + 1, 0);
    aux4000L3_.assign((n >> 16) + 1, 0);
    InitRegions();
    SyncHierarchyFromStates();
    meshesDirty_ = true;
    return true;
}

// GridData::Store @ 0xa3770 — StopProcessing, mark region flags 0xff, snapshot cell buffers
void GridData::Store() {
    // Binary also writes 0x7fffffff to +0x20/+0x24 and fills region-tag bytes with 0xff.
    storeTypes_ = cellTypes;
    storeStates_ = cellStates;
    storeOriginX_ = originX;
    storeOriginY_ = originY;
    storeOriginZ_ = originZ;
    storeSizeX_ = sizeX;
    storeSizeY_ = sizeY;
    storeSizeZ_ = sizeZ;
}

// GridData::Restore @ 0xa4d50 — re-materialize near player from stored snapshot
void GridData::Restore(const Vector& /*playerPos*/) {
    if (storeTypes_.empty()) return;
    // Full binary restores streaming chunks around player; host path copies overlap via Resize.
    // If sizes match, direct copy:
    if (storeSizeX_ == sizeX && storeSizeY_ == sizeY && storeSizeZ_ == sizeZ &&
        storeOriginX_ == originX && storeOriginY_ == originY && storeOriginZ_ == originZ) {
        cellTypes = storeTypes_;
        cellStates = storeStates_;
    }
    meshesDirty_ = true;
}

// GridData::InvalidateLighting @ 0x798f0 — zero light map (+0x7c count +0x88) and hierarchy
void GridData::InvalidateLighting() {
    // Binary: for i in [0, count): light[i]=0; then clear 4 hierarchy bands at +0x280 / +0x2a8
    // Host model has no separate light[] yet; mark meshes dirty so mesher rebuilds vertex colors.
    meshesDirty_ = true;
}


void GridData::Region::Init(int rx, int ry, int rz) {
    // Binary Region::Init @ 0x9af10: +8/+c/+10 = args; +0/+4 = 0x7fffffff
    x = rx; y = ry; z = rz;
    unknown0 = unknown1 = 0x7fffffff;
    flag18() = 0;
    flag19() = 0;
    active() = 0;
    dirty() = 0;
}
void GridData::Region::Free() {
    // Binary Region::Free @ 0x9af40: clears +0x18 and releases mesh resources
    flag18() = 0;
    flag19() = 0;
    dirty() = 0;
    active() = 0;
}



unsigned int GridData::EncodeWorldPos(int x, int z) {
    // Exact recovery from 0xa2300:
    // if negative: add 15 before sar 4 (arithmetic floor-divide by 16)
    auto region = [](int v) -> int {
        int t = v;
        if (t < 0) t = v + 15;
        return (t >> 4) + 0x8000;
    };
    unsigned int hx = (unsigned int)region(x) << 16;
    unsigned int hz = (unsigned int)region(z);
    return hx | hz;
}

void GridData::DecodeWorldPos(int& x, int& z, unsigned int key) {
    int rx = (int)((key >> 16) & 0xFFFF) - 0x8000;
    int rz = (int)(key & 0xFFFF) - 0x8000;
    x = rx * 16;
    z = rz * 16;
}

void GridData::MarkModifiedRegions() {
    // Original @ 0x7aad0 — walks dirty cells and flags region entries.
    // Stub must NOT raise meshesDirty_ alone: that triggered full-world
    // EnsureRegionMeshes when dirtyRegions_ was empty (constant hitch).
    // SetCell already MarkRegionDirty + meshesDirty_ for real edits.
}

void GridData::Invalidate() {
    // Original @ 0x794a0 — forces full mesh rebuild
    MarkModifiedRegions();
}




// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// BoxLineSegIntersect @ 0xb3520 — AABB vs segment; writes entry t, exit t, face.
//
// Binary face codes written to param_7 (confirmed via pdg + GetAddingPosition
// jumptable @ 0x99320 → offsets x±1 / y±1 / z±1):
//   0 = XP (+X face)   3 = XN (−X face)
//   1 = YP (+Y face)   4 = YN (−Y face)
//   2 = ZP (+Z face)   5 = ZN (−Z face)
//
// Face is the face of the box that the segment *enters* through (tEnter plane).
// When going +axis, entry is at box min → negative-side face (XN=3, YN=4, ZN=5).
// When going −axis, entry is at box max → positive-side face (XP=0, YP=1, ZP=2).
// ---------------------------------------------------------------------------
static bool BoxLineSegIntersect(const Vector& bmin, const Vector& bmax,
                                const Vector& from, const Vector& to,
                                float& outTEnter, float& outTExit, int& outFace) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float dz = to.z - from.z;
    const float eps = 1e-8f;

    float tEnter = 0.f;
    float tExit = 1.f;
    int face = -1;

    auto axis = [&](float p, float d, float mn, float mx, int faceMin, int faceMax) -> bool {
        if (std::fabs(d) < eps) {
            return p >= mn && p <= mx;
        }
        float inv = 1.f / d;
        float tA = (mn - p) * inv;
        float tB = (mx - p) * inv;
        int fA = faceMin;
        int fB = faceMax;
        if (tA > tB) {
            float tmp = tA; tA = tB; tB = tmp;
            int tf = fA; fA = fB; fB = tf;
        }
        if (tA > tEnter) {
            tEnter = tA;
            face = fA;
        }
        if (tB < tExit)
            tExit = tB;
        return tEnter <= tExit;
    };

    // X: min plane → face 3 (XN), max plane → face 0 (XP)
    // Y: min plane → face 4 (YN), max plane → face 1 (YP)
    // Z: min plane → face 5 (ZN), max plane → face 2 (ZP)
    if (!axis(from.x, dx, bmin.x, bmax.x, 3, 0)) return false;
    if (!axis(from.y, dy, bmin.y, bmax.y, 4, 1)) return false;
    if (!axis(from.z, dz, bmin.z, bmax.z, 5, 2)) return false;
    if (tExit < 0.f || tEnter > 1.f) return false;
    if (tEnter < 0.f) tEnter = 0.f;
    if (face < 0) face = 0;

    outTEnter = tEnter;
    outTExit = tExit;
    outFace = face;
    return true;
}

static bool ClipRayAABB(const Vector& from, const Vector& to,
                        const Vector& bmin, const Vector& bmax,
                        float& t0, float& t1) {
    int face = -1;
    return BoxLineSegIntersect(bmin, bmax, from, to, t0, t1, face);
}

// FindFirstUsedCell @ 0x79dd0 (2541 B)
// Binary path on non-air cell:
//   GetCellCollisionBoxArray → for each AABB BoxLineSegIntersect → keep nearest
//   then GetCellSelectionBox + BoxLineSegIntersect again for final face/t
// Face comes from BoxLineSegIntersect, NOT from DDA step axis.
bool GridData::FindFirstUsedCell(const Vector& from, const Vector& to,
                                 float& outT, int& outCellIndex, int& outFace) {
    outT = 0.f;
    outCellIndex = -1;
    outFace = -1;
    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0) return false;

    Vector wbmin((float)originX, (float)originY, (float)originZ);
    Vector wbmax((float)(originX + sizeX), (float)(originY + sizeY), (float)(originZ + sizeZ));

    float t0, t1;
    if (!ClipRayAABB(from, to, wbmin, wbmax, t0, t1)) return false;
    if (t0 < 0.f) t0 = 0.f;
    if (t1 > 1.f) t1 = 1.f;

    float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    float ex = from.x + dx * t0;
    float ey = from.y + dy * t0;
    float ez = from.z + dz * t0;

    int x = (int)std::floor(ex);
    int y = (int)std::floor(ey);
    int z = (int)std::floor(ez);
    if (x < originX) x = originX;
    if (y < originY) y = originY;
    if (z < originZ) z = originZ;
    if (x >= originX + sizeX) x = originX + sizeX - 1;
    if (y >= originY + sizeY) y = originY + sizeY - 1;
    if (z >= originZ + sizeZ) z = originZ + sizeZ - 1;

    int stepX = dx > 0.f ? 1 : (dx < 0.f ? -1 : 0);
    int stepY = dy > 0.f ? 1 : (dy < 0.f ? -1 : 0);
    int stepZ = dz > 0.f ? 1 : (dz < 0.f ? -1 : 0);

    auto frac = [](float v) { return v - std::floor(v); };
    float tMaxX = 1e30f, tMaxY = 1e30f, tMaxZ = 1e30f;
    float tDeltaX = 1e30f, tDeltaY = 1e30f, tDeltaZ = 1e30f;
    float t = t0;
    if (stepX != 0) {
        tMaxX = t0 + ((stepX > 0) ? (1.f - frac(ex)) : (frac(ex) > 0 ? frac(ex) : 1.f)) / std::fabs(dx);
        tDeltaX = 1.f / std::fabs(dx);
    }
    if (stepY != 0) {
        tMaxY = t0 + ((stepY > 0) ? (1.f - frac(ey)) : (frac(ey) > 0 ? frac(ey) : 1.f)) / std::fabs(dy);
        tDeltaY = 1.f / std::fabs(dy);
    }
    if (stepZ != 0) {
        tMaxZ = t0 + ((stepZ > 0) ? (1.f - frac(ez)) : (frac(ez) > 0 ? frac(ez) : 1.f)) / std::fabs(dz);
        tDeltaZ = 1.f / std::fabs(dz);
    }

    // Binary: GetCellCollisionBoxArray returns 0 for non-solid → DDA continues.
    for (int i = 0; i < 512 && t <= t1; ++i) {
        int cidx = CoordToIndex(x, y, z);
        if (cidx >= 0 && cidx < (int)cellTypes.size()) {
            CellAabb boxes[4];
            int n = GetCellCollisionBoxArray(cidx, boxes, 4);
            if (n > 0) {
                float bestT = 1.f;
                int bestFace = -1;
                bool any = false;
                for (int bi = 0; bi < n; ++bi) {
                    float te = 0.f, tx = 1.f;
                    int fc = -1;
                    if (BoxLineSegIntersect(boxes[bi].mn, boxes[bi].mx, from, to, te, tx, fc)) {
                        if (te < bestT) {
                            bestT = te;
                            bestFace = fc;
                            any = true;
                        }
                    }
                }
                if (any) {
                    // Binary: refine with GetCellSelectionBox + BoxLineSegIntersect
                    Vector smin, smax;
                    GetCellSelectionBox(cidx, smin, smax);
                    float te = 0.f, tx = 1.f;
                    int fc = -1;
                    if (BoxLineSegIntersect(smin, smax, from, to, te, tx, fc)) {
                        bestT = te;
                        bestFace = fc;
                    }
                    outT = bestT;
                    outCellIndex = cidx;
                    outFace = bestFace;
                    return true;
                }
            }
        }
        // DDA step (region flag +0x1e approximated as always active)
        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                t = tMaxX; tMaxX += tDeltaX; x += stepX;
            } else {
                t = tMaxZ; tMaxZ += tDeltaZ; z += stepZ;
            }
        } else {
            if (tMaxY < tMaxZ) {
                t = tMaxY; tMaxY += tDeltaY; y += stepY;
            } else {
                t = tMaxZ; tMaxZ += tDeltaZ; z += stepZ;
            }
        }
        if (x < originX || y < originY || z < originZ ||
            x >= originX + sizeX || y >= originY + sizeY || z >= originZ + sizeZ)
            break;
    }
    return false;
}

// IsHidden (0xa3340, 432 B)
// Called with region coords (cell>>4). Checks region metadata tables against
// coordinate ranges and a byte array (values 0xff = sentinel).
// Exact semantics unresolved (likely leaves-quality / LOD culling).
// Until region tables at the referenced offsets are mapped, return false
// (not hidden) so solid blocks are never incorrectly skipped.
bool GridData::IsHidden(int /*regionCoordA*/, int /*regionCoordB*/) const {
    return false; // unresolved — see AUDIT
}

// ---------------------------------------------------------------------------
// TracePoint (0x7c050) — dual path orchestrator
// ---------------------------------------------------------------------------
void GridData::TracePoint(const Vector& from, const Vector& to,
                          GridDataTraceResult& result, void* /*entity*/) {
    result.Clear();

    // Path 1: entity tracers at +0xc54/+0xc58
    // Original: for each tracer, virtual call +0x7c(from,to,result); OR hit flag.
    // Entity-tracer list ownership/registration not yet recovered — empty.

    // Path 2: FindFirstUsedCell + IsHidden
    // Binary TracePoint @ 0x7c050:
    //   face = local_20[0] from FindFirstUsedCell (BoxLineSegIntersect), written
    //   to result+0x14. Must NOT re-derive face from look-vector dominant axis.
    float t = 0.f;
    int cellIndex = -1;
    int face = -1;
    if (!FindFirstUsedCell(from, to, t, cellIndex, face)) return;
    if (cellIndex < 0) return;

    // Decode cell coords from index
    int lx = cellIndex % sizeX;
    int ly = (cellIndex / sizeX) % sizeY;
    int lz = cellIndex / (sizeX * sizeY);
    int cx = originX + lx, cy = originY + ly, cz = originZ + lz;

    // IsHidden filter (region coords = floor_div16)
    auto reg = [](int v) { return (v < 0) ? (v + 15) >> 4 : v >> 4; };
    if (IsHidden(reg(cx), reg(cz))) {
        // Original continues search; for now accept miss (rare with IsHidden=false)
        return;
    }

    float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    result.hit = true;
    result.cellIndex = cellIndex;
    result.cellX = cx; result.cellY = cy; result.cellZ = cz;
    result.distance = t;
    result.blockType = cellTypes[cellIndex];
    result.hitPos = Vector(from.x + dx * t, from.y + dy * t, from.z + dz * t);
    // Binary: *(result+0x14) = face from FindFirstUsedCell / BoxLineSegIntersect
    // Enum: 0=XP 1=YP 2=ZP 3=XN 4=YN 5=ZN
    if (face < 0) {
        // BoxLineSegIntersect default when entry t clamped to 0 (ray origin inside)
        face = 1; // binary FindFirstUsedCell sets local_20[0]=1 when == -1
    }
    result.face = face;
}

// FullHeightBlockVariant @ 0x98f20
// BMT==0 && height!=0x10: scan materials for height==0x10 with matching UV faces
// (memcmp Uxp..Uzn and Vxp..Vzn). Returns that full type id when found.
bool GridData::FullHeightBlockVariant(unsigned char partialType, unsigned char* outFullType) const {
    if (materials.empty() || partialType >= materials.size()) return false;
    const Material& src = materials[partialType];
    if (src.prop01() != 0) return false;
    if (src.height() == 0x10) return false;
    for (int i = 1; i < (int)materials.size(); ++i) {
        const Material& cand = materials[i];
        if (cand.height() != 0x10) continue;
        // Binary: memcmp mat+0x0c.. (6 U) and mat+0x12.. (6 V)
        if (std::memcmp(&cand.raw[0x0c], &src.raw[0x0c], 6) != 0) continue;
        if (std::memcmp(&cand.raw[0x12], &src.raw[0x12], 6) != 0) continue;
        if (outFullType) *outFullType = (unsigned char)i;
        return true;
    }
    if (outFullType) *outFullType = partialType;
    return false;
}


// SAU (+0x05) — binary ConfigureState @ 0x997f0 ONLY consumer of mat[+0x05]:
//   *sauOut = mat[+0x05];
//   if (SAU && type∈{0x18,0x19}):  // Sand / Gravel
//     if (localY < 1 || types[cell - sizeX] != 0) *sauOut = false;
// EntityPlayer maps sauOut→place mode 2 vs 1; BOTH modes only SetCell at the
// same index (FixedUpdate / FixedPostUpdate). No FallingSand entity, no SAU
// tick, no multi-cell settle in this .so (xrefs of mat[+0x05] exhausted).

bool GridData::ComputeSauPlaceFlag(unsigned char blockType, int cellIndex) const {
    // Binary ConfigureState @ 0x997f0:
    //   *param_6 = mat[+0x05];
    //   if (SAU && (type-0x18) < 2) {  // Sand/Gravel only
    //     y = index → local Y; if (y < 1 || types[index - sizeX] != 0) *param_6 = false;
    //   }
    if (materials.empty() || blockType >= materials.size()) return false;
    if (!materials[blockType].sau()) return false;
    if (blockType != 0x18 && blockType != 0x19) return true;
    if (cellIndex < 0 || cellIndex >= (int)cellTypes.size()) return false;
    int y = (cellIndex / sizeX) % sizeY;
    if (y < 1) return false; // *param_6 = false
    int below = cellIndex - sizeX;
    if (below < 0 || below >= (int)cellTypes.size()) return false;
    if (cellTypes[below] != 0) return false; // *param_6 = false when support present
    return true; // unsupported → SAU flag stays
}

// =============================================================================
// SELF-MADE EXTENSION — visible SAU fall (NOT in binary .so)
// -----------------------------------------------------------------------------
// Binary only sets place mode from SAU flag; both modes SetCell in-place.
// This extension keeps the same support test (type below == 0 / y>=1) but moves
// the block ONE cell down per TickSau period so motion is stepwise, not an
// instant multi-cell teleport.
// DO NOT strip in binary-cleanup passes unless the user explicitly asks.
// =============================================================================

void GridData::ScheduleSauCheck(int worldX, int worldY, int worldZ) {
    int idx = CoordToIndex(worldX, worldY, worldZ);
    if (idx < 0 || idx >= (int)cellTypes.size()) return;
    unsigned char t = cellTypes[idx];
    if (t == 0) return;
    const Material* mat = GetMaterial((int)t);
    if (!mat || !mat->sau()) return;
    if (!ComputeSauPlaceFlag(t, idx)) return;
    for (int q : sauPending_)
        if (q == idx) return;
    sauPending_.push_back(idx);
}

void GridData::TickSau(int dtMs, int periodMs, int maxSteps) {
    if (periodMs < 16) periodMs = 16;
    sauClockMs_ += dtMs;
    if (sauClockMs_ < sauDeadlineMs_) return;
    sauDeadlineMs_ = sauClockMs_ + periodMs;
    if (sauPending_.empty() || maxSteps <= 0) return;

    // Snapshot queue so ScheduleSauCheck during processing only affects next wave
    std::vector<int> cur = std::move(sauPending_);
    sauPending_.clear();
    int steps = 0;
    for (int idx : cur) {
        if (steps >= maxSteps) {
            // defer remainder
            bool dup = false;
            for (int q : sauPending_) if (q == idx) { dup = true; break; }
            if (!dup) sauPending_.push_back(idx);
            continue;
        }
        if (idx < 0 || idx >= (int)cellTypes.size()) continue;
        unsigned char t = cellTypes[idx];
        if (t == 0) continue;
        const Material* mat = GetMaterial((int)t);
        if (!mat || !mat->sau()) continue;
        if (!ComputeSauPlaceFlag(t, idx)) continue;

        int lx = idx % sizeX;
        int ly = (idx / sizeX) % sizeY;
        int lz = idx / (sizeX * sizeY);
        int wx = originX + lx, wy = originY + ly, wz = originZ + lz;

        int below = CoordToIndex(wx, wy - 1, wz);
        if (below < 0) continue; // bottom of world — remain (no void-delete in binary)
        if (cellTypes[below] != 0) continue;

        unsigned short st = cellStates[idx];
        SetCell(wx, wy, wz, 0, 0);
        SetCell(wx, wy - 1, wz, t, st);
        ++steps;

        // New cell may still be unsupported → schedule next step
        ScheduleSauCheck(wx, wy - 1, wz);
        // Column above may have lost support
        ScheduleSauCheck(wx, wy + 1, wz);
    }
}



// GetAddingPosition @ 0x99240
// Binary face jumptable (disasm 0x99320 / targets 0x994d0..0x993f8):
//   face 0 → x+1   face 1 → y+1   face 2 → z+1
//   face 3 → x−1   face 4 → y−1   face 5 → z−1
// Slab merge: place BMT0 onto existing BMT0 height<=0xf when
//   face==1 (YP) and existing is bottom slab (!(state&4)), or
//   face==4 (YN) and existing is top slab (state&4),
//   and FullHeightBlockVariant matches → same cell, full type.
bool GridData::GetAddingPosition(const GridDataTraceResult& tr, unsigned char blockType,
                                 int& outCellIndex, unsigned char& outFace,
                                 unsigned char* outType) {
    if (!tr.hit) return false;
    if (tr.blockType == 0x4c) return false;

    outFace = (unsigned char)tr.face;
    if (outType) *outType = blockType;

    int hitIdx = CoordToIndex(tr.cellX, tr.cellY, tr.cellZ);
    outCellIndex = hitIdx;

    // --- slab merge path (binary GetAddingPosition @ 0x992d2..) ---
    if (hitIdx >= 0 && hitIdx < (int)cellTypes.size() &&
        !materials.empty() && blockType < materials.size()) {
        unsigned char existType = cellTypes[hitIdx];
        const Material* placeMat = GetMaterial((int)blockType);
        const Material* existMat = GetMaterial((int)existType);
        // Binary: place BMT==0 && exist BMT==0 && exist height <= 0xf
        if (placeMat && existMat &&
            placeMat->prop01() == 0 && existMat->prop01() == 0 &&
            existMat->height() <= 0x0f) {
            unsigned short est = (hitIdx < (int)cellStates.size()) ? cellStates[hitIdx] : 0;
            bool mergeFace = false;
            if (tr.face == 1) {
                // YP: top of bottom slab
                if ((est & 4) == 0) mergeFace = true;
            } else if (tr.face == 4) {
                // YN: bottom of top slab
                if ((est & 4) != 0) mergeFace = true;
            }
            if (mergeFace) {
                unsigned char fullPlace = 0, fullExist = 0;
                if (FullHeightBlockVariant(blockType, &fullPlace) &&
                    FullHeightBlockVariant(existType, &fullExist) &&
                    fullPlace == fullExist) {
                    if (outType) *outType = fullPlace;
                    outCellIndex = hitIdx;
                    return true;
                }
            }
        }
    }

    // Binary face → neighbour (jumptable verified via rizin @ 0x99320)
    int x = tr.cellX, y = tr.cellY, z = tr.cellZ;
    switch (tr.face) {
        case 0: x++; break; // XP
        case 1: y++; break; // YP
        case 2: z++; break; // ZP
        case 3: x--; break; // XN
        case 4: y--; break; // YN
        case 5: z--; break; // ZN
        default: break;
    }
    outCellIndex = CoordToIndex(x, y, z);
    return outCellIndex >= 0;
}

bool GridData::IsPossibleToAdd(const GridDataTraceResult& tr, unsigned char blockType, int /*flags*/,
                               const Vector* playerPos, const Vector* playerSize) {
    int idx = -1; unsigned char face = 0; unsigned char placeType = blockType;
    if (!GetAddingPosition(tr, blockType, idx, face, &placeType)) return false;
    if (idx < 0 || idx >= (int)cellTypes.size()) return false;

    // Binary IsPossibleToAdd @ 0x99530: BMT==3 (ladder) rejects face YP(1) / YN(4).
    if (!materials.empty() && blockType < materials.size() &&
        materials[blockType].prop01() == 3) {
        if (tr.face == 1 || tr.face == 4)
            return false;
    }

    // Destination empty / liquid, OR same-cell slab merge (placeType is full variant).
    {
        unsigned char dt = cellTypes[idx];
        unsigned short st = (idx < (int)cellStates.size()) ? cellStates[idx] : 0;
        bool empty = (dt == 0 && !HasLiquidState(st));
        bool liquid = (dt == 0 && HasLiquidState(st));
        bool slabMerge = (placeType != blockType);
        if (!empty && !liquid && !slabMerge) return false;
    }

    // Entity list walk @ 0x99630: virtual Intersects(cellX,cellY,cellZ) on each entity.
    // With a single local player, reject if the destination cell AABB overlaps player AABB.
    // Player AABB: feet at position, size = (w,h,d); cell is unit cube [x,x+1)×[y,y+1)×[z,z+1).
    if (playerPos && playerSize) {
        int lx = idx % sizeX;
        int ly = (idx / sizeX) % sizeY;
        int lz = idx / (sizeX * sizeY);
        float bx0 = (float)(originX + lx);
        float by0 = (float)(originY + ly);
        float bz0 = (float)(originZ + lz);
        float bx1 = bx0 + 1.f, by1 = by0 + 1.f, bz1 = bz0 + 1.f;
        float hw = playerSize->x * 0.5f;
        float hd = playerSize->z * 0.5f;
        float px0 = playerPos->x - hw;
        float px1 = playerPos->x + hw;
        float py0 = playerPos->y;
        float py1 = playerPos->y + playerSize->y;
        float pz0 = playerPos->z - hd;
        float pz1 = playerPos->z + hd;
        bool overlap = !(px1 <= bx0 || px0 >= bx1 ||
                         py1 <= by0 || py0 >= by1 ||
                         pz1 <= bz0 || pz0 >= bz1);
        if (overlap) return false;
    }
    return true;
}

unsigned char GridData::MtlSelIndexToCell(int selIndex) const {
    // 0x98ed0: scan materials[0..count), compare int16 at +0x20 with selIndex; return type index
    if (materials.empty()) return 0;
    for (int i = 0; i < (int)materials.size(); ++i) {
        if (materials[i].selIndex() == (int16_t)selIndex)
            return (unsigned char)i;
    }
    return 0;
}

int GridData::CellToMtlSelIndex(unsigned char cellType) const {
    // 0x98f10: materials[cell].+0x20 as int16
    if (cellType >= materials.size()) return -1;
    return (int)materials[cellType].selIndex();
}

// ConfigureState (0x997f0, 765 B) — orientation / state bit computation
// Material table at +0xe8, stride 34 (type*32+type*2).
// Orientation from dominant look-vector axis:
//   state 0/1/2/3 based on |look.x| vs |look.z| and signs; bit 0x04 for variants.
// Special material property bytes at +0x01,+0x05,+0x08 drive liquid/slab paths.
// GetDirectionMarker, SetLiquidState, FullHeightBlockVariant called for specials.
// SetLiquidState @ 0x99720 — merge liquid bits into outState when target
// material has FTW (+0x1c) != 0 and source state carries 0x3FC0.
// Called from ConfigureState / ConfigureAirState. World cells stay type 0;
// this only builds the state word (type write is caller's job → 0 for liquids).
static void SetLiquidStateMerge(GridData& g, unsigned char dstType, unsigned short& outState,
                                unsigned char /*srcType*/, unsigned short srcState) {
    if (g.materials.empty() || dstType >= g.materials.size()) return;
    const Material& mat = g.materials[dstType];
    if (mat.raw[0x1c] == 0) return; // no FTW → not a liquid-capable row
    if ((srcState & 0x3fc0) == 0) return;
    int level = (int)((srcState & 0x1c00) >> 10);
    int matLvl = (int)mat.raw[0x04];
    if (level <= matLvl) {
        outState = (unsigned short)(outState | (srcState & 0x3fc0));
        return;
    }
    // Binary: MinWaterHeight path then recombine — approximate with clamped level
    int mh = GridData::MinWaterHeight(mat.raw[0x01], mat.raw[0x03], outState);
    (void)mh;
    outState = (unsigned short)(outState | (srcState & 0x3fc0));
}

// Binary facing from look: argmin of {look.x, look.z, -look.x, -look.z}
// → local_12 in {1, 2, 3, 0} respectively (ConfigureState @ 0x997f0 else-branch).
static unsigned short BinaryLookFacingXZ(float lx, float lz) {
    unsigned short face = 1;
    float best = lx;
    if (lz < best) { best = lz; face = 2; }
    if (-lx < best) { best = -lx; face = 3; }
    if (-lz < best) { face = 0; }
    return face;
}

// GetDirectionMarker @ 0x99060 — binary face enum 0=XP 1=YP 2=ZP 3=XN 4=YN 5=ZN.
// Returns marker codes consumed by ConfigureState (1/4 for slab bit, 0/2/3/5 stairs).
static int BinaryGetDirectionMarker(const GridDataTraceResult& tr,
                                    const Material& mat, const Vector& lookDir) {
    uint8_t bmt = mat.prop01();
    // Binary: if BMT==0 && height>0xf → -1; elif BMT not in {0,6,10,7} → -1
    if (bmt == 0) {
        if (mat.height() > 0x0f) return -1;
        // pdg: hitY < cellY+eps → (face!=4)*3+1 ; else (face==1)*3+1
        float cellY = (float)tr.cellY;
        if (tr.hitPos.y < cellY + 0.5f)
            return (tr.face != 4) ? 4 : 1; // YN(4)→1 else 4
        return (tr.face == 1) ? 4 : 1;     // YP(1)→4 else 1
    }
    if (bmt == 6 || bmt == 10) {
        // Same Y half / face tests as BMT0
        float cellY = (float)tr.cellY;
        if (tr.hitPos.y < cellY + 0.5f)
            return (tr.face != 4) ? 4 : 1;
        return (tr.face == 1) ? 4 : 1;
    }
    if (bmt == 7) {
        // Stairs: binary uses look vs hit cell corner + face equality on 0/2/3/5.
        // Full look/hit branch recovered in pdg; equality path uses result face.
        (void)lookDir;
        return tr.face;
    }
    return -1;
}

unsigned short GridData::ConfigureState(const GridDataTraceResult& tr,
                                        unsigned char blockType,
                                        const Vector& lookDir) {
    // Binary ConfigureState @ 0x997f0 (ghidra). UVR = mat[+0x08].
    // Branch:
    //   if (UVR==0 && BMT∉{6,7,10}) → face/trapdoor path; slab bit4 if marker==1
    //   else → look-based facing 0..3 (UVR≠0 or stairs/door-ish BMT 6/7/10)
    unsigned short state = 0;
    if (materials.empty() || blockType >= materials.size())
        return state;
    const Material& mat = materials[blockType];
    const uint8_t bmt = mat.prop01();
    const uint8_t uvr = mat.uvr();
    const int marker = BinaryGetDirectionMarker(tr, mat, lookDir);
    const int face = tr.face; // 0..5 project enum

    // --- Initial face bits for BMT 8 / 2 / 3 (torch / ladder / similar) ---
    if (bmt == 8 || bmt == 2) {
        if (face >= 0 && face < 6)
            state = (unsigned short)(state | (unsigned)face);
        else
            state = (unsigned short)(state | 1);
    } else if (bmt == 3) {
        if (face >= 0 && face <= 5)
            state = (unsigned short)(state | (unsigned)face);
        else
            state = (unsigned short)(state | 1);
    }

    const bool useLookFacing =
        (uvr != 0) || (bmt == 6) || (bmt == 7) || (bmt == 10);

    if (!useLookFacing) {
        // UVR==0 and not BMT 6/7/10
        if (bmt == 9) {
            // Trapdoor: from look X/Z signs (decompile LAB_000a9913)
            float lx = lookDir.x, lz = lookDir.z;
            float ax = std::fabs(lx), az = std::fabs(lz);
            // decompile: local_12 = (lz >= lx); then refine with -lx/-lz
            state = BinaryLookFacingXZ(lx, lz);
        }
        // Slab / partial BMT0: marker==1 → state |= 4
        if (marker == 1 && bmt == 0 && mat.height() <= 0x0f && mat.height() != 0 &&
            FullHeightBlockVariant(blockType, nullptr)) {
            state = (unsigned short)(state | 4);
        }
    } else {
        // UVR≠0 OR BMT∈{6,7,10}: encode horizontal facing from player look
        float lx = lookDir.x, lz = lookDir.z;
        state = BinaryLookFacingXZ(lx, lz);
        // Equality of marker to the chosen cardinal (decompile bVar7)
        bool markerMatch = false;
        // Map state→expected marker face codes used in decompile:
        // state 2 → marker==3, state 1 → marker==2, state 3 → marker==5, state 0 → marker==0
        int expect = 2;
        if (state == 2) expect = 3;
        else if (state == 1) expect = 2;
        else if (state == 3) expect = 5;
        else if (state == 0) expect = 0;
        markerMatch = (marker == expect);

        if (bmt == 7) {
            if (markerMatch)
                state = (unsigned short)(state | 4);
        } else if (bmt == 10) {
            if (marker == 1)
                state = (unsigned short)(state | 4);
        } else if (bmt == 6) {
            // stairs: marker path continues to bit4 when marker==1 after face encode
            if (marker == 1)
                state = (unsigned short)(state | 4);
        }
        // UVR-only BMT0 (furnace/log): state lower bits = facing; no forced bit4 here
        if (bmt == 0 && marker == 1 && mat.height() <= 0x0f && mat.height() != 0 &&
            FullHeightBlockVariant(blockType, nullptr)) {
            state = (unsigned short)(state | 4);
        }
    }
    return state;
}

bool GridData::PlaceBlock(const GridDataTraceResult& tr, unsigned char blockType,
                          const Vector* playerPos, const Vector* playerSize,
                          const Vector* playerLook) {
    if (!IsPossibleToAdd(tr, blockType, 0, playerPos, playerSize)) return false;
    int idx = -1; unsigned char face = 0; unsigned char placeType = blockType;
    GetAddingPosition(tr, blockType, idx, face, &placeType);
    int lx = idx % sizeX;
    int ly = (idx / sizeX) % sizeY;
    int lz = idx / (sizeX * sizeY);
    // Binary ConfigureState uses EntityPlayer look vector, not hit-face normal.
    // Face enum: 0=XP 1=YP 2=ZP 3=XN 4=YN 5=ZN (fallback only when look absent).
    Vector look(0, 0, 1);
    if (playerLook) {
        look = *playerLook;
    } else {
        if (face == 0) look = Vector(1.f, 0, 0);
        else if (face == 3) look = Vector(-1.f, 0, 0);
        else if (face == 2) look = Vector(0, 0, 1.f);
        else if (face == 5) look = Vector(0, 0, -1.f);
        else if (face == 1) look = Vector(0, 1.f, 0);
        else if (face == 4) look = Vector(0, -1.f, 0);
    }

    // Slab merge → full type (no upper bit; full height block).
    bool slabMerge = (placeType != blockType);
    unsigned short state = 0;
    if (slabMerge) {
        state = 0;
    } else {
        state = ConfigureState(tr, placeType, look);
    }

    // Binary EntityPlayer place: only type==5 becomes world liquid
    //   type=0, state=0xC3C0 (then we use WATER_SOURCE|0x4000 schedule bits).
    // SetLiquidState @ 0x99720: if mat[place].FTW!=0 and dest has 0x3FC0, OR those
    // bits into state — solid type is kept (torch/trapdoor into water keep type).
    unsigned char writeType = placeType;
    if (placeType == 5) {
        writeType = 0;
        state = (unsigned short)(WATER_SOURCE_STATE | STATE_BIT_4000);
    } else {
        // SetLiquidState merge from destination cell (binary end of ConfigureState)
        if (idx >= 0 && idx < (int)cellTypes.size()) {
            unsigned char destT = cellTypes[idx];
            unsigned short destS = (idx < (int)cellStates.size()) ? cellStates[idx] : 0;
            SetLiquidStateMerge(*this, placeType, state, destT, destS);
        }
        // state may gain 0x3FC0; writeType stays placeType (not forced to water)
    }
    SetCell(originX + lx, originY + ly, originZ + lz, writeType, state);
    // Binary: place at target cell. SELF-MADE: schedule stepwise SAU fall if unsupported.
    ScheduleSauCheck(originX + lx, originY + ly, originZ + lz);
    return true;
}

bool GridData::BreakBlock(const GridDataTraceResult& tr) {
    if (!tr.hit) return false;
    int idx = CoordToIndex(tr.cellX, tr.cellY, tr.cellZ);
    // Pure liquid (type 0 + fluid state): not breakable as a block — ray already
    // skips it; guard if hit somehow lands here.
    if (idx >= 0 && idx < (int)cellStates.size() && HasLiquidState(cellStates[idx]) &&
        (idx >= (int)cellTypes.size() || cellTypes[idx] == 0))
        return false;
    // ConfigureAirState (0x99af0) → air + state 0
    SetCell(tr.cellX, tr.cellY, tr.cellZ, 0, 0);
    // SELF-MADE: block above may be SAU and lose support
    ScheduleSauCheck(tr.cellX, tr.cellY + 1, tr.cellZ);
    return true;
}

// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Material table — InitMaterials @ 0x98720, LoadMaterials @ 0x987d0
// Source: assets/scenes/blocks.txt (text, # comments, GetLine parse)
// ---------------------------------------------------------------------------
bool GridData::InitMaterials() {
    materials.clear();
    materials.resize(256);
    for (int i = 0; i < 256; ++i)
        materials[i].initDefault(i);
    materialCount = 256;
    return true;
}

bool GridData::LoadMaterials(const char* path) {
    // GridData::LoadMaterials @ 0x987d0 — exact column map from Ghidra decomp.
    if (materials.empty()) InitMaterials();
    std::string text;
    if (!Assets::ReadText(path, text)) {
        FILE* fp = std::fopen(path, "r");
        if (!fp) return false;
        char buf[1024];
        while (std::fgets(buf, sizeof(buf), fp)) text += buf;
        std::fclose(fp);
    }
    // Strip UTF-8 BOM once at file start (binary checks first 3 bytes).
    size_t off = 0;
    if (text.size() >= 3 &&
        (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF) {
        off = 3;
    }
    std::istringstream in(text.substr(off));
    std::string line;
    int loaded = 0;
    int maxSel = 0;
    while (std::getline(in, line)) {
        const char* p = line.c_str();
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\0' || *p == '\n' || *p == '\r') continue;
        if (std::strstr(p, "Index") && std::strstr(p, "Uxp")) continue;

        // 29 fields — binary ParseString requires iVar5 == 0x1d.
        int idx = -1, sIndex = -1;
        int uxp = 0, vxp = 0, uyp = 0, vyp = 0, uzp = 0, vzp = 0;
        int uxn = 0, vxn = 0, uyn = 0, vyn = 0, uzn = 0, vzn = 0;
        int bh = 0, bmt = 0, mbi = 0, sau = 0, col = 0, allv = 0, uvr = 0;
        int xt = 0, yt = 0, zt = 0, w = 0, h = 0, l = 0, ftl = 0, ftw = 0;
        int n = std::sscanf(p,
            "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
            &idx, &sIndex,
            &uxp, &vxp, &uyp, &vyp, &uzp, &vzp, &uxn, &vxn, &uyn, &vyn, &uzn, &vzn,
            &bh, &bmt, &mbi, &sau, &col, &allv, &uvr, &xt, &yt, &zt, &w, &h, &l, &ftl, &ftw);
        if (n < 29 || idx < 0 || idx >= (int)materials.size()) continue;

        Material& m = materials[idx];
        m.initDefault(idx);

        // --- BMT / BH gating (exact control flow from decomp) ---
        // if ((BMT < 2) || (BMT == 4)) {
        //   if (BMT == 0) goto clamp_BH;
        //   else BH=0, d8=0;          // BMT 1 or 4
        // } else {
        //   if (BMT in {2,3,5,6,7,8,9,10}) BH=0, d8=0;
        //   else { BMT = 0; goto clamp_BH; }
        // }
        // clamp_BH: if BH in [1,16] d8=BH; else BH=d8=0x10
        uint8_t d8 = 0;
        if ((bmt < 2) || (bmt == 4)) {
            if (bmt == 0) {
                if ((unsigned)(bh - 1) < 0x10u) {
                    d8 = (uint8_t)bh;
                } else {
                    bh = 0x10;
                    d8 = 0x10;
                }
            } else {
                // BMT == 1 or 4
                bh = 0;
                d8 = 0;
            }
        } else if (bmt == 2 || bmt == 3 || bmt == 5 || bmt == 6 || bmt == 7 ||
                   bmt == 8 || bmt == 9 || bmt == 10) {
            bh = 0;
            d8 = 0;
        } else {
            // invalid BMT → force solid class 0 + clamp BH
            bmt = 0;
            if ((unsigned)(bh - 1) < 0x10u) {
                d8 = (uint8_t)bh;
            } else {
                bh = 0x10;
                d8 = 0x10;
            }
        }

        // MBI clamp 0..6
        if ((unsigned)mbi >= 7u) mbi = 0;

        m.raw[0x00] = d8;
        m.raw[0x01] = (uint8_t)bmt;           // class
        m.raw[0x02] = (uint8_t)mbi;
        m.raw[0x03] = 0;                      // may be filled by post-process
        m.raw[0x05] = (sau != 0) ? 1 : 0;
        m.raw[0x06] = (col != 0) ? 1 : 0;
        m.raw[0x07] = (allv != 0) ? 1 : 0;
        m.raw[0x08] = (uint8_t)(uvr & 0xff);
        m.raw[0x09] = (uint8_t)(xt & 0xff);
        m.raw[0x0a] = (uint8_t)(yt & 0xff);
        m.raw[0x0b] = (uint8_t)(zt & 0xff);

        // UV: XP YP ZP XN YN ZN
        m.raw[0x0c] = (uint8_t)(uxp & 0xff);
        m.raw[0x0d] = (uint8_t)(uyp & 0xff);
        m.raw[0x0e] = (uint8_t)(uzp & 0xff);
        m.raw[0x0f] = (uint8_t)(uxn & 0xff);
        m.raw[0x10] = (uint8_t)(uyn & 0xff);
        m.raw[0x11] = (uint8_t)(uzn & 0xff);
        m.raw[0x12] = (uint8_t)(vxp & 0xff);
        m.raw[0x13] = (uint8_t)(vyp & 0xff);
        m.raw[0x14] = (uint8_t)(vzp & 0xff);
        m.raw[0x15] = (uint8_t)(vxn & 0xff);
        m.raw[0x16] = (uint8_t)(vyn & 0xff);
        m.raw[0x17] = (uint8_t)(vzn & 0xff);

        // W/H clamp 0..16
        if (w < 0) w = 0; else if (w > 0x10) w = 0x10;
        if (h < 0) h = 0; else if (h > 0x10) h = 0x10;
        m.raw[0x18] = (uint8_t)w;
        m.raw[0x19] = (uint8_t)h;
        m.raw[0x1a] = (uint8_t)(l & 0xff);
        m.raw[0x1b] = (uint8_t)(ftl & 0xff);  // FTL — FlowXP / default
        m.raw[0x1c] = (uint8_t)(ftw & 0xff);  // FTW — FlowXN
        // Expand into Y/Z slots so IsFlowAllowed(FlowYN/YP/ZP) sees non-zero
        // (blocks.txt only has two columns; +0x20 is S-Index — leave untouched)
        {
            uint8_t f = m.raw[0x1b] ? m.raw[0x1b] : m.raw[0x1c];
            m.raw[0x1d] = f; // FlowYP
            m.raw[0x1e] = f; // FlowYN  — vertical fall
            m.raw[0x1f] = f; // FlowZP
        }

        // S-Index → +0x20; out of range → 0xffff
        if (sIndex < 0 || sIndex >= (int)materials.size()) {
            m.raw[0x20] = 0xff;
            m.raw[0x21] = 0xff;
        } else {
            int16_t si = (int16_t)sIndex;
            m.raw[0x20] = (uint8_t)(si & 0xff);
            m.raw[0x21] = (uint8_t)((si >> 8) & 0xff);
            if (sIndex + 1 > maxSel) maxSel = sIndex + 1;
        }

        // Post-process by class (+0x01) — drives waterBase / companion
        uint8_t cls = m.raw[0x01];
        if (cls == 0) {
            uint8_t ht = m.raw[0x00];
            if (ht < 0x0e) {
                m.raw[0x03] = ht;
                m.raw[0x04] = (uint8_t)(((0x10 - (unsigned)ht) >> 1) - 1);
            }
        } else if (cls == 6) {
            m.raw[0x03] = 8;
            m.raw[0x04] = 3;
        } else if (cls == 10) {
            m.raw[0x03] = 2;
            m.raw[0x04] = 6;
        }

        ++loaded;
    }
    maxSelIndex_ = maxSel > 0 ? maxSel : 0;
    return loaded > 0;
}

const Material* GridData::GetMaterial(int type) const {
    if (type < 0 || type >= (int)materials.size()) return nullptr;
    return &materials[type];
}

// ---------------------------------------------------------------------------
// IsFlowAllowed* recovery (all six directions share the same core):
//
//   mat = materials[types[cellIndex]]          // +0xe8 stride 34
//   perm = mat[+0x1b + FlowType]               // direction-specific permission byte
//
//   if (perm & 0x01) {
//     if (perm & 0x08) { /* special prop01 paths (0,6,7,0x0a) → 0 or 0x0f */ }
//     else return 0x0f;                        // fully allowed
//   } else {
//     if (perm & 0x02) return 0;               // Y axes: inverted logic via sbb/not
//     if (perm & 0x04) {
//       // XP/XN/ZP/ZN: return (state&3)==3 ? 0x0f : 0
//       // YP/YN: different — if !(perm&2) result via sbb
//     }
//     return 0;
//   }
//
// Without the binary material table, we approximate permission from type:
//   air(0)/water(5)/glass(11)/leaves/torch → allow (0x0f)
//   solid cubes → block (0)
// Full material+0x1b table load remains open.
// ---------------------------------------------------------------------------
// IsFlowAllowedXP @ 0x851a0 (pdg) — canonical; other dirs share structure.
// perm = materials[type].raw[0x1b + FlowType]
// Returns 0 or 0x0f (and rare intermediate values for specials).
// CONFIRMED from rz-ghidra pdg 2026-11-01.
int GridData::IsFlowAllowed(int cellIndex, FlowType flow) const {
    if (cellIndex < 0 || cellIndex >= (int)cellTypes.size()) return 0;
    unsigned char t = cellTypes[cellIndex];
    if (!materials.empty() && t < materials.size()) {
        const Material& mat = materials[t];
        uint8_t perm = mat.flowPerm((int)flow);
        uint32_t result = 0x0f;
        if ((perm & 1) == 0) {
            // not bit0: check bit1 / bit2 + state
            if ((perm & 2) == 0) {
                result = 0;
                if ((perm & 4) != 0) {
                    if (cellIndex < (int)cellStates.size() &&
                        (cellStates[cellIndex] & 3) == 3) {
                        result = 0x0f;
                    }
                }
            } else {
                result = 0; // bit1 set → blocked (matches Y-axis inverted path)
            }
        } else if ((perm & 8) != 0) {
            // special prop01 paths (pdg):
            uint8_t p01 = mat.prop01();
            unsigned short st = (cellIndex < (int)cellStates.size())
                                    ? cellStates[cellIndex] : 0;
            if (p01 == 0) {
                // if raw[0] > 7 → complex: (-(state&4==0) & ~7) + 0xc
                if (mat.raw[0] > 7) {
                    result = ((st & 4) == 0) ? (0xfffffff7u + 0xc) : 0xc;
                    // simplifies to 4 or 0xc depending on bit; keep 0x0f/0 heuristic
                    result = ((st & 4) == 0) ? 0x04 : 0x0c;
                }
            } else if (p01 == 6) {
                // table lookup (state & 7) — table address unknown; allow full
                result = 0x0f;
            } else if (p01 == 7) {
                // table lookup (state & 0xf)
                result = 0x0f;
            } else if (p01 == 10) {
                if ((st & 8) != 0) {
                    // table (state & 3)
                    result = 0x0f;
                } else {
                    result = 0;
                }
            } else {
                result = 0;
            }
        }
        // else (bit0 set, bit3 clear) → 0x0f (already set)
        return (int)result;
    }
    // Fallback when material table not loaded (air / glass / plants pass flow)
    if (t == 0 || t == 11) return 0x0f;
    if (t == 10 || t == 20 || t == 17 || t == 18) return 0x0f;
    if (t == 12 || t == 13 || t == 14) return 0x0f;
    return 0;
}

// Material::MinWaterHeight @ 0x86b70 / MaxWaterHeight @ 0x86c70
// matProp03 = material byte +0x03 (base height)
// matProp01 = material byte +0x01 (class; 0x0a = special liquid)
// state bits: 0x04, 0x0c used for height variants
// ---------------------------------------------------------------------------
// Material::MinWaterHeight @ 0x86b70 — CONFIRMED from rz-ghidra pdg
int GridData::MinWaterHeight(unsigned char matProp01, unsigned char matProp03, unsigned short state) {
    if (matProp03 == 0) return 0;
    if (matProp01 == 0x0a) {
        // return -((state & 0xc) == 0) & 2;  → 2 if (state&0xc)==0 else 0
        return ((state & 0x0c) == 0) ? 2 : 0;
    }
    if (state & 0x04) return 0;
    return (int)matProp03;
}

// Material::MaxWaterHeight @ 0x86c70 — CONFIRMED from rz-ghidra pdg
int GridData::MaxWaterHeight(unsigned char matProp01, unsigned char matProp03, unsigned short state) {
    if (matProp03 == 0) return 0;
    if (matProp01 == 0x0a) {
        // (-((state&0xc)==0) & 2) + 0xe  → 0x10 or 0x0e
        return ((state & 0x0c) == 0) ? 0x10 : 0x0e;
    }
    // default 0x10; if (state & 4) then 0x10 - prop03
    if (state & 0x04) return 0x10 - (int)matProp03;
    return 0x10;
}

// ---------------------------------------------------------------------------
// UpdateWater — recovers UpdateWaterState @ 0xa1040 + SpreadWater @ 0x9f560
// + UpdateOneWaterState @ 0x9d390.
//
// CONFIRMED state encoding (from SpreadWater / UpdateOneWaterState disasm):
//   bits [10:12]  0x1c00  water level 0..7   (0 = source/full, 7 = nearly dry)
//   bit  0x2000           fluid-active flag
//   bit  0x4000           hierarchy / "needs process" (SpreadWater marks this)
//   bits 0x3fc0           fluid-related mask (empty test: (st&0x3fc0)==0)
//
// CONFIRMED SpreadWater receive gate:
//   materials[neighType].raw[0x1c] != 0
//   AND ( (neighState&0x3fc0)==0 && srcLevel < 7
//         OR (neighState&0x2000 && srcLevel < neighLevel) )
//
// CONFIRMED flow order in UpdateWaterState:
//   budget gate (+0x2d4/+0x2d8/+0x2dc) → iterate cells → UpdateOneWaterState
//   → SetCell / SpreadWater|SpreadLava for dirty results.
//
// Level model (Minecraft-classic style, matching 0..7 encoding):
//   Source placed by player: type=Water, level=0, fluid bit set.
//   Fall into empty below: always preferred; destination gets same level.
//   Horizontal: destination level = srcLevel+1 (if < 8); only into air or
//   higher-level fluid. Level 7 does not spread horizontally.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Liquid pipeline recovered from Ghidra:
//   UpdateWaterState @ 0xa1040 → UpdateOneWaterState → queue → Spread*/SetCell
//   SpreadWater @ 0x9f560 / SpreadLava — mark receive gates only (0x4000) + we
//   also enqueue SetCell for empty/higher so liquid advances in the same tick
//   (binary relies on dirty hierarchy revisiting cells; we flush equivalently).
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Liquid pipeline (Ghidra UpdateWaterState / UpdateOneWaterState / Spread*).
// Fix 2026-11-20: previous version exhausted maxSteps on Spread* so SetCell
// never flushed → no visible flow; full-volume scan + every source enqueued
// Spread every frame → multi-second hitches.
// ---------------------------------------------------------------------------

void GridData::QueueLiquidCmd(int x, int y, int z, unsigned char type, unsigned short state) {
    LiquidCmd c;
    c.x = x; c.y = y; c.z = z;
    c.type = type;
    c.state = state;
    liquidQueue_.push_back(c);
}

void GridData::FlushLiquidQueue(int& stepsLeft) {
    // PRIORITY: apply all SetCell first, then Spread*. Otherwise budget dies on
    // Spread and liquid never materializes (observed: no flow + lag).
    std::vector<LiquidCmd> spreads;
    spreads.reserve(liquidQueue_.size());
    size_t i = 0;
    while (i < liquidQueue_.size() && stepsLeft > 0) {
        LiquidCmd c = liquidQueue_[i++];
        if (c.type == 0xFE || c.type == 0xFF) {
            spreads.push_back(c);
            continue;
        }
        int idx = CoordToIndex(c.x, c.y, c.z);
        if (idx >= 0) {
            unsigned char ot = cellTypes[idx];
            unsigned short os = cellStates[idx];
            if (ot != c.type || ((os ^ c.state) & 0x3fff) != 0) {
                SetCell(c.x, c.y, c.z, c.type, c.state);
                --stepsLeft;
            }
        }
    }
    // leftover unprocessed stay; clear processed+spreads handling
    liquidQueue_.clear();
    for (size_t s = 0; s < spreads.size() && stepsLeft > 0; ++s) {
        const LiquidCmd& c = spreads[s];
        if (c.type == 0xFE)
            SpreadWater(c.x, c.y, c.z, (int)c.state);
        else
            SpreadLava(c.x, c.y, c.z, (int)c.state);
        --stepsLeft;
    }
    // SetCells enqueued by Spread* — apply immediately within remaining budget
    if (!liquidQueue_.empty() && stepsLeft > 0) {
        std::vector<LiquidCmd> nested = std::move(liquidQueue_);
        liquidQueue_.clear();
        for (size_t n = 0; n < nested.size() && stepsLeft > 0; ++n) {
            const LiquidCmd& c = nested[n];
            if (c.type == 0xFE || c.type == 0xFF) {
                liquidQueue_.push_back(c); // defer further spreads to next tick
                continue;
            }
            int idx = CoordToIndex(c.x, c.y, c.z);
            if (idx < 0) continue;
            unsigned char ot = cellTypes[idx];
            unsigned short os = cellStates[idx];
            if (ot != c.type || ((os ^ c.state) & 0x3fff) != 0) {
                SetCell(c.x, c.y, c.z, c.type, c.state);
                --stepsLeft;
            }
        }
    }
}

bool GridData::WaterThresholdTest(int fromIdx, int toIdx, int levelHint) const {
    if (fromIdx < 0 || toIdx < 0) return true;
    if (fromIdx >= (int)cellTypes.size() || toIdx >= (int)cellTypes.size()) return true;
    if (materials.empty()) return true;
    unsigned char tTo = cellTypes[toIdx];
    unsigned char tFrom = cellTypes[fromIdx];
    if (tTo >= materials.size() || tFrom >= materials.size()) return true;
    const Material& mTo = materials[tTo];
    const Material& mFrom = materials[tFrom];
    int prop04 = (int)mTo.raw[4];
    if (prop04 < levelHint) {
        unsigned short stTo = cellStates[toIdx];
        unsigned short stFrom = cellStates[fromIdx];
        int hTo = MinWaterHeight(mTo.raw[1], mTo.raw[3], stTo);
        int hFrom = MinWaterHeight(mFrom.raw[1], mFrom.raw[3], stFrom);
        return hTo <= hFrom;
    }
    return true;
}

unsigned short GridData::FindFlow(int x, int y, int z, int cellIdx, int maxDepth) {
    (void)cellIdx;
    unsigned short bits = 0;
    if (maxDepth <= 0) return 0;
    auto lvl = [&](int idx) -> int {
        if (idx < 0 || idx >= (int)cellStates.size()) return 8;
        if ((cellStates[idx] & STATE_BIT_2000) == 0) return 8;
        return LiquidLevel(cellStates[idx]);
    };
    int self = CoordToIndex(x, y, z);
    int selfL = lvl(self);
    struct { int dx, dz; unsigned short bit; } dirs[4] = {
        {1,0,0x1}, {-1,0,0x2}, {0,1,0x4}, {0,-1,0x8}
    };
    for (auto& d : dirs) {
        int ni = CoordToIndex(x + d.dx, y, z + d.dz);
        if (ni < 0) continue;
        int nl = lvl(ni);
        bool empty = cellTypes[ni] == 0 && (cellStates[ni] & STATE_LIQUID_ANY) == 0;
        if (empty || nl > selfL) bits = (unsigned short)(bits | d.bit);
    }
    int below = CoordToIndex(x, y - 1, z);
    if (below >= 0) {
        bool empty = cellTypes[below] == 0 && (cellStates[below] & STATE_LIQUID_ANY) == 0;
        if (empty || lvl(below) > selfL) bits = (unsigned short)(bits | 0x10);
    }
    return bits;
}

int GridData::GetWaterBlockHeight(int x, int y, int z, int dx, int dz) const {
    int sx = x + dx, sz = z + dz;
    int idx = CoordToIndex(sx, y, sz);
    if (idx < 0) return 0;
    unsigned short st = cellStates[idx];
    if ((st & STATE_BIT_2000) == 0 && (st & STATE_BIT_3C0) == 0) return 0;
    int above = CoordToIndex(sx, y + 1, sz);
    if (above >= 0) {
        unsigned short ast = cellStates[above];
        bool same = ((st & STATE_BIT_2000) && (ast & STATE_BIT_2000)) ||
                    (((st & STATE_BIT_3C0) && !(st & STATE_BIT_2000)) &&
                     ((ast & STATE_BIT_3C0) && !(ast & STATE_BIT_2000)));
        if (same && IsFlowAllowed(idx, FlowYP) && IsFlowAllowed(above, FlowYN))
            return 0x80;
    }
    int level = LiquidLevel(st);
    int units = (level == 0) ? 0x0e : (0x0f - 2 * level);
    if (units < 1) units = 1;
    if (units > 16) units = 16;
    if (!materials.empty() && idx < (int)cellTypes.size()) {
        unsigned char ty = cellTypes[idx];
        if (ty < materials.size()) {
            int mh = MinWaterHeight(materials[ty].raw[1], materials[ty].raw[3], st);
            if (units < mh) units = mh;
        }
    }
    return units;
}

void GridData::SpreadWater(int x, int y, int z, int srcLevel) {
    // Ghidra SpreadWater @ 0x9f560 — ONLY marks receive cells with 0x4000
    // (writes aux+0x258 hierarchy, not SetCell). Placement is next tick.
    //
    // Horizontal: mat[+0x1c]!=0 && ((empty && srcLevel<7) || (water && srcLevel<neighLevel))
    // Vertical Y- (exact from decomp):
    //   if (y > 0) && mat[below][+0x1c]!=0:
    //     mark if (st & 0x3fc0)==0  OR  ((st & 0x2000) && (st & 0x1c00) > 0x400)
    //   → empty OR water-with-level>=2; NO srcLevel<7 gate (level 7 still falls).
    int self = CoordToIndex(x, y, z);
    if (self < 0) return;
    static const int Hoff[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    auto flowPermOk = [&](int idx) -> bool {
        if (idx < 0 || idx >= (int)cellTypes.size()) return false;
        unsigned char t = cellTypes[idx];
        if (!materials.empty() && t < materials.size())
            return materials[t].raw[0x1c] != 0;
        // No material table: allow air/water cells
        return t == 0 || (cellStates[idx] & STATE_BIT_2000) != 0;
    };
    auto markDirty = [&](int ni) {
        if (ni < 0) return;
        unsigned short ns = cellStates[ni];
        if ((ns & STATE_BIT_4000) == 0)
            SetState(ni, (unsigned short)(ns | STATE_BIT_4000));
    };

    // Horizontal — binary empty gate uses param_4 < 7
    if (srcLevel < 7) {
        for (int d = 0; d < 4; ++d) {
            int ni = CoordToIndex(x + Hoff[d][0], y, z + Hoff[d][1]);
            if (!flowPermOk(ni)) continue;
            unsigned short ns = cellStates[ni];
            bool empty = (ns & STATE_LIQUID_ANY) == 0 && cellTypes[ni] == 0;
            bool waterHigher = (ns & STATE_BIT_2000) != 0 && srcLevel < LiquidLevel(ns);
            if (empty || waterHigher)
                markDirty(ni);
        }
    }

    // Vertical fall (Y-) — Ghidra exact receive test (no srcLevel gate)
    if (y > originY) {
        int below = CoordToIndex(x, y - 1, z);
        if (flowPermOk(below)) {
            unsigned short ns = cellStates[below];
            bool empty = (ns & STATE_LIQUID_ANY) == 0; // (st & 0x3fc0)==0
            // water && (st & 0x1c00) > 0x400  → level >= 2
            bool waterDeep = (ns & STATE_BIT_2000) != 0 && (ns & STATE_LEVEL_MASK) > 0x400;
            if (empty || waterDeep)
                markDirty(below);
        }
    }
}

void GridData::SpreadLava(int x, int y, int z, int srcLevel) {
    // Same contract as SpreadWater: mark 0x4000 only; no SetCell here.
    int self = CoordToIndex(x, y, z);
    if (self < 0) return;
    static const int Hoff[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    auto canMat = [&](int idx) -> bool {
        if (idx < 0 || idx >= (int)cellTypes.size()) return false;
        unsigned char t = cellTypes[idx];
        if (!materials.empty() && t < materials.size()) {
            if (materials[t].raw[0x1c] == 0 && materials[t].raw[0x1b] == 0)
                return false;
        }
        return true;
    };
    auto isLava = [&](unsigned short s) {
        return (s & STATE_BIT_3C0) != 0 && (s & STATE_BIT_2000) == 0;
    };
    auto markDirty = [&](int ni) {
        if (ni < 0) return;
        unsigned short ns = cellStates[ni];
        if ((ns & STATE_BIT_4000) == 0)
            SetState(ni, (unsigned short)(ns | STATE_BIT_4000));
    };

    if (srcLevel < 6) {
        for (int d = 0; d < 4; ++d) {
            int ni = CoordToIndex(x + Hoff[d][0], y, z + Hoff[d][1]);
            if (!canMat(ni)) continue;
            unsigned short ns = cellStates[ni];
            unsigned char nt = cellTypes[ni];
            bool empty = (ns & STATE_LIQUID_ANY) == 0 && nt == 0;
            bool lavaHigher = isLava(ns) && srcLevel < LiquidLevel(ns);
            if (empty || lavaHigher)
                markDirty(ni);
        }
    }
    int below = CoordToIndex(x, y - 1, z);
    if (below >= 0 && canMat(below)) {
        unsigned short ns = cellStates[below];
        unsigned char nt = cellTypes[below];
        bool empty = (ns & STATE_LIQUID_ANY) == 0 && nt == 0;
        bool lavaHigher = isLava(ns) && srcLevel < LiquidLevel(ns);
        if (empty || lavaHigher)
            markDirty(below);
    }
}

void GridData::UpdateOneWaterState(int x, int y, int z) {
    int idx = CoordToIndex(x, y, z);
    if (idx < 0) return;
    unsigned char type = cellTypes[idx];
    unsigned short st = cellStates[idx];
    if (!materials.empty() && type < materials.size()) {
        if (materials[type].raw[0x1c] == 0 && materials[type].raw[0x1b] == 0) {
            if (st & STATE_BIT_4000)
                SetState(idx, (unsigned short)(st & ~STATE_BIT_4000));
            return;
        }
    }

    const bool isWater = (st & STATE_BIT_2000) != 0;
    const bool isLava  = (st & STATE_BIT_3C0) != 0 && !isWater;
    const bool isEmpty = !isWater && !isLava && type == 0;

    static const int Hoff[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};

    auto neighWaterLevel = [&](int ni) -> int {
        if (ni < 0 || ni >= (int)cellStates.size()) return 8;
        if ((cellStates[ni] & STATE_BIT_2000) == 0) return 8;
        return LiquidLevel(cellStates[ni]);
    };
    auto neighLavaLevel = [&](int ni) -> int {
        if (ni < 0 || ni >= (int)cellStates.size()) return 8;
        unsigned short s = cellStates[ni];
        if ((s & STATE_BIT_3C0) == 0 || (s & STATE_BIT_2000) != 0) return 8;
        return LiquidLevel(s);
    };
    auto isSolid = [&](int ni) -> bool {
        if (ni < 0) return false;
        if (ni < (int)cellStates.size()) {
            unsigned short s = cellStates[ni];
            if ((s & STATE_BIT_2000) || ((s & STATE_BIT_3C0) && !(s & STATE_BIT_2000)))
                return false;
        }
        return cellTypes[ni] != 0;
    };
    auto hasEmptyNeighbor = [&]() -> bool {
        static const int n6[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (auto& o : n6) {
            int ni = CoordToIndex(x+o[0], y+o[1], z+o[2]);
            if (ni < 0) continue;
            if (cellTypes[ni] == 0 && (cellStates[ni] & STATE_LIQUID_ANY) == 0)
                return true;
            // also spread into higher-level same liquid
            if (isWater && (cellStates[ni] & STATE_BIT_2000) &&
                LiquidLevel(st) < LiquidLevel(cellStates[ni]))
                return true;
            if (isLava && (cellStates[ni] & STATE_BIT_3C0) && !(cellStates[ni] & STATE_BIT_2000) &&
                LiquidLevel(st) < LiquidLevel(cellStates[ni]))
                return true;
        }
        return false;
    };

    int below = CoordToIndex(x, y - 1, z);
    int above = CoordToIndex(x, y + 1, z);
    bool waterAbove = above >= 0 && (cellStates[above] & STATE_BIT_2000) != 0;
    bool lavaAbove = above >= 0 && (cellStates[above] & STATE_BIT_3C0) != 0 &&
                     (cellStates[above] & STATE_BIT_2000) == 0;

    if (isWater) {
        int cur = LiquidLevel(st);
        SetState(idx, (unsigned short)(st & ~STATE_BIT_4000));
        if (cur == 0) {
            // Source: only spread when there is somewhere to go (shore / fall)
            if (hasEmptyNeighbor())
                QueueLiquidCmd(x, y, z, 0xFE, 0);
            return;
        }
        int minAdj = 8, sourceNeigh = 0;
        for (int d = 0; d < 4; ++d) {
            int ni = CoordToIndex(x + Hoff[d][0], y, z + Hoff[d][1]);
            int nl = neighWaterLevel(ni);
            if (nl >= 8) continue;
            if (nl < minAdj) minAdj = nl;
            if (nl == 0) ++sourceNeigh;
        }
        bool supportOk = isSolid(below) || (neighWaterLevel(below) == 0);
        int newLevel;
        if (sourceNeigh >= 2 && supportOk) {
            newLevel = 0;
        } else if (waterAbove) {
            newLevel = cur;
            if (minAdj < 8 && minAdj + 1 < newLevel) newLevel = minAdj + 1;
            if (isSolid(below) && newLevel == 0) newLevel = 1;
        } else if (minAdj < 8) {
            newLevel = minAdj + 1;
            if (newLevel > 7) newLevel = 8;
        } else {
            newLevel = 8;
        }
        if (newLevel >= 8) {
            QueueLiquidCmd(x, y, z, 0, 0);
            return;
        }
        if (newLevel != cur)
            QueueLiquidCmd(x, y, z, 0,
                (unsigned short)(MakeWaterState(newLevel) | STATE_BIT_4000));
        // Spread even at level 7 — vertical has no srcLevel gate in Ghidra
        if (newLevel <= 7 && hasEmptyNeighbor())
            QueueLiquidCmd(x, y, z, 0xFE, (unsigned short)newLevel);
        return;
    }

    if (isLava) {
        int cur = LiquidLevel(st);
        SetState(idx, (unsigned short)(st & ~STATE_BIT_4000));
        if (cur == 0) {
            if (hasEmptyNeighbor())
                QueueLiquidCmd(x, y, z, 0xFF, 0);
            return;
        }
        int minAdj = 8, sourceNeigh = 0;
        for (int d = 0; d < 4; ++d) {
            int ni = CoordToIndex(x + Hoff[d][0], y, z + Hoff[d][1]);
            int nl = neighLavaLevel(ni);
            if (nl >= 8) continue;
            if (nl < minAdj) minAdj = nl;
            if (nl == 0) ++sourceNeigh;
        }
        bool supportOk = isSolid(below) || (neighLavaLevel(below) == 0);
        int newLevel;
        if (sourceNeigh >= 2 && supportOk) newLevel = 0;
        else if (lavaAbove) {
            newLevel = cur;
            if (minAdj < 8 && minAdj + 1 < newLevel) newLevel = minAdj + 1;
            if (isSolid(below) && newLevel == 0) newLevel = 1;
        } else if (minAdj < 8) {
            newLevel = minAdj + 1;
            if (newLevel > 7) newLevel = 8;
        } else newLevel = 8;
        auto makeLava = [](int level) -> unsigned short {
            if (level < 0) level = 0;
            if (level > 7) level = 7;
            return (unsigned short)(STATE_BIT_3C0 | 0xC000 | ((level & 7) << STATE_LEVEL_SHIFT));
        };
        if (newLevel >= 8) {
            QueueLiquidCmd(x, y, z, 0, 0);
            return;
        }
        if (newLevel != cur)
            QueueLiquidCmd(x, y, z, 0, makeLava(newLevel));
        if (newLevel <= 7 && hasEmptyNeighbor())
            QueueLiquidCmd(x, y, z, 0xFF, (unsigned short)newLevel);
        return;
    }

    // Empty dirty cell: fill from adjacent liquid.
    // Vertical fall (water above): same level as source (binary SpreadWater
    // marks below; UpdateOneWaterState places). Horizontal: level = minAdj+1.
    if (isEmpty && (st & STATE_BIT_4000)) {
        int minW = 8, minL = 8;
        for (int d = 0; d < 4; ++d) {
            int ni = CoordToIndex(x + Hoff[d][0], y, z + Hoff[d][1]);
            int nw = neighWaterLevel(ni);
            int nl = neighLavaLevel(ni);
            if (nw < minW) minW = nw;
            if (nl < minL) minL = nl;
        }
        SetState(idx, (unsigned short)(st & ~STATE_BIT_4000));
        // Keep 0x4000 on newly placed liquid so the next UpdateWaterState tick
        // runs UpdateOneWaterState → Spread* (binary relies on neighbour dirty
        // / same-cell re-entry; without it flow stops after one cell).
        if (waterAbove) {
            int place = LiquidLevel(cellStates[above]);
            if (place > 7) place = 7;
            // Fall keeps level; landing on solid: 0 → 1
            if (isSolid(below) && place == 0) place = 1;
            QueueLiquidCmd(x, y, z, 0,
                (unsigned short)(MakeWaterState(place) | STATE_BIT_4000));
        } else if (minW < 7) {
            int place = minW + 1;
            if (place <= 7)
                QueueLiquidCmd(x, y, z, 0,
                    (unsigned short)(MakeWaterState(place) | STATE_BIT_4000));
        } else if (lavaAbove) {
            int place = LiquidLevel(cellStates[above]);
            if (isSolid(below) && place == 0) place = 1;
            if (place <= 7) {
                unsigned short ls = (unsigned short)(STATE_BIT_3C0 | STATE_BIT_4000 |
                    ((place & 7) << STATE_LEVEL_SHIFT));
                QueueLiquidCmd(x, y, z, 0, ls);
            }
        } else if (minL < 6) {
            int place = minL + 1;
            if (place <= 7) {
                unsigned short ls = (unsigned short)(STATE_BIT_3C0 | STATE_BIT_4000 |
                    ((place & 7) << STATE_LEVEL_SHIFT));
                QueueLiquidCmd(x, y, z, 0, ls);
            }
        }
    }
}

void GridData::TickWater(int deltaMs, int maxSteps) {
    // Binary UpdateWaterState @ 0xa1040 entry (pdf):
    //   cmp [this+0x2d8], now; setle → +0x2d0; jz return (not due)
    //   period = [this+0x2d4]; [this+0x2d8] = now + period
    //   if now >= [this+0x2dc]: [this+0x2dc] = now + period*8
    if (deltaMs < 0) deltaMs = 0;
    if (deltaMs > 200) deltaMs = 200; // Scene MaxTimestep = 200
    waterClockMs_ += deltaMs;
    const int now = waterClockMs_;
    waterWasDue_ = (now >= waterDeadline_) ? 1 : 0;
    if (!waterWasDue_)
        return;
    waterDeadline_ = now + waterPeriod_; // +0x2d4 default 200
    if (now >= waterLongDeadline_)
        waterLongDeadline_ = now + waterPeriod_ * 8;
    UpdateWater(maxSteps);
}

void GridData::UpdateWater(int maxSteps) {
    // Recovered from Ghidra GridData::UpdateWaterState @ 0xa1040:
    //   1) budget gate (+0x2d4/+0x2d8/+0x2dc) — applied in TickWater
    //   2) walk hierarchical occupancy of bit 0x4000
    //      maps at +0x260 (>>16), +0x268 (>>12), +0x270 (>>8), +0x278 (>>4)
    //   3) for each dirty group, test cellStates[i] & 0x4000
    //   4) UpdateOneWaterState → queue → SpreadWater/SpreadLava/SetCell
    // NEVER scans the full cellStates volume (that caused the 1 Hz hitch).
    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0) return;
    if (cellStates.empty() || aux4000L0_.empty()) return;
    if (maxSteps <= 0) maxSteps = 200; // binary default +0x2d4
    int stepsLeft = maxSteps;
    liquidQueue_.clear();

    const int total = (int)cellStates.size();
    const int nL3 = (int)aux4000L3_.size();
    const int nL2 = (int)aux4000L2_.size();
    const int nL1 = (int)aux4000L1_.size();
    const int nL0 = (int)aux4000L0_.size();

    // Fast path: top hierarchy empty → nothing dirty.
    bool anyDirty = false;
    for (int i = 0; i < nL3; ++i) {
        if (aux4000L3_[i]) { anyDirty = true; break; }
    }
    if (!anyDirty) {
        // Hierarchy should be authoritative; if L3 is empty, L0 must be empty
        // when SetState is used for all 0x4000 toggles. Still cheap to bail.
        return;
    }

    // Process at most maxSteps dirty cells per call (binary budget +0x2d4).
    int processed = 0;
    for (int i3 = 0; i3 < nL3 && processed < maxSteps; ++i3) {
        if (!aux4000L3_[i3]) continue;
        const int i2Begin = i3 << 4;
        const int i2End = std::min(i2Begin + 16, nL2);
        for (int i2 = i2Begin; i2 < i2End && processed < maxSteps; ++i2) {
            if (!aux4000L2_[i2]) continue;
            const int i1Begin = i2 << 4;
            const int i1End = std::min(i1Begin + 16, nL1);
            for (int i1 = i1Begin; i1 < i1End && processed < maxSteps; ++i1) {
                if (!aux4000L1_[i1]) continue;
                const int i0Begin = i1 << 4;
                const int i0End = std::min(i0Begin + 16, nL0);
                for (int i0 = i0Begin; i0 < i0End && processed < maxSteps; ++i0) {
                    if (!aux4000L0_[i0]) continue;
                    const int cellBegin = i0 << 4;
                    const int cellEnd = std::min(cellBegin + 16, total);
                    for (int idx = cellBegin; idx < cellEnd && processed < maxSteps; ++idx) {
                        if ((cellStates[idx] & STATE_BIT_4000) == 0) continue;
                        int lx = idx % sizeX;
                        int t = idx / sizeX;
                        int ly = t % sizeY;
                        int lz = t / sizeY;
                        // Binary UpdateWaterState calls UpdateOneWaterState which
                        // immediately SetCell / SpreadWater (no deferred global queue).
                        // Flush after each cell so side-effects apply same tick.
                        UpdateOneWaterState(originX + lx, originY + ly, originZ + lz);
                        if (!liquidQueue_.empty() && stepsLeft > 0)
                            FlushLiquidQueue(stepsLeft);
                        ++processed;
                        if (stepsLeft > 0) --stepsLeft;
                    }
                }
            }
        }
    }

    // Drain any remaining queued spreads within budget
    int waves = 0;
    while (!liquidQueue_.empty() && stepsLeft > 0 && waves < 4) {
        FlushLiquidQueue(stepsLeft);
        ++waves;
    }
}








// Save one 16×full-height×16 column strip keyed by EncodeWorldPos
// Original SaveChunk @ 0xa25d0 — writes region key + cell payload
void GridData::SaveChunk() {
    // no-op without active stream; use SaveWorld for full dump
}

bool GridData::SaveWorld(const char* path) {
    FILE* fp = std::fopen(path, "wb");
    if (!fp) return false;
    // Simple portable format (documented; not byte-identical to original):
    // magic "ELW1", origin, size, then type[] then state[]
    const char magic[4] = {'E','L','W','1'};
    std::fwrite(magic, 1, 4, fp);
    int hdr[6] = { originX, originY, originZ, sizeX, sizeY, sizeZ };
    std::fwrite(hdr, sizeof(int), 6, fp);
    if (!cellTypes.empty())
        std::fwrite(cellTypes.data(), 1, cellTypes.size(), fp);
    if (!cellStates.empty())
        std::fwrite(cellStates.data(), sizeof(unsigned short), cellStates.size(), fp);
    std::fclose(fp);
    return true;
}

bool GridData::LoadWorld(const char* path) {
    FILE* fp = std::fopen(path, "rb");
    if (!fp) return false;
    char magic[4];
    if (std::fread(magic, 1, 4, fp) != 4 || magic[0]!='E' || magic[1]!='L' || magic[2]!='W' || magic[3]!='1') {
        std::fclose(fp); return false;
    }
    int hdr[6];
    if (std::fread(hdr, sizeof(int), 6, fp) != 6) { std::fclose(fp); return false; }
    originX = hdr[0]; originY = hdr[1]; originZ = hdr[2];
    sizeX = hdr[3]; sizeY = hdr[4]; sizeZ = hdr[5];
    size_t n = (size_t)sizeX * sizeY * sizeZ;
    cellTypes.assign(n, 0);
    cellStates.assign(n, 0);
    if (std::fread(cellTypes.data(), 1, n, fp) != n) { std::fclose(fp); return false; }
    std::fread(cellStates.data(), sizeof(unsigned short), n, fp); // optional
    std::fclose(fp);
    meshesDirty_ = true;
    return true;
}

bool GridData::LoadChunk(int regionX, int regionZ) {
    // Region key for lookup — full page cache not yet recovered
    (void)EncodeWorldPos(regionX * 16, regionZ * 16);
    return false; // page-cache path TBD
}
