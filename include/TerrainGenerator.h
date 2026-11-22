#pragma once
#include "GridData.h"
#include <cstdint>

class TerrainGenerator {
public:
    virtual ~TerrainGenerator() = default;
    virtual void Init(GridData& grid, int64_t seed) = 0;
    virtual void Generate(GridData& grid, int x0, int z0, int x1, int z1) = 0;
    virtual int GetGroundLevel(int x, int z) = 0;
    virtual void GetStartPosition(int& x, int& y, int& z) = 0;
};

class TerrainGeneratorEarth : public TerrainGenerator {
public:
    TerrainGeneratorEarth();
    void Init(GridData& grid, int64_t seed) override;
    void Generate(GridData& grid, int x0, int z0, int x1, int z1) override;
    int GetGroundLevel(int x, int z) override;
    void GetStartPosition(int& x, int& y, int& z) override;

    void GenerateTerrainShape(GridData& grid, int x0, int z0, int x1, int z1);
    void Slice(float* out, int x, int z, int w);
    void AddGrassAndWater(GridData& grid, int x0, int z0, int x1, int z1);
    void AddTree(GridData& grid, int x, int z);
    void AddFlowers(GridData& grid, int x, int z, unsigned char variant);
    void AddMushrooms(GridData& grid, int x, int z, unsigned char variant);
    void AddLiquidSource(GridData& grid, int x, int y, int z, int liquidType);
    int  GetPositionCost(int x, int y, int z);
    void FindStartPosition(int a, int b);

private:
    int64_t seed_ = 0;
    uint32_t rng_state_ = 0;
    uint32_t NextRand();
    static constexpr int kRegionSize = 16;
};

class TerrainGeneratorFlat : public TerrainGenerator {
public:
    void Init(GridData& grid, int64_t seed) override;
    void Generate(GridData& grid, int x0, int z0, int x1, int z1) override;
    int GetGroundLevel(int x, int z) override;
    void GetStartPosition(int& x, int& y, int& z) override;
};
