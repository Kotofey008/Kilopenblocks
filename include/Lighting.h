#pragma once
#include "GridData.h"
#include <vector>
#include <cstdint>

// Lighting recovered from:
//   GetLight / GetLightColor / GetLightInside
//   UpdateSunRays / UpdateLightMap / InvalidateLighting
//   AddLight(x,y,z,ushort) / AddLightUpdate / RemoveLight
//   AddTorch(x,y,z, Material)
// Packing: sky[0:3] + block[4:7]; torch emission ~14

namespace LightPack {
    inline uint16_t Make(int sky, int block) {
        sky = sky < 0 ? 0 : (sky > 15 ? 15 : sky);
        block = block < 0 ? 0 : (block > 15 ? 15 : block);
        return (uint16_t)((sky & 0xf) | ((block & 0xf) << 4));
    }
    inline int Sky(uint16_t v) { return v & 0xf; }
    inline int Block(uint16_t v) { return (v >> 4) & 0xf; }
}

class LightMap {
public:
    void Init(GridData& grid);
    void UpdateSunRays(GridData& grid);
    void PropagateBlockLights(GridData& grid);
    void AddLight(int x, int y, int z, uint16_t packed);
    void Invalidate() { dirty_ = true; }

    uint16_t GetLight(int x, int y, int z) const;
    uint16_t GetFaceLight(int x, int y, int z, int face) const;
    static uint32_t GetLightColor(uint16_t packed);

private:
    std::vector<uint16_t> data_;
    int sizeX_ = 0, sizeY_ = 0, sizeZ_ = 0;
    int ox_ = 0, oy_ = 0, oz_ = 0;
    bool dirty_ = true;
    int Index(int x, int y, int z) const;
};
