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
    // Binary @ 0x7b350: test fluid bits (state & 0x3fc0), not material type 5.
    int x = (int)std::floor(pos.x);
    int y = (int)std::floor(pos.y);
    int z = (int)std::floor(pos.z);
    int idx = CoordToIndex(x, y, z);
    if (idx < 0 || idx >= (int)cellStates.size()) return false;
    return HasLiquidState(cellStates[idx]);
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

void GridData::ActivateCell(int, const Vector&) {}

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

    // Binary FindFirstUsedCell @ 0x79dd0: non-air cells call
    // GetCellCollisionBoxArray; water (and other non-solid) return 0 boxes →
    // DDA continues. Effect: ray passes through water to the first solid.
    auto isTraceSolid = [&](int cidx) -> bool {
        if (cidx < 0 || cidx >= (int)cellTypes.size()) return false;
        unsigned char t = cellTypes[cidx];
        if (t == 0) return false;   // air / pure liquid column
        if (t == 5) return false;   // block id 5 water-block — pass through
        if (t == 12 || t == 13 || t == 14) return false;
        if (t == 17 || t == 18) return false;
        return true;
    };

    int face = -1;
    for (int i = 0; i < 512 && t <= t1; ++i) {
        int cidx = CoordToIndex(x, y, z);
        if (cidx >= 0 && isTraceSolid(cidx)) {
            outT = t;
            outCellIndex = cidx;
            return true;
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
    // Destination must be empty OR replaceable liquid (water). Binary allows
    // placing into water cells so streams can be blocked / seabed filled.
    {
        unsigned char dt = cellTypes[idx];
        unsigned short st = (idx < (int)cellStates.size()) ? cellStates[idx] : 0;
        bool empty = (dt == 0 && !HasLiquidState(st));
        // Type 5 = project water-block (kept); also any liquid state cell
        bool water = (dt == 5) || HasLiquidState(st);
        if (!empty && !water) return false;
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
unsigned short GridData::ConfigureState(const GridDataTraceResult& tr,
                                        unsigned char blockType,
                                        const Vector& lookDir) {
    // Liquid source: type 5 (Water). Binary SetLiquidState @ 0x99720 sets
    // fluid bit 0x2000 and level 0 (source) in bits 10-12. Without this,
    // placed water has state 0 and is invisible to the fluid simulation.
    // Block id 5: keep previous project behaviour (type 5 + fluid state).
    // Natural gen does not place this id; real world water is type 0 + state.
    if (blockType == 5) {
        (void)tr; (void)lookDir;
        return (unsigned short)(STATE_BIT_2000 | (0 << STATE_LEVEL_SHIFT));
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
    // Type 5 water-block: not breakable (previous project behaviour).
    // Pure liquid (type 0 + fluid state) also skipped by ray / not breakable.
    if (tr.blockType == 5) return false;
    int idx = CoordToIndex(tr.cellX, tr.cellY, tr.cellZ);
    if (idx >= 0 && idx < (int)cellTypes.size() && cellTypes[idx] == 5)
        return false;
    if (idx >= 0 && idx < (int)cellStates.size() && HasLiquidState(cellStates[idx]) &&
        cellTypes[idx] == 0)
        return false;
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
        m.raw[0x1b] = (uint8_t)(ftl & 0xff);  // flow perm base
        m.raw[0x1c] = (uint8_t)(ftw & 0xff);

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
    // World liquids are type==0 (Air) + state bits, matching AddGrassAndWater /
    // AddLiquidSource. Material type 5 is never written by this simulation.
    //
    // Water fluid: state & 0x2000  (sources often 0xE000 = 0xC000|0x2000)
    // Level: (state & 0x1C00) >> 10  (0=source .. 7)
    // Lava:  state & 0x3C0 without 0x2000 (source 0xC3C0) — tracked but not spread yet
    //
    // Order (UpdateWaterState @ 0xa1040): fall prefer → horizontal level+1 → source form

    static constexpr unsigned short ST_FLUID       = STATE_BIT_2000;
    static constexpr unsigned short ST_LEVEL_MASK  = STATE_LEVEL_MASK;
    static constexpr unsigned short ST_LEVEL_SHIFT = STATE_LEVEL_SHIFT;
    static constexpr unsigned short ST_FLUID_MASK  = STATE_LIQUID_ANY;

    auto makeWaterState = [](int level) -> unsigned short {
        return MakeWaterState(level, /*asSource=*/level == 0);
    };
    auto getLevel = [&](int idx) -> int {
        if (idx < 0 || idx >= (int)cellStates.size()) return 7;
        return LiquidLevel(cellStates[idx]);
    };
    auto isWaterFluid = [&](int idx) -> bool {
        if (idx < 0 || idx >= (int)cellStates.size()) return false;
        return IsWaterFluidState(cellStates[idx]);
    };
    auto canReceiveFluid = [&](int idx) -> bool {
        if (idx < 0 || idx >= (int)cellTypes.size()) return false;
        unsigned char t = cellTypes[idx];
        unsigned short st = cellStates[idx];
        // Air, or already water-fluid, or replaceable (no collidable solid)
        if (t == 0) return true;
        if (IsWaterFluidState(st)) return true;
        // Type 5 hidden material: treat as non-receiver for sim (leave in place)
        if (t == 5 && !HasLiquidState(st)) return false;
        // Soft / non-col materials may allow flow via IsFlowAllowed
        if (!materials.empty() && t < materials.size()) {
            if (materials[t].raw[0x06] == 0) return true; // Col==0
        }
        return false;
    };

    struct Cell { int x, y, z; };
    std::vector<Cell> active;
    active.reserve(256);
    for (size_t i = 0; i < cellTypes.size(); ++i) {
        if (i < cellStates.size() && IsWaterFluidState(cellStates[i])) {
            int lx = (int)(i % (size_t)sizeX);
            int ly = (int)((i / (size_t)sizeX) % (size_t)sizeY);
            int lz = (int)(i / ((size_t)sizeX * (size_t)sizeY));
            active.push_back({originX + lx, originY + ly, originZ + lz});
        }
    }

    int steps = 0;
    bool any = false;

    auto tryPlace = [&](int nx, int ny, int nz, int newLevel, FlowType inflow) -> bool {
        if (steps >= maxSteps) return false;
        int ni = CoordToIndex(nx, ny, nz);
        if (ni < 0) return false;
        if (!canReceiveFluid(ni)) return false;

        unsigned short ns = cellStates[ni];
        int nLevel = LiquidLevel(ns);
        bool empty = !HasLiquidState(ns) && cellTypes[ni] == 0;
        bool replaceHigher = IsWaterFluidState(ns) && newLevel < nLevel;
        if (!empty && !replaceHigher) return false;
        if (IsFlowAllowed(ni, inflow) == 0) return false;

        // Binary: liquid cells are type 0 + state (never material 5)
        SetCell(nx, ny, nz, /*Air*/0, makeWaterState(newLevel));
        any = true;
        ++steps;
        return true;
    };

    auto isSolidSupport = [&](int idx) -> bool {
        if (idx < 0) return false;
        if (IsWaterFluidState(cellStates[idx])) return false;
        if (IsLavaFluidState(cellStates[idx])) return false;
        unsigned char t = cellTypes[idx];
        if (t == 0) return false;
        // Hidden type 5 without fluid still counts as solid block
        return true;
    };
    auto isWaterIdx = [&](int idx) -> bool {
        return idx >= 0 && isWaterFluid(idx);
    };

    for (size_t si = 0; si < active.size() && steps < maxSteps; ++si) {
        int x = active[si].x, y = active[si].y, z = active[si].z;
        int selfIdx = CoordToIndex(x, y, z);
        if (selfIdx < 0 || !isWaterFluid(selfIdx)) continue;

        int level = getLevel(selfIdx);
        // Normalize source encoding: 0xE000 → level 0
        if ((cellStates[selfIdx] & 0xE000) == 0xE000) level = 0;

        int below = CoordToIndex(x, y - 1, z);
        int above = CoordToIndex(x, y + 1, z);
        bool waterBelow = isWaterIdx(below);
        bool solidBelow = isSolidSupport(below);
        bool waterAbove = isWaterIdx(above);
        bool trueSource = (level == 0);
        bool falseSource = trueSource == false && solidBelow && !waterBelow;

        // Fall
        if (below >= 0 && canReceiveFluid(below)) {
            if (!waterBelow || (IsWaterFluidState(cellStates[below]) && getLevel(below) > 0)) {
                if (IsFlowAllowed(below, FlowYN) != 0 || IsFlowAllowed(selfIdx, FlowYN) != 0) {
                    int placeLvl = 1;
                    if (trueSource) placeLvl = 1;
                    else if (level < 7) placeLvl = level + 1;
                    else placeLvl = 7;
                    // Prefer fall this tick
                    if (tryPlace(x, y - 1, z, trueSource ? 0 : placeLvl, FlowYN)) {
                        // flowing cell without support dries when it can fall
                        if (!trueSource && !solidBelow) {
                            // keep current; binary spreads down
                        }
                        continue;
                    }
                }
            }
        }

        // Dry unsupported flowing water (no solid/water below)
        if (!trueSource && !solidBelow && !waterBelow) {
            // try fall already failed — evaporate / clear
            SetCell(x, y, z, 0, 0);
            any = true;
            ++steps;
            continue;
        }

        if (waterBelow) continue;

        int spreadFrom = trueSource || falseSource ? 0 : level;
        if (spreadFrom >= 7) continue;
        const int newLevel = spreadFrom + 1;
        struct Dir { int dx, dz; FlowType ft; };
        static const Dir dirs[4] = {
            { 1, 0, FlowXP}, {-1, 0, FlowXN},
            { 0, 1, FlowZP}, { 0,-1, FlowZN}
        };
        for (const auto& d : dirs) {
            if (steps >= maxSteps) break;
            tryPlace(x + d.dx, y, z + d.dz, newLevel, d.ft);
        }
        (void)waterAbove;
    }

    // New source formation: ≥2 horizontal level-0 water neighbours + support
    if (steps < maxSteps) {
        for (size_t si = 0; si < active.size() && steps < maxSteps; ++si) {
            int x = active[si].x, y = active[si].y, z = active[si].z;
            int selfIdx = CoordToIndex(x, y, z);
            if (selfIdx < 0 || !isWaterFluid(selfIdx)) continue;
            if (getLevel(selfIdx) == 0) continue;
            int below = CoordToIndex(x, y - 1, z);
            bool supportOk = isSolidSupport(below) ||
                             (isWaterIdx(below) && getLevel(below) == 0);
            if (!supportOk) continue;
            int sourceNeigh = 0;
            static const int offs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& o : offs) {
                int ni = CoordToIndex(x + o[0], y, z + o[1]);
                if (!isWaterIdx(ni)) continue;
                if (getLevel(ni) == 0) ++sourceNeigh;
            }
            if (sourceNeigh >= 2) {
                SetCell(x, y, z, 0, makeWaterState(0));
                any = true;
                ++steps;
            }
        }
    }
    (void)any;
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
