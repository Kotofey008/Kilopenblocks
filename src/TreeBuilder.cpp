#include "TreeBuilder.h"
#include <algorithm>
#include <cstdlib>

static inline void PutCell(GridData& grid, int x, int y, int z,
                           unsigned char type, unsigned short state = 0) {
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0) return;
    grid.cellTypes[idx] = type;
    if (idx < (int)grid.cellStates.size())
        grid.cellStates[idx] = state;
}

bool CheckTreePosition(GridData& grid, int x, int y, int z, int height) {
    if (height <= 4) height = 5;
    int ground = y - 1;
    int gidx = grid.CoordToIndex(x, ground, z);
    if (gidx < 0) return false;
    unsigned char gt = grid.cellTypes[gidx];
    if (gt != BlockId::Grass && gt != BlockId::Dirt && gt != BlockId::Stone)
        return false;

    for (int dy = 0; dy < height + 3; ++dy) {
        int idx = grid.CoordToIndex(x, y + dy, z);
        if (idx < 0) return false;
        if (grid.cellTypes[idx] != BlockId::Air) return false;
    }
    return true;
}

void BuildTree(GridData& grid, int x, int y, int z, int height, int woodType, bool /*variant*/) {
    if (height < 5) height = 5;
    if (height > 15) height = 15;

    unsigned char wood = (woodType == BlockId::BirchWood) ? BlockId::BirchWood : BlockId::OakWood;
    unsigned char leaves = (wood == BlockId::BirchWood) ? BlockId::Leaves2 : BlockId::Leaves1;

    for (int dy = 0; dy < height; ++dy)
        PutCell(grid, x, y + dy, z, wood, 0);

    int top = y + height - 1;
    for (int dy = -2; dy <= 2; ++dy) {
        int radius = (std::abs(dy) >= 2) ? 1 : 2;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                if (dx == 0 && dz == 0 && dy <= 0) continue;
                if (std::abs(dx) == radius && std::abs(dz) == radius && radius > 1)
                    continue;
                int lx = x + dx, ly = top + dy, lz = z + dz;
                int idx = grid.CoordToIndex(lx, ly, lz);
                if (idx < 0) continue;
                if (grid.cellTypes[idx] == BlockId::Air)
                    PutCell(grid, lx, ly, lz, leaves, 0);
            }
        }
    }
}
