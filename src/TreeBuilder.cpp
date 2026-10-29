#include "TreeBuilder.h"
#include <algorithm>
#include <cstdlib>

bool CheckTreePosition(GridData& grid, int x, int y, int z, int height) {
    // Observed: if height <= 4, force to 5
    if (height <= 4) height = 5;
    // Must sit on solid ground and have clear air column + canopy space
    int ground = y - 1;
    int gidx = grid.CoordToIndex(x, ground, z);
    if (gidx < 0) return false;
    unsigned char gt = grid.cellTypes[gidx];
    if (gt != BlockId::Grass && gt != BlockId::Dirt && gt != BlockId::Stone)
        return false;

    // Clear vertical space for trunk + a bit of canopy
    for (int dy = 0; dy < height + 3; ++dy) {
        int idx = grid.CoordToIndex(x, y + dy, z);
        if (idx < 0) return false;
        if (grid.cellTypes[idx] != BlockId::Air) return false;
    }
    return true;
}

void BuildTree(GridData& grid, int x, int y, int z, int height, int woodType, bool /*variant*/) {
    // Clamp height to observed range [5, 15]
    if (height < 5) height = 5;
    if (height > 15) height = 15;

    unsigned char wood = (woodType == BlockId::BirchWood) ? BlockId::BirchWood : BlockId::OakWood;
    unsigned char leaves = (wood == BlockId::BirchWood) ? BlockId::Leaves2 : BlockId::Leaves1;

    // Trunk
    for (int dy = 0; dy < height; ++dy)
        grid.SetCell(x, y + dy, z, wood, 0);

    // Canopy: roughly spherical / cross layers near the top (standard voxel tree)
    int top = y + height - 1;
    for (int dy = -2; dy <= 2; ++dy) {
        int radius = (std::abs(dy) >= 2) ? 1 : 2;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                if (dx == 0 && dz == 0 && dy <= 0) continue; // trunk occupies center lower
                if (std::abs(dx) == radius && std::abs(dz) == radius && radius > 1)
                    continue; // soft corners
                int lx = x + dx, ly = top + dy, lz = z + dz;
                int idx = grid.CoordToIndex(lx, ly, lz);
                if (idx < 0) continue;
                if (grid.cellTypes[idx] == BlockId::Air)
                    grid.SetCell(lx, ly, lz, leaves, 0);
            }
        }
    }
}
