#include "TreeBuilder.h"
#include <cstdint>

// Binary BuildTree leaf/branch pattern table (4 layers × 5×5), recovered from
// libExplorationLite.so @ file offset 0x12e080 (ebx+0x208c relative to GOT).
// Cell values:
//   0  — empty
//   4  — wood branch (same type as trunk)
//   10 — leaves (always BlockId::Leaves1)
//   20 — leaves with sparse RNG (seedBits & 3)==0 → place, else skip
static const unsigned char kTreePattern[4][5][5] = {
    // Layer 0
    {
        { 0, 10, 10, 10,  0},
        {10, 10, 10, 10, 10},
        {10, 10,  4, 10, 10},
        {10, 10, 10, 10, 10},
        { 0, 10, 10, 10,  0},
    },
    // Layer 1
    {
        { 0, 10, 10, 10,  0},
        {10, 10, 10, 10, 10},
        {10, 10,  4, 10, 10},
        {10, 10, 10, 10, 10},
        { 0, 10, 10, 10,  0},
    },
    // Layer 2 — corners/edges use 20 (0x14) sparse
    {
        { 0, 20, 10, 20,  0},
        {20, 10, 10, 10, 20},
        {10, 10,  4, 10, 10},
        {20, 10, 10, 10, 20},
        { 0, 20, 10, 20,  0},
    },
    // Layer 3 — top cross
    {
        { 0,  0,  0,  0,  0},
        { 0,  0, 10,  0,  0},
        { 0, 10, 10, 10,  0},
        { 0,  0, 10,  0,  0},
        { 0,  0,  0,  0,  0},
    },
};

// Material mask 0x180781 from CheckTreePosition — bits for types that may occupy
// the 3×3 column without blocking (with type <= 0x14).
static constexpr unsigned kClearMask = 0x180781u;

static inline void PutCell(GridData& grid, int x, int y, int z,
                           unsigned char type, unsigned short state = 0) {
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0) return;
    grid.cellTypes[idx] = type;
    if (idx < (int)grid.cellStates.size())
        grid.cellStates[idx] = state;
}

static inline bool CellBlocksTree(GridData& grid, int x, int y, int z) {
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0) return false; // outside world — binary skips OOB local cells
    unsigned char type = grid.cellTypes[idx];
    // Fail if type > 0x14 OR type not in mask OR state high-byte bit 0x20 (0x2000)
    if (type > 0x14)
        return true;
    if (((1u << (type & 0x1f)) & kClearMask) == 0)
        return true;
    if (idx < (int)grid.cellStates.size()) {
        // Binary: *(byte*)(stateBase + 1 + ushortIndex*2) & 0x20 → state & 0x2000
        if ((grid.cellStates[idx] & 0x2000) != 0)
            return true;
    }
    return false;
}

bool CheckTreePosition(GridData& grid, int x, int y, int z, int height) {
    // Binary @ 0xea290
    if (height < 5)
        height = 5;

    // Must fit: y + 1 + height < world height (binary compares to edit max-Y)
    int maxY = (int)grid.worldMax.y;
    if (y + 1 + height >= maxY)
        return false;

    // Ground at (x,y,z) must be Dirt(7) or Grass(8): (type-7) < 2
    int gidx = grid.CoordToIndex(x, y, z);
    if (gidx < 0)
        return false;
    unsigned char gt = grid.cellTypes[gidx];
    if ((unsigned)(gt - 7) >= 2)
        return false;

    // Clearance: y from y+1 to y+height-3 (exclusive end), XZ in [x-1,x+1]×[z-1,z+1]
    // Binary: local_50 = y+1; end = y - 3 + height; while local_50 < end
    int y0 = y + 1;
    int y1 = y - 3 + height;
    for (int yy = y0; yy < y1; ++yy) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (CellBlocksTree(grid, x + dx, yy, z + dz))
                    return false;
            }
        }
    }
    return true;
}

void BuildTree(GridData& grid, int x, int y, int z, int height, int seedBits, bool variant) {
    // Binary @ 0xea610
    if (height < 5)
        height = 5;

    if (y + 1 + height >= (int)grid.worldMax.y)
        return;

    // wood = (-!variant & 0x0C) + 4  → oak=4, birch=16
    const unsigned char wood = static_cast<unsigned char>((variant ? 0x0C : 0x00) + 4);
    // Leaves always type 10 in binary BuildTree
    const unsigned char leaves = BlockId::Leaves1;

    // Ground cell → Dirt(7)
    PutCell(grid, x, y, z, BlockId::Dirt, 0);

    // Trunk: y+1 .. y+height-4 inclusive (loop i from y+1 to y+height-3 exclusive)
    int trunkEnd = y + height - 3; // exclusive
    for (int yy = y + 1; yy < trunkEnd; ++yy)
        PutCell(grid, x, yy, z, wood, 0);

    // Canopy: 4 layers starting at y+height-3
    int layerY0 = y + height - 3;
    unsigned bits = static_cast<unsigned>(seedBits);

    for (int layer = 0; layer < 4; ++layer) {
        int yy = layerY0 + layer;
        for (int row = 0; row < 5; ++row) {
            int zz = z - 2 + row;
            for (int col = 0; col < 5; ++col) {
                int xx = x - 2 + col;
                unsigned char pat = kTreePattern[layer][row][col];
                if (pat == 0)
                    continue;

                if (pat == 4) {
                    // Branch — wood, no air check in binary when in-bounds
                    PutCell(grid, xx, yy, zz, wood, 0);
                    continue;
                }

                if (pat == 20) {
                    // Sparse leaf: (seedBits & 3)==0 → place and >>=2; else >>=1 skip
                    if ((bits & 3u) == 0) {
                        bits >>= 2;
                        int idx = grid.CoordToIndex(xx, yy, zz);
                        if (idx >= 0 && grid.cellTypes[idx] == BlockId::Air)
                            PutCell(grid, xx, yy, zz, leaves, 0);
                    } else {
                        bits >>= 1;
                    }
                    continue;
                }

                // Any other non-zero (including 10): leaves if air
                int idx = grid.CoordToIndex(xx, yy, zz);
                if (idx >= 0 && grid.cellTypes[idx] == BlockId::Air)
                    PutCell(grid, xx, yy, zz, leaves, 0);
            }
        }
    }
}
