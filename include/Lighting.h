#pragma once
#include "GridData.h"
#include <vector>
#include <cstdint>
#include <queue>

// =============================================================================
// INTENTIONAL DEVIATION (self-made): light *propagation* is MCPE / classic-style
// dual-channel flood-fill (sky + block, levels 0..15), not a 1:1 port of
// GridData::AddLightUpdate / SetAddLights / hierarchy maps from the original .so.
//
// Kept compatible with original-facing project interfaces so later RE work is not blocked:
//   - packing: sky[0:3] | block[4:7] | mode[11:13]  (GetLightColor scales unchanged)
//   - face modes from binary GetLight (XP=0 … ZN=0x2800) via GetFaceLight
//   - Material L: >0 emit (block channel), <0 attenuate both channels (leaves/water/ice)
//   - UpdateLightMap brightness LUT (day/night) — does NOT re-propagate light
//   - mesher samples GetFaceLight + GetLightColor only
//
// Original binary addresses (reference only): GetLight 0x94480, GetLightColor 0x947e0,
// UpdateLightMap 0x94920, AddLightUpdate 0x94b60, …
// =============================================================================

namespace LightPack {
    static constexpr uint16_t MODE_MASK = 0x3800;
    static constexpr uint16_t MODE_DEFAULT = 0x0000;
    static constexpr uint16_t MODE_0800 = 0x0800; // YP
    static constexpr uint16_t MODE_1000 = 0x1000; // ZP
    static constexpr uint16_t MODE_1800 = 0x1800; // XN
    static constexpr uint16_t MODE_2000 = 0x2000; // YN
    static constexpr uint16_t MODE_2800 = 0x2800; // ZN

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

class LightMap {
public:
    void Init(GridData& grid);

    // Full rebuild (world load / rare invalidation).
    void PropagateBlockLights(GridData& grid);
    // Local rebuild after place/break — AABB expanded by light radius 15.
    void PropagateBlockLightsLocal(GridData& grid, int minX, int maxX, int minY, int maxY,
                                   int minZ, int maxZ);

    // Compatibility stubs used by older call sites / docs (map onto MCPE path).
    void UpdateSunRays(GridData& grid);
    void UpdateSunRaysLocal(GridData& grid, int minX, int maxX, int minZ, int maxZ);
    void InvalidateSunRays(int worldX, int worldZ);

    void AddLight(int x, int y, int z, uint16_t packed);

    void Invalidate() { dirty_ = true; }
    bool Empty() const { return data_.empty(); }

    void SetBrightness(float b);
    float GetBrightness() const { return brightness_; }
    void UpdateLightMap();

    uint16_t GetLight(int x, int y, int z) const;
    uint16_t GetFaceLight(int x, int y, int z, int face) const;
    uint32_t GetLightColor(uint16_t packed) const;

    bool ConsumeTableDirty() {
        bool d = tableDirty_;
        tableDirty_ = false;
        return d;
    }

private:
    std::vector<uint16_t> data_;
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
    void InitDefaultCurves();

    // MCPE-style internal
    void RebuildRegion(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1);
    void PropagateSky(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1);
    void PropagateBlock(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1);
};
