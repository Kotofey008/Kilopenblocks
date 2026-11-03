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
    // Region size 16 (verified). Floor-divide with negative handling matching
    // binary: test sign, lea +0xf / cmovns, then sar 4.
    auto reg = [](int v) -> int {
        if (v < 0) v += 15;
        return v >> 4;
    };
    RegionKey k{reg(x), reg(y), reg(z)};
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
    int bx = (int)std::floor(pos.x);
    int by = (int)std::floor(pos.y);
    int bz = (int)std::floor(pos.z);
    int idx = CoordToIndex(bx, by, bz);
    if (idx < 0) return false;
    return cellTypes[idx] == 5; // BlockId::Water — partial vs binary state mask 0x3fc0
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
    auto align16 = [](int v) -> int {
        // Binary: if (v & 0xf) != 0, snap toward +inf to next 16-multiple for positive
        int sign = v >> 31; // 0 or -1
        unsigned u = (unsigned)(v + (sign & 15));
        int r = (int)(u & ~15u) - (sign & 15);
        // Match observed: if misaligned positive, (v+15)&~15 then +0?  actually:
        //   if (v % 16 != 0) { if (v>=0) v = (v+15)&~15 + 0x10? — see 8200e }
        // Simpler faithful: same as 82006-82011 for each axis used as max-side
        if ((v & 15) == 0) return v;
        if (v >= 0)
            return (v + 15) & ~15;
        // negative: binary uses similar two's path
        return -(((-v + 15) & ~15));
    };
    // Binary applies align when (v&15)!=0 for the three "max-like" args in first group.
    // For world min/max pair we align the extents to 16.
    int a0 = align16(minX);
    int a1 = align16(minY);
    int a2 = align16(minZ);
    int a3 = align16(maxX);
    int a4 = align16(maxY);
    int a5 = align16(maxZ);
    // If only max-side provided via table (common path uses ±half sizes)
    // Store as world AABB
    Vector newMin{(float)std::min(a0, a3), (float)std::min(a1, a4), (float)std::min(a2, a5)};
    Vector newMax{(float)std::max(a0, a3), (float)std::max(a1, a4), (float)std::max(a2, a5)};
    // Binary compares this+0x44 / +0x48 — if equal return early success path variant
    if (newMin.x == worldMin.x && newMin.y == worldMin.y && newMin.z == worldMin.z &&
        newMax.x == worldMax.x && newMax.y == worldMax.y && newMax.z == worldMax.z) {
        return true;
    }
    worldMin = newMin;
    worldMax = newMax;
    // Rebuild type/state arrays for new bounds (clear — Store/Restore handles content)
    int sx = (int)(worldMax.x - worldMin.x);
    int sy = (int)(worldMax.y - worldMin.y);
    int sz = (int)(worldMax.z - worldMin.z);
    if (sx < 16) sx = 16;
    if (sy < 16) sy = 16;
    if (sz < 16) sz = 16;
    // GridData storage rebuild if API exists
    // Mark all regions dirty
    meshesDirty_ = true;
    return true;
}

void GridData::Store() {
    // 0xa3770 — snapshot cells before resize (full body large). No-op until cell dump needed.
}

void GridData::Restore(const Vector& /*playerPos*/) {
    // 0xa4d50 — restore cells near player after resize
}

void GridData::InvalidateLighting() {
    // 0x798f0
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
    // Default state 0
    unsigned short state = 0;
    // Orientation from dominant horizontal axis (recovered comparisons)
    float lx = lookDir.x, lz = lookDir.z;
    float ax = std::fabs(lx), az = std::fabs(lz);
    if (ax >= az) {
        // X dominant
        state = (lx >= 0.f) ? 0 : 1;
    } else {
        // Z dominant
        state = (lz >= 0.f) ? 2 : 3;
    }
    // Face-based override observed for some materials (property checks)
    // Full material table not loaded — orientation-only for now.
    (void)tr; (void)blockType;
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

// Simplified water tick based on UpdateWaterState @ 0xa1040 + flow direction helpers.
// Rules: water falls into air below; otherwise spreads horizontally to air neighbors
// (finite-ish by limiting steps). Full level-based flow still TBD.
void GridData::UpdateWater(int maxSteps) {
    // Partial UpdateWaterState @ 0xa1040:
    // Uses IsFlowAllowed* before placing; Min/MaxWaterHeight available for level
    // encoding once material table is loaded. Current: binary water type only.
    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0) return;
    const unsigned char WATER = 5;
    struct Cell { int x,y,z; };
    std::vector<Cell> sources;
    sources.reserve(256);
    for (int z = 0; z < sizeZ; ++z)
    for (int y = 0; y < sizeY; ++y)
    for (int x = 0; x < sizeX; ++x) {
        int i = x + sizeX * (y + sizeY * z);
        if (cellTypes[i] == WATER) sources.push_back({originX+x, originY+y, originZ+z});
    }
    int steps = 0;
    bool any = false;
    // Direction → FlowType mapping (binary axis names)
    // fall uses FlowYN into cell below
    for (size_t si = 0; si < sources.size() && steps < maxSteps; ++si) {
        int x = sources[si].x, y = sources[si].y, z = sources[si].z;
        int selfIdx = CoordToIndex(x, y, z);
        if (selfIdx < 0) continue;

        // Vertical fall (YN direction into cell below)
        int below = CoordToIndex(x, y - 1, z);
        if (below >= 0 && cellTypes[below] == 0) {
            if (IsFlowAllowed(below, FlowYN) || IsFlowAllowed(selfIdx, FlowYN)) {
                cellTypes[below] = WATER;
                // Default full height: state bit 0x04 clear → Min uses prop03
                if (below < (int)cellStates.size()) cellStates[below] = 0;
                any = true; steps++;
                continue;
            }
        }
        // Horizontal: XP XN ZP ZN
        struct Dir { int dx, dz; FlowType ft; };
        const Dir dirs[4] = {
            {1, 0, FlowXP}, {-1, 0, FlowXN},
            {0, 1, FlowZP}, {0, -1, FlowZN}
        };
        for (const auto& d : dirs) {
            if (steps >= maxSteps) break;
            int ni = CoordToIndex(x + d.dx, y, z + d.dz);
            if (ni < 0 || cellTypes[ni] != 0) continue;
            // Target cell must allow inflow
            if (!IsFlowAllowed(ni, d.ft) && !IsFlowAllowed(selfIdx, d.ft))
                continue;
            cellTypes[ni] = WATER;
            if (ni < (int)cellStates.size()) cellStates[ni] = 0;
            any = true; steps++;
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
