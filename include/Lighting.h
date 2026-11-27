#pragma once
#include "GridData.h"
#include <vector>
#include <cstdint>
#include <queue>

// =============================================================================
// INTENTIONAL DEVIATION: light *propagation* borrows NeoMCPE / classic dual-channel
// mechanics (opacity step, emission, heightmap / isSkyLit) adapted to Exploration
// Lite materials, fluids, and packing — not a 1:1 port of binary AddLightUpdate.
//
// Kept original-compatible for mesher + future RE:
//   - packing sky[0:3]|block[4:7]|mode[11:13]
//   - GetFaceLight face modes (binary map)
//   - GetLightColor LUT + mode scales
//   - Material L: >0 emission, <0 attenuation (opacity step)
// =============================================================================

namespace LightPack {
    static constexpr uint16_t MODE_MASK = 0x3800;
    static constexpr uint16_t MODE_DEFAULT = 0x0000;
    static constexpr uint16_t MODE_0800 = 0x0800;
    static constexpr uint16_t MODE_1000 = 0x1000;
    static constexpr uint16_t MODE_1800 = 0x1800;
    static constexpr uint16_t MODE_2000 = 0x2000;
    static constexpr uint16_t MODE_2800 = 0x2800;

    inline int Clamp15(int v) {
        if (v < 0) return 0;
        if (v > 15) return 15;
        return v;
    }
    inline uint16_t Make(int sky, int block, uint16_t mode = MODE_DEFAULT) {
        sky = Clamp15(sky);
        block = Clamp15(block);
        return (uint16_t)((sky & 0xf) | ((block & 0xf) << 4) | (mode & MODE_MASK));
    }
    inline int Sky(uint16_t v) { return v & 0xf; }
    inline int Block(uint16_t v) { return (v >> 4) & 0xf; }
    inline uint16_t Mode(uint16_t v) { return (uint16_t)(v & MODE_MASK); }
}

// ---------------------------------------------------------------------------
// Step 1: opacity / emission from Lite Material + fluid state
// (NeoMCPE lightBlock / lightEmission adapted — not tile-id tables)
// ---------------------------------------------------------------------------
namespace LightRules {
    // Signed L from material byte +0x1a (binary MtlDesc L field).
    int SignedL(const Material* m);

    // NeoMCPE lightBlock equivalent [0..15]: light loss when entering this cell.
    // 0  = transparent (air); formula treats 0 as 1 step when spreading.
    // 15 = fully blocks (solid).
    // Leaves/ice: -L; water fluid state: 2–3.
    int Opacity(const GridData& grid, unsigned char type, unsigned short state);
    int OpacityAt(const GridData& grid, int wx, int wy, int wz);

    // NeoMCPE lightEmission: Material L > 0.
    int Emission(const GridData& grid, unsigned char type);
    int EmissionAt(const GridData& grid, int wx, int wy, int wz);

    // Heightmap stopper: opacity >= 15 (true solid). Leaves/water do not raise heightmap.
    bool BlocksSkyHeight(const GridData& grid, unsigned char type, unsigned short state);
}

class LightMap {
public:
    void Init(GridData& grid);

    void PropagateBlockLights(GridData& grid);
    void PropagateBlockLightsLocal(GridData& grid, int minX, int maxX, int minY, int maxY,
                                   int minZ, int maxZ);

    void UpdateSunRays(GridData& grid);
    void UpdateSunRaysLocal(GridData& grid, int minX, int maxX, int minZ, int maxZ);
    void InvalidateSunRays(int worldX, int worldZ);

    void AddLight(int x, int y, int z, uint16_t packed);

    void Invalidate() { dirty_ = true; }
    bool Empty() const { return data_.empty(); }

    void SetBrightness(float b);
    float GetBrightness() const { return brightness_; }
    float GetDayScale() const {
        float s = 0.25f + 0.75f * brightness_;
        if (s < 0.25f) s = 0.25f;
        if (s > 1.f) s = 1.f;
        return s;
    }
    void UpdateLightMap();

    uint16_t GetLight(int x, int y, int z) const;
    uint16_t GetFaceLight(int x, int y, int z, int face) const;
    uint32_t GetLightColor(uint16_t packed) const;

    bool ConsumeTableDirty() {
        bool d = tableDirty_;
        tableDirty_ = false;
        return d;
    }

    // Step 2: heightmap / isSkyLit (NeoMCPE LevelChunk heightmap adapted)
    // skyHeight_[lx+sizeX*lz] = lowest local Y where cell is sky-lit (ly >= height → lit).
    // Matches MCPE: isSkyLit iff y >= heightmap[xz].
    void RebuildHeightmap(GridData& grid);
    void RecalcHeightColumn(GridData& grid, int lx, int lz);
    int GetSkyHeightLocal(int lx, int lz) const;
    int GetSkyHeightWorld(int wx, int wz) const;
    bool IsSkyLit(int wx, int wy, int wz) const;

private:
    std::vector<uint16_t> data_;
    // Per-XZ heightmap (local Y, 0..sizeY). sizeY means open sky to bottom.
    std::vector<uint16_t> skyHeight_;
    uint32_t colorTable_[256]{};
    float brightness_ = 1.f;
    float brightnessApplied_ = -1.f;
    float curveR_[16]{};
    float curveG_[16]{};
    float curveB_[16]{};
    float curveRow_[16]{};
    int sizeX_ = 0, sizeY_ = 0, sizeZ_ = 0;
    int ox_ = 0, oy_ = 0, oz_ = 0;
    bool dirty_ = true;
    bool tableDirty_ = true;

    int Index(int x, int y, int z) const;
    int XZIndex(int lx, int lz) const { return lx + sizeX_ * lz; }
    void InitDefaultCurves();

    void RebuildRegion(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1);
    void PropagateSky(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1);
    void PropagateBlock(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1);
};
