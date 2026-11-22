#include "TerrainGenerator.h"
#include "TreeBuilder.h"
#include "Noise.h"
#include <cmath>
#include <algorithm>
#include <cstring>

// LCG fully recovered: mul 0x343FD, add 0x269EC3
static constexpr uint32_t kLcgMul = 0x343FD;
static constexpr uint32_t kLcgAdd = 0x269EC3;

// Bulk cell write — NO SetCell side effects (hierarchy / mesh dirty / neighbours).
// Binary terrain paths use FillCells / direct buffer writes for the same reason.
static inline void PutCell(GridData& grid, int x, int y, int z,
                           unsigned char type, unsigned short state = 0) {
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0) return;
    grid.cellTypes[idx] = type;
    if (idx < (int)grid.cellStates.size())
        grid.cellStates[idx] = state;
}

TerrainGeneratorEarth::TerrainGeneratorEarth() {}

uint32_t TerrainGeneratorEarth::NextRand() {
    rng_state_ = rng_state_ * kLcgMul + kLcgAdd;
    return (rng_state_ >> 16) & 0x7FFF;
}

void TerrainGeneratorEarth::Init(GridData& grid, int64_t seed) {
    seed_ = seed;
    rng_state_ = static_cast<uint32_t>(seed);
    grid.InitDimensionsFromWorldBounds();
}

void TerrainGeneratorEarth::Generate(GridData& grid, int x0, int z0, int x1, int z1) {
    auto alignDown = [](int v) { return v & ~15; };
    auto alignUp   = [](int v) { return (v + 15) & ~15; };
    int ax0 = alignDown(x0), az0 = alignDown(z0);
    int ax1 = alignUp(x1),   az1 = alignUp(z1);

    GenerateTerrainShape(grid, ax0, az0, ax1, az1);
    AddGrassAndWater(grid, ax0, az0, ax1, az1);

    // Scatter trees (density ~1 per 12x12 cells)
    for (int z = az0; z < az1; z += 4) {
        for (int x = ax0; x < ax1; x += 4) {
            if ((NextRand() % 12) == 0) {
                int tx = x + (int)(NextRand() % 4);
                int tz = z + (int)(NextRand() % 4);
                AddTree(grid, tx, tz);
            }
        }
    }
}

void TerrainGeneratorEarth::GenerateTerrainShape(GridData& grid, int x0, int z0, int x1, int z1) {
    Random rng(static_cast<uint32_t>(seed_));
    Noise noise;
    noise.Init(rng, 4, 0.02f, 24.f, 0.5f);

    const int yMin = (int)grid.worldMin.y;
    const int yMax = (int)grid.worldMax.y - 2;

    for (int z = z0; z < z1; z += kRegionSize) {
        for (int x = x0; x < x1; x += kRegionSize) {
            for (int lz = 0; lz < kRegionSize; ++lz) {
                for (int lx = 0; lx < kRegionSize; ++lx) {
                    int wx = x + lx, wz = z + lz;
                    float n = noise.GetValue((float)wx, (float)wz);
                    int h = 48 + (int)n;
                    if (h < yMin + 1) h = yMin + 1;
                    if (h > yMax) h = yMax;
                    // Column fill — direct writes (was SetCell → multi-second gen)
                    for (int y = yMin; y < h; ++y) {
                        unsigned char type = BlockId::Stone;
                        if (y >= h - 4 && y < h - 1) type = BlockId::Dirt;
                        if (y == h - 1) type = BlockId::Grass;
                        PutCell(grid, wx, y, wz, type, 0);
                    }
                }
            }
        }
    }
}

void TerrainGeneratorEarth::Slice(float* out, int x, int z, int w) {
    if (!out || w <= 0) return;
    Random rng(static_cast<uint32_t>(seed_));
    Noise noise;
    noise.Init(rng, 4, 0.02f, 24.f, 0.5f);
    for (int i = 0; i < w; ++i)
        out[i] = 48.f + noise.GetValue((float)(x + i), (float)z);
}

void TerrainGeneratorEarth::AddGrassAndWater(GridData& grid, int x0, int z0, int x1, int z1) {
    const int waterLevel = 40;
    const int lavaLevel = 28;
    // One Noise instance for the whole pass (GetGroundLevel used to re-Init per column).
    Random rng(static_cast<uint32_t>(seed_));
    Noise noise;
    noise.Init(rng, 4, 0.02f, 24.f, 0.5f);
    for (int z = z0; z < z1; ++z) {
        for (int x = x0; x < x1; ++x) {
            int ground = 48 + (int)noise.GetValue((float)x, (float)z);
            if (ground < waterLevel) {
                for (int y = ground; y < waterLevel; ++y)
                    PutCell(grid, x, y, z, /*Air*/0, GridData::WATER_FILL_STATE);
            }
            if (ground < lavaLevel - 4) {
                int top = ground + 3;
                if (top > lavaLevel) top = lavaLevel;
                for (int y = ground; y < top; ++y)
                    PutCell(grid, x, y, z, /*Air*/0, GridData::LAVA_SOURCE_STATE);
            }
        }
    }
}

void TerrainGeneratorEarth::AddTree(GridData& grid, int x, int z) {
    int y = GetGroundLevel(x, z);
    int h = 5 + (int)(NextRand() % 7);
    unsigned char wood = (NextRand() & 1) ? BlockId::BirchWood : BlockId::OakWood;
    if (CheckTreePosition(grid, x, y, z, h))
        BuildTree(grid, x, y, z, h, wood, false);
}

void TerrainGeneratorEarth::AddFlowers(GridData& grid, int x, int z, unsigned char) {
    int y = GetGroundLevel(x, z);
    if (y > (int)grid.worldMin.y)
        PutCell(grid, x, y, z, BlockId::Flower1, 0);
}

void TerrainGeneratorEarth::AddMushrooms(GridData& grid, int x, int z, unsigned char) {
    int y = GetGroundLevel(x, z);
    if (y > (int)grid.worldMin.y)
        PutCell(grid, x, y, z, BlockId::Mushroom1, 0);
}

void TerrainGeneratorEarth::AddLiquidSource(GridData& grid, int x, int y, int z, int liquidType) {
    if (liquidType == 0)
        PutCell(grid, x, y, z, /*Air*/0, GridData::WATER_SOURCE_STATE);
    else
        PutCell(grid, x, y, z, /*Air*/0, GridData::LAVA_SOURCE_STATE);
}

void TerrainGeneratorEarth::FindStartPosition(int, int) {}

int TerrainGeneratorEarth::GetGroundLevel(int x, int z) {
    Random rng(static_cast<uint32_t>(seed_));
    Noise noise;
    noise.Init(rng, 4, 0.02f, 24.f, 0.5f);
    return 48 + (int)noise.GetValue((float)x, (float)z);
}

void TerrainGeneratorEarth::GetStartPosition(int& x, int& y, int& z) {
    x = 0; y = GetGroundLevel(0, 0) + 2; z = 0;
}

int TerrainGeneratorEarth::GetPositionCost(int, int, int) { return 0; }

void TerrainGeneratorFlat::Init(GridData& grid, int64_t) {
    grid.InitDimensionsFromWorldBounds();
}

void TerrainGeneratorFlat::Generate(GridData& grid, int x0, int z0, int x1, int z1) {
    int ground = (int)grid.worldMin.y + 32;
    if (ground >= (int)grid.worldMax.y) ground = (int)grid.worldMax.y - 2;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            for (int y = (int)grid.worldMin.y; y < ground; ++y)
                PutCell(grid, x, y, z, BlockId::Stone, 0);
            PutCell(grid, x, ground, z, BlockId::Grass, 0);
        }
}

int TerrainGeneratorFlat::GetGroundLevel(int, int) {
    return 32;
}

void TerrainGeneratorFlat::GetStartPosition(int& x, int& y, int& z) {
    x = 0; y = 34; z = 0;
}
