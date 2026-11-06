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
    if (((oldStored ^ newState) & (STATE_BIT_2000 | STATE_BIT_3C0 | STATE_BIT_4000)) != 0) {
        meshesDirty_ = true;
    }
}

// ---------------------------------------------------------------------------
// SetCell (0x77d80, 5910 B / 122 BBs) — see
//   pseudocode/recovered/0x77d80_SetCell_COMPLETE.md
//
// Signature CONFIRMED.
//
// CONFIRMED side effects implemented here:
//   - types[index] = type
//   - SetState(index, state)  → primary states + aux+0x230 merge
//     (old&0x4000)|(new&0xBFFF) + hierarchical 0x4000 occupancy
//   - region[rx,rz]+0x1f = 1  (MarkRegionDirty)
//   - meshesDirty_ / MarkModifiedRegions
//   - face-neighbour region dirty when type or 0x2000/0x3c0 change
//
// CONFIRMED but NOT implemented (gates PARTIAL — do not invent predicates):
//   - 16 SetState call sites that OR 0xC000 onto face-neighbour states
//   - Per-axis dirty byte arrays at +0x66d/+0x74d/+0x82d/+0x90d/...
//   - this+0xf0 batch/generation flag that suppresses neighbour work
//
// The "24 loops" reported by afi are hierarchy propagation chains and
// shared-exit CFG diamonds, not volume iteration. See COMPLETE.md §3.
// ---------------------------------------------------------------------------
void GridData::SetCell(int x, int y, int z, unsigned char type, unsigned short state) {
    int idx = CoordToIndex(x, y, z);
    if (idx < 0) return;

    unsigned char oldType = cellTypes[idx];
    unsigned short oldState = cellStates[idx];
    bool typeChanged = (oldType != type);

    // Type write (full path). State-only path leaves type unchanged (already equal).
    cellTypes[idx] = type;

    // Always run SetState: binary state-only path still reaches the aux-merge
    // at 0x77ecd when (old^new)&0x3fff==0, and re-enters the full path otherwise.
    SetState(idx, state);
    unsigned short newState = cellStates[idx];

    bool bit2000Changed = ((oldState ^ newState) & STATE_BIT_2000) != 0;
    bool bit3c0Changed  = ((oldState ^ newState) & STATE_BIT_3C0) != 0;
    bool bit4000Changed = ((oldState ^ newState) & STATE_BIT_4000) != 0;
    bool lowBitsChanged = ((oldState ^ newState) & 0x3fff) != 0;
    bool stateRelevant = bit2000Changed || bit3c0Changed || bit4000Changed || lowBitsChanged;

    if (typeChanged || stateRelevant) {
        // Region dirty (original: region entry +0x1f = 1). Region size 16.
        MarkRegionDirty(x, y, z);
        meshesDirty_ = true;

        // Approximate the original per-axis dirty bitmaps (+0x66d/+0x74d/+0x82d)
        // and the neighbour-region marks that accompany type / 0x2000 / 0x3c0
        // changes. We do NOT OR 0xC000 onto neighbour states: those SetState
        // calls are gated by material-table and this+0xf0 tests that remain
        // PARTIAL (see COMPLETE.md §2, §7).
        if (typeChanged || bit2000Changed || bit3c0Changed) {
            static const int offs[6][3] = {
                {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
            };
            for (auto& o : offs) {
                int nx = x + o[0], ny = y + o[1], nz = z + o[2];
                if (CoordToIndex(nx, ny, nz) >= 0)
                    MarkRegionDirty(nx, ny, nz);
            }
        }
        MarkModifiedRegions();
    }
}

void GridData::MarkRegionDirty(int x, int y, int z) {
    // Region size 16. Origin must match EnsureRegionMeshes / full-build alignment:
    //   origin = coord & ~15  (two's-complement, works for negatives)
    // The earlier (v<0 ? v+15 : v)>>4 formula maps e.g. -1 → region 0 while
    // full-build places cells -16..-1 at origin -16 — mesh key mismatch →
    // second-chunk edits never replaced the stored RegionMesh (physics ok, view stale).
    int ox = x & ~15;
    int oy = y & ~15;
    int oz = z & ~15;
    // Store as region indices (origin/16) for compactness; rebuild multiplies back.
    RegionKey k{ox >> 4, oy >> 4, oz >> 4};
    for (const auto& d : dirtyRegions_)
        if (d == k) return;
    dirtyRegions_.push_back(k);
}

bool GridData::IsRegionDirty(int rx, int ry, int rz) const {
    RegionKey k{rx, ry, rz};
    for (const auto& d : dirtyRegions_)
        if (d == k) return true;
    return false;
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
    // Binary IsUnderwater @ 0x7b350: floor cell, then (states[idx] & 0x3fc0) != 0
    // (fluid bits). Type-5 accepted as fallback when state not yet fluid-tagged.
    int bx = (int)std::floor(pos.x);
    int by = (int)std::floor(pos.y);
    int bz = (int)std::floor(pos.z);
    int idx = CoordToIndex(bx, by, bz);
    if (idx < 0) return false;
    if (idx < (int)cellStates.size() && (cellStates[idx] & 0x3fc0) != 0)
        return true;
    return cellTypes[idx] == 5;
}

bool GridData::OnLadder(const Vector&, const Vector&, const Vector&) const {
    return false; // OnLadder @ 0x7b090 not fully recovered
}

void GridData::GetCellSelectionBox(int, Vector&, Vector&) {}
void GridData::ActivateCell(int, const Vector&) {}

GridData::LeavesQuality GridData::GetLeavesQuality() const {
    return leavesQuality_; // +0xec
}
void GridData::SetLeavesQuality(LeavesQuality q) {
    leavesQuality_ = q;
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
    x = rx; y = ry; z = rz;
    unknown0 = unknown1 = 0x7fffffff;
}
void GridData::Region::Free() {}



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
    // Original @ 0x7aad0 — walks dirty cells and flags regions
    meshesDirty_ = true;
}

void GridData::Invalidate() {
    // Original @ 0x794a0 — forces full mesh rebuild
    MarkModifiedRegions();
}




// ---------------------------------------------------------------------------
// FindFirstUsedCell (0x79dd0, 2541 B) — recovered structure
//
// Verified from disassembly:
//   1. Build world AABB from integer bounds (esi+0x10/14/18/1c/48)
//   2. BoxLineSegIntersect (0xb3520) → entry/exit params (t0,t1) as doubles
//   3. If miss → return cellIndex = -1
//   4. Compute entry point = from + t0 * (to-from); floor to cell coords
//   5. Clamp cell coords to [0, dim-1]
//   6. DDA step loop:
//        - Map cell → region (>>4), check region flag at +0x1e
//        - If region active: linear index, if types[idx] != 0 → HIT
//        - Step along axis with smallest tMax (classic 3D DDA)
//   7. Return cellIndex + t
//
// Current implementation follows this structure with a simple slab AABB clip
// in place of the full 2269 B BoxLineSegIntersect, and DDA matching the
// observed step/clamp/type-check pattern. Region +0x1e flag approximated
// as "always active" until region array is fully mapped.
// ---------------------------------------------------------------------------
static bool ClipRayAABB(const Vector& from, const Vector& to,
                        const Vector& bmin, const Vector& bmax,
                        float& t0, float& t1) {
    // Slab method (core of BoxLineSegIntersect for axis-aligned box)
    float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    t0 = 0.f; t1 = 1.f;
    auto slab = [&](float p, float d, float minv, float maxv) -> bool {
        if (std::fabs(d) < 1e-8f) {
            return p >= minv && p <= maxv;
        }
        float inv = 1.f / d;
        float ta = (minv - p) * inv, tb = (maxv - p) * inv;
        if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        return t0 <= t1;
    };
    return slab(from.x, dx, bmin.x, bmax.x)
        && slab(from.y, dy, bmin.y, bmax.y)
        && slab(from.z, dz, bmin.z, bmax.z)
        && t1 >= 0.f && t0 <= 1.f;
}

// Returns true on hit; writes *outT (0..1 along segment) and *outCellIndex
bool GridData::FindFirstUsedCell(const Vector& from, const Vector& to,
                                 float& outT, int& outCellIndex) {
    outT = 0.f;
    outCellIndex = -1;
    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0) return false;

    Vector bmin((float)originX, (float)originY, (float)originZ);
    Vector bmax((float)(originX + sizeX), (float)(originY + sizeY), (float)(originZ + sizeZ));

    float t0, t1;
    if (!ClipRayAABB(from, to, bmin, bmax, t0, t1)) return false;
    if (t0 < 0.f) t0 = 0.f;
    if (t1 > 1.f) t1 = 1.f;

    float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    // Entry point
    float ex = from.x + dx * t0;
    float ey = from.y + dy * t0;
    float ez = from.z + dz * t0;

    int x = (int)std::floor(ex);
    int y = (int)std::floor(ey);
    int z = (int)std::floor(ez);
    // Clamp to bounds (observed: cmovl dim-1)
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
    // t along full segment; start at t0
    float t = t0;
    if (stepX != 0) {
        float inv = 1.f / std::fabs(dx);
        tDeltaX = inv; // one cell in param space (segment length normalized later)
        // distance in segment-param to next X plane from entry
        float next = (stepX > 0) ? (std::floor(ex) + 1.f - ex) : (ex - std::floor(ex));
        if (stepX < 0 && frac(ex) == 0.f) next = 1.f;
        tMaxX = t0 + next * inv * (dx != 0 ? std::fabs(dx)/std::fabs(dx) : 0); // simplify
        // Use absolute param: t increases by (cell_size / |dir|) along segment
        // dir is (to-from) so |dir_comp| is |dx|; param step = 1/|dx| for 1 cell
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

    int face = -1;
    for (int i = 0; i < 512 && t <= t1; ++i) {
        int cidx = CoordToIndex(x, y, z);
        if (cidx >= 0) {
            // Region flag check (orig: region[+0x1e] != 0) — approximated always true
            if (cellTypes[cidx] != 0) {
                outT = t;
                outCellIndex = cidx;
                return true;
            }
        }
        // Step
        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                t = tMaxX; tMaxX += tDeltaX; x += stepX;
                face = (stepX > 0) ? 1 : 0;
            } else {
                t = tMaxZ; tMaxZ += tDeltaZ; z += stepZ;
                face = (stepZ > 0) ? 5 : 4;
            }
        } else {
            if (tMaxY < tMaxZ) {
                t = tMaxY; tMaxY += tDeltaY; y += stepY;
                face = (stepY > 0) ? 3 : 2;
            } else {
                t = tMaxZ; tMaxZ += tDeltaZ; z += stepZ;
                face = (stepZ > 0) ? 5 : 4;
            }
        }
        // Bounds exit
        if (x < originX || y < originY || z < originZ ||
            x >= originX + sizeX || y >= originY + sizeY || z >= originZ + sizeZ)
            break;
    }
    (void)face;
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
    float t = 0.f;
    int cellIndex = -1;
    if (!FindFirstUsedCell(from, to, t, cellIndex)) return;
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

    // Reconstruct face by short backward step from hit (approximate; full face
    // comes from DDA step axis in FindFirstUsedCell — stored via re-trace below)
    float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    result.hit = true;
    result.cellIndex = cellIndex;
    result.cellX = cx; result.cellY = cy; result.cellZ = cz;
    result.distance = t;
    result.blockType = cellTypes[cellIndex];
    result.hitPos = Vector(from.x + dx * t, from.y + dy * t, from.z + dz * t);

    // Face: determine from which side we entered by checking neighbor along -dir
    int face = -1;
    float ax = std::fabs(dx), ay = std::fabs(dy), az = std::fabs(dz);
    if (ax >= ay && ax >= az) face = (dx > 0) ? 1 : 0;
    else if (ay >= ax && ay >= az) face = (dy > 0) ? 3 : 2;
    else face = (dz > 0) ? 5 : 4;
    result.face = face;
}

// GetAddingPosition (0x99240, 743 B)
bool GridData::GetAddingPosition(const GridDataTraceResult& tr, unsigned char /*blockType*/,
                                 int& outCellIndex, unsigned char& outFace) {
    if (!tr.hit) return false;
    if (tr.blockType == 0x4c) return false; // binary reject

    int x = tr.cellX, y = tr.cellY, z = tr.cellZ;
    switch (tr.face) {
        case 0: x++; break;
        case 1: x--; break;
        case 2: y++; break;
        case 3: y--; break;
        case 4: z++; break;
        case 5: z--; break;
        default: break;
    }
    outCellIndex = CoordToIndex(x, y, z);
    outFace = (unsigned char)tr.face;
    return outCellIndex >= 0;
}

bool GridData::IsPossibleToAdd(const GridDataTraceResult& tr, unsigned char blockType, int /*flags*/,
                               const Vector* playerPos, const Vector* playerSize) {
    int idx = -1; unsigned char face = 0;
    if (!GetAddingPosition(tr, blockType, idx, face)) return false;
    if (idx < 0 || idx >= (int)cellTypes.size()) return false;
    if (cellTypes[idx] != 0) return false;

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
        const Material& m = materials[i];
        int16_t si = (int16_t)(m.raw[0x20] | (m.raw[0x21] << 8));
        if (si == (int16_t)selIndex)
            return (unsigned char)i;
    }
    return 0;
}

int GridData::CellToMtlSelIndex(unsigned char cellType) const {
    // 0x98f10: materials[cell].+0x20 as int16
    if (cellType >= materials.size()) return -1;
    const Material& m = materials[cellType];
    return (int)(int16_t)(m.raw[0x20] | (m.raw[0x21] << 8));
}

// ConfigureState (0x997f0, 765 B) — orientation / state bit computation
// Material table at +0xe8, stride 34 (type*32+type*2).
// Orientation from dominant look-vector axis:
//   state 0/1/2/3 based on |look.x| vs |look.z| and signs; bit 0x04 for variants.
// Special material property bytes at +0x01,+0x05,+0x08 drive liquid/slab paths.
// GetDirectionMarker, SetLiquidState, FullHeightBlockVariant called for specials.
unsigned short GridData::ConfigureState(const GridDataTraceResult& tr,
                                        unsigned char blockType,
                                        const Vector& lookDir) {
    // Liquid source: type 5 (Water). Binary SetLiquidState @ 0x99720 sets
    // fluid bit 0x2000 and level 0 (source) in bits 10-12. Without this,
    // placed water has state 0 and is invisible to the fluid simulation.
    if (blockType == 5 /* Water */) {
        (void)tr; (void)lookDir;
        return (unsigned short)(0x2000 | (0 << 10)); // ST_FLUID | level 0
    }

    // Default solid orientation 0-3 from dominant horizontal look axis
    unsigned short state = 0;
    float lx = lookDir.x, lz = lookDir.z;
    float ax = std::fabs(lx), az = std::fabs(lz);
    if (ax >= az) {
        state = (lx >= 0.f) ? 0 : 1;
    } else {
        state = (lz >= 0.f) ? 2 : 3;
    }
    (void)tr;
    return state;
}

bool GridData::PlaceBlock(const GridDataTraceResult& tr, unsigned char blockType,
                          const Vector* playerPos, const Vector* playerSize) {
    if (!IsPossibleToAdd(tr, blockType, 0, playerPos, playerSize)) return false;
    int idx = -1; unsigned char face = 0;
    GetAddingPosition(tr, blockType, idx, face);
    int lx = idx % sizeX;
    int ly = (idx / sizeX) % sizeY;
    int lz = idx / (sizeX * sizeY);
    // lookDir unknown here — use face as proxy for orientation
    Vector look(0, 0, 1);
    if (face == 0 || face == 1) look = Vector(face == 0 ? 1.f : -1.f, 0, 0);
    else if (face == 4 || face == 5) look = Vector(0, 0, face == 4 ? 1.f : -1.f);
    unsigned short state = ConfigureState(tr, blockType, look);
    SetCell(originX + lx, originY + ly, originZ + lz, blockType, state);
    return true;
}

bool GridData::BreakBlock(const GridDataTraceResult& tr) {
    if (!tr.hit) return false;
    // ConfigureAirState (0x99af0) → air + state 0
    SetCell(tr.cellX, tr.cellY, tr.cellZ, 0, 0);
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
    if (materials.empty()) InitMaterials();
    // Android assets require AAssetManager — never fopen relative paths.
    std::string text;
    if (!Assets::ReadText(path, text)) {
        // Fallback: try as filesystem path (host tools)
        FILE* fp = std::fopen(path, "r");
        if (!fp) return false;
        char buf[1024];
        while (std::fgets(buf, sizeof(buf), fp)) text += buf;
        std::fclose(fp);
    }
    std::istringstream in(text);
    std::string line;
    int loaded = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && (unsigned char)line[0] == 0xEF) {
            // UTF-8 BOM on first line
            if (line.size() >= 3) line = line.substr(3);
        }
        const char* p = line.c_str();
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\0' || *p == '\n' || *p == '\r') continue;
        if (std::strstr(p, "Index") && std::strstr(p, "Uxp")) continue;

        int idx = -1, sIndex = -1;
        int uxp=0,vxp=0,uyp=0,vyp=0,uzp=0,vzp=0,uxn=0,vxn=0,uyn=0,vyn=0,uzn=0,vzn=0;
        int bh=0, bmt=0, mbi=0, sau=0, col=0, all=0, uvr=0;
        int xt=0,yt=0,zt=0,w=0,h=0,l=0,ftl=0,ftw=0;
        int n = std::sscanf(p,
            "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
            &idx, &sIndex,
            &uxp,&vxp,&uyp,&vyp,&uzp,&vzp,&uxn,&vxn,&uyn,&vyn,&uzn,&vzn,
            &bh,&bmt,&mbi,&sau,&col,&all,&uvr,&xt,&yt,&zt,&w,&h,&l,&ftl,&ftw);
        // Need at least Index + S-Index + 12 UV ints
        if (n < 14 || idx < 0 || idx >= (int)materials.size()) continue;
        Material& m = materials[idx];
        m.initDefault(idx);
        int cls = sIndex;
        if (cls < 0 || cls > 0x0a) cls = bmt;
        if (cls < 0 || cls > 0x0a) cls = mbi;
        if (cls < 0 || cls > 0x0a) cls = 0;
        m.raw[0x01] = (uint8_t)cls;
        m.raw[0x02] = (col != 0) ? 0 : 1;
        m.raw[0x03] = (uint8_t)(bh & 0xff);
        // +0x20/+0x21: S-Index as int16 (MtlSelIndexToCell / CellToMtlSelIndex)
        {
            int16_t si = (int16_t)sIndex;
            m.raw[0x20] = (uint8_t)(si & 0xff);
            m.raw[0x21] = (uint8_t)((si >> 8) & 0xff);
        }
        // UV faces order in material table: XP YP ZP XN YN ZN (binary +0x0c..+0x17)
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
        m.raw[0x07] = (all != 0) ? 1 : 0;
        uint8_t perm = (col == 0) ? 0x0f : 0x00;
        for (int f = 0; f < 6; ++f)
            m.raw[0x1b + f] = perm;
        ++loaded;
    }
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
    // Fallback when material table not loaded
    if (t == 0 || t == 5 || t == 11) return 0x0f;
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
void GridData::UpdateWater(int maxSteps) {
    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0) return;
    if (maxSteps <= 0) maxSteps = 32;

    static constexpr unsigned char WATER = 5;
    static constexpr unsigned short ST_LEVEL_MASK = 0x1c00;
    static constexpr int             ST_LEVEL_SHIFT = 10;
    static constexpr unsigned short ST_FLUID       = 0x2000;
    static constexpr unsigned short ST_FLUID_MASK  = 0x3fc0; // empty-fluid test

    auto getLevel = [&](int idx) -> int {
        if (idx < 0 || idx >= (int)cellStates.size()) return 7;
        return (int)((cellStates[idx] & ST_LEVEL_MASK) >> ST_LEVEL_SHIFT);
    };
    auto makeWaterState = [](int level) -> unsigned short {
        if (level < 0) level = 0;
        if (level > 7) level = 7;
        return (unsigned short)(ST_FLUID | ((level & 7) << ST_LEVEL_SHIFT));
    };
    // material[+0x1c] receive gate (SpreadWater)
    auto canReceiveFluid = [&](int idx) -> bool {
        if (idx < 0 || idx >= (int)cellTypes.size()) return false;
        unsigned char t = cellTypes[idx];
        if (!materials.empty() && t < materials.size()) {
            // +0x1c is flowPerm(FlowXN=1) in our layout; binary uses it as
            // "accepts fluid" flag. Transparent/air/water materials get 0x0f.
            return materials[t].raw[0x1c] != 0;
        }
        // Fallback without table: air / water / glass / leaves
        return t == 0 || t == WATER || t == 11 || t == 10 || t == 20;
    };

    struct Cell { int x, y, z; };
    std::vector<Cell> active;
    active.reserve(256);

    // Collect fluid cells (type water, or state has fluid bit)
    for (int z = 0; z < sizeZ; ++z)
    for (int y = 0; y < sizeY; ++y)
    for (int x = 0; x < sizeX; ++x) {
        int i = x + sizeX * (y + sizeY * z);
        if (cellTypes[i] == WATER) {
            active.push_back({originX + x, originY + y, originZ + z});
        } else if (i < (int)cellStates.size() &&
                   (cellStates[i] & ST_FLUID) != 0) {
            active.push_back({originX + x, originY + y, originZ + z});
        }
    }

    int steps = 0;
    bool any = false;

    auto tryPlace = [&](int nx, int ny, int nz, int newLevel, FlowType inflow) -> bool {
        if (steps >= maxSteps) return false;
        int ni = CoordToIndex(nx, ny, nz);
        if (ni < 0) return false;
        if (!canReceiveFluid(ni)) return false;

        unsigned char nt = cellTypes[ni];
        unsigned short ns = (ni < (int)cellStates.size()) ? cellStates[ni] : 0;
        int nLevel = (int)((ns & ST_LEVEL_MASK) >> ST_LEVEL_SHIFT);

        // Empty (no fluid bits) or existing fluid with strictly higher level
        bool empty = (ns & ST_FLUID_MASK) == 0 && nt == 0;
        bool replaceHigher = (ns & ST_FLUID) != 0 && nt == WATER && newLevel < nLevel;
        if (!empty && !replaceHigher) return false;

        // IsFlowAllowed on destination for the inflow direction
        if (IsFlowAllowed(ni, inflow) == 0) return false;

        // Write via SetCell so region dirty / mesh side-effects fire
        SetCell(nx, ny, nz, WATER, makeWaterState(newLevel));
        any = true;
        ++steps;
        return true;
    };

    for (size_t si = 0; si < active.size() && steps < maxSteps; ++si) {
        int x = active[si].x, y = active[si].y, z = active[si].z;
        int selfIdx = CoordToIndex(x, y, z);
        if (selfIdx < 0) continue;
        if (cellTypes[selfIdx] != WATER) continue;

        int level = getLevel(selfIdx);

        // --- Vertical fall (preferred) — SpreadWater / UpdateOneWaterState YN path
        // Destination keeps source level (falling column is still "full" at that depth).
        int below = CoordToIndex(x, y - 1, z);
        if (below >= 0) {
            unsigned char bt = cellTypes[below];
            unsigned short bs = (below < (int)cellStates.size()) ? cellStates[below] : 0;
            bool belowEmpty = (bt == 0 && (bs & ST_FLUID_MASK) == 0);
            bool belowHigher = (bt == WATER && (bs & ST_FLUID) &&
                                level < (int)((bs & ST_LEVEL_MASK) >> ST_LEVEL_SHIFT));
            if (belowEmpty || belowHigher) {
                if (IsFlowAllowed(below, FlowYN) != 0 || IsFlowAllowed(selfIdx, FlowYN) != 0) {
                    if (tryPlace(x, y - 1, z, level, FlowYN)) {
                        // After successful fall from a non-source, the source cell
                        // itself may dry — binary does this in UpdateOneWaterState
                        // when no supporting neighbours remain. For sources (level 0)
                        // we keep the block; for flowing water we leave it (infinite
                        // column from above is handled next tick).
                        continue; // prefer fall over horizontal this tick
                    }
                }
            }
        }

        // --- Horizontal spread (level + 1), only if level < 7
        // Matches SpreadWater: arg_14h (srcLevel) < 7 for empty, or < neighLevel.
        if (level >= 7) continue;
        const int newLevel = level + 1;
        struct Dir { int dx, dz; FlowType ft; };
        static const Dir dirs[4] = {
            { 1, 0, FlowXP}, {-1, 0, FlowXN},
            { 0, 1, FlowZP}, { 0,-1, FlowZN}
        };
        for (const auto& d : dirs) {
            if (steps >= maxSteps) break;
            tryPlace(x + d.dx, y, z + d.dz, newLevel, d.ft);
        }
    }

    // --- New source formation (UpdateOneWaterState neighbour scan)
    // Binary tracks adjacent level==0 cells (bStack_26 / source count). When a
    // flowing cell has ≥2 horizontal neighbours that are sources, promote to
    // level 0 (infinite spring). Matches classic two-source infinite water.
    if (steps < maxSteps) {
        for (size_t si = 0; si < active.size() && steps < maxSteps; ++si) {
            int x = active[si].x, y = active[si].y, z = active[si].z;
            int selfIdx = CoordToIndex(x, y, z);
            if (selfIdx < 0 || cellTypes[selfIdx] != WATER) continue;
            int level = getLevel(selfIdx);
            if (level == 0) continue; // already source
            int sourceNeigh = 0;
            static const int offs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& o : offs) {
                int ni = CoordToIndex(x + o[0], y, z + o[1]);
                if (ni < 0 || cellTypes[ni] != WATER) continue;
                if (getLevel(ni) == 0) ++sourceNeigh;
            }
            if (sourceNeigh >= 2) {
                SetCell(x, y, z, WATER, makeWaterState(0));
                any = true;
                ++steps;
            }
        }
    }

    // Evaporation / dry-out of unsupported flowing water (level > 0):
    // if a flowing cell has no adjacent water with level <= ownLevel-1 (or source),
    // and nothing above feeding it, raise level toward 7 then clear.
    // Budget-limited; runs after spread so newly placed cells are stable one tick.
    if (steps < maxSteps) {
        for (size_t si = 0; si < active.size() && steps < maxSteps; ++si) {
            int x = active[si].x, y = active[si].y, z = active[si].z;
            int selfIdx = CoordToIndex(x, y, z);
            if (selfIdx < 0 || cellTypes[selfIdx] != WATER) continue;
            int level = getLevel(selfIdx);
            if (level == 0) continue; // source — never evaporate

            bool supported = false;
            // Supported by water above (any level)
            int above = CoordToIndex(x, y + 1, z);
            if (above >= 0 && cellTypes[above] == WATER) supported = true;
            // Supported by neighbour with strictly lower level (more full)
            if (!supported) {
                static const int offs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                for (auto& o : offs) {
                    int ni = CoordToIndex(x + o[0], y, z + o[1]);
                    if (ni < 0 || cellTypes[ni] != WATER) continue;
                    if (getLevel(ni) < level) { supported = true; break; }
                }
            }
            if (supported) continue;

            // Dry one step
            if (level >= 7) {
                SetCell(x, y, z, 0, 0); // fully dry → air
            } else {
                SetCell(x, y, z, WATER, makeWaterState(level + 1));
            }
            any = true;
            ++steps;
        }
    }

    if (any) MarkModifiedRegions();
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
