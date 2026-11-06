#pragma once
#include "GridData.h"
#include <vector>
#include <cstdint>

// Lighting — binary addresses:
//   GetLight 0x94480 (860B), GetLightInside 0x94400, GetLightColor 0x947e0 (318B)
//   UpdateLightMap 0x94920, SetAddLights 0x92cb0 (1837B)
//   AddLight 0x95b90 (2977B), AddLightUpdate 0x94b60 (4132B), AddLights 0x96740
// Packing (GetLightColor verified):
//   bits[0:3]=nibbleA, [4:7]=nibbleB, [11:13]=mode (0x1800/0x2000/0x2800)
//   color = table@GridData+0x214[(A<<4)|B] scaled by mode
// Hypothesis: A=sky B=block (matches AddLight 0x0f mask). Mode bits NOT in current Make().
// Torch emission ~14 (AddTorch path). Hierarchy maps +0x280.. used in AddLight.

// Light value packing (GetLightColor @ 0x947e0 verified):
//   bits[0:3]  nibble A (hyp: sky)   mask 0x000f
//   bits[4:7]  nibble B (hyp: block) mask 0x00f0
//   bits[11:13] mode                 mask 0x3800
// Mode values and RGB scale factors (from GetLightColor imul paths):
//   0x0000 / 0x1800 → scale 0xA4/256 ≈ 0.641  (day/default)
//   0x1000          → alternate path
//   0x2000          → dedicated branch
//   0x2800          → scale 0xD2/256 ≈ 0.820
// Color = table[GridData+0x214][(A<<4)|B] then per-channel imul scale.
// Mode bits are set by light system; Make() stores A/B only until mode
// assignment sites in AddLight/SetAddLights are fully recovered.
namespace LightPack {
    static constexpr uint16_t MODE_MASK = 0x3800;
    static constexpr uint16_t MODE_DEFAULT = 0x0000;
    static constexpr uint16_t MODE_1800 = 0x1800;
    static constexpr uint16_t MODE_1000 = 0x1000;
    static constexpr uint16_t MODE_2000 = 0x2000;
    static constexpr uint16_t MODE_2800 = 0x2800;

    inline uint16_t Make(int sky, int block, uint16_t mode = MODE_DEFAULT) {
        sky = sky < 0 ? 0 : (sky > 15 ? 15 : sky);
        block = block < 0 ? 0 : (block > 15 ? 15 : block);
        return (uint16_t)((sky & 0xf) | ((block & 0xf) << 4) | (mode & MODE_MASK));
    }
    inline int Sky(uint16_t v) { return v & 0xf; }
    inline int Block(uint16_t v) { return (v >> 4) & 0xf; }
    inline uint16_t Mode(uint16_t v) { return (uint16_t)(v & MODE_MASK); }
}

class LightMap {
public:
    void Init(GridData& grid);
    void UpdateSunRays(GridData& grid);
    // Cheap local sky-light refresh for columns covering [minX..maxX]×[minZ..maxZ]
    // (world coords). Used after place/break so vertex colors stay correct without
    // a full O(world) Init. Block-light channel is preserved.
    void UpdateSunRaysLocal(GridData& grid, int minX, int maxX, int minZ, int maxZ);
    void PropagateBlockLights(GridData& grid);
    void AddLight(int x, int y, int z, uint16_t packed);
    void Invalidate() { dirty_ = true; }
    bool Empty() const { return data_.empty(); }

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
