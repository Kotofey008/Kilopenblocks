#pragma once
#include "GridData.h"
#include <vector>
#include <cstdint>
#include <queue>

// =============================================================================
// INTENTIONAL DEVIATION: NeoMCPE-style dual-channel LightUpdate queue adapted to
// Lite (Material L, fluid state, packing). Not 1:1 binary AddLightUpdate.
//
// Public surface stays original-compatible for mesher / RE:
//   packing sky|block|mode, GetFaceLight modes, GetLightColor, Material L
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
        if (v <= 0) return 0;
        if (v >= 15) return 15;
        return v;
    }
    inline int SaturateSub(int light, int amount) {
        if (light <= 0) return 0;
        if (amount <= 0) return Clamp15(light);
        if (amount >= light) return 0;
        return Clamp15(light - amount);
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

namespace LightRules {
    int SignedL(const Material* m);
    int Opacity(const GridData& grid, unsigned char type, unsigned short state);
    int OpacityAt(const GridData& grid, int wx, int wy, int wz);
    int Emission(const GridData& grid, unsigned char type);
    int EmissionAt(const GridData& grid, int wx, int wy, int wz);
    bool BlocksSkyHeight(const GridData& grid, unsigned char type, unsigned short state);
}

// NeoMCPE LightLayer adapted
enum class LightLayer : uint8_t { Sky = 0, Block = 1 };

// NeoMCPE LightUpdate AABB (inclusive world coords)
struct LightUpdateBox {
    LightLayer layer = LightLayer::Sky;
    int x0 = 0, y0 = 0, z0 = 0;
    int x1 = 0, y1 = 0, z1 = 0;
    bool expandToContain(int _x0, int _y0, int _z0, int _x1, int _y1, int _z1);
};

class LightMap {
public:
    void Init(GridData& grid);

    // Full rebuild (world load). Also seeds queue cleared.
    void PropagateBlockLights(GridData& grid);
    // Legacy entry: enqueues sky+block for AABB (processed by ProcessLightQueue).
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

    // Step 2: heightmap
    void RebuildHeightmap(GridData& grid);
    void RefreshColumnSky(GridData& grid, int worldOx, int worldOz, int colsX = 1, int colsZ = 1);
    void RecalcHeightColumn(GridData& grid, int lx, int lz);
    int GetSkyHeightLocal(int lx, int lz) const;
    int GetSkyHeightWorld(int wx, int wz) const;
    bool IsSkyLit(int wx, int wy, int wz) const;

    // Step 3–5: LightUpdate queue (NeoMCPE-style)
    // Enqueue AABB (world inclusive). Merges into existing box when possible.
    void Enqueue(LightLayer layer, int x0, int y0, int z0, int x1, int y1, int z1);
    // After SetCell: height recalc + enqueue sky/block around the edit.
    void OnBlockChange(GridData& grid, int wx, int wy, int wz, int emitRadius);
    // Drain queue, maxCells evaluations per call. Marks MarkRegionDirty only when
    // a cell's sky/block nibble actually changes.
    int ProcessLightQueue(GridData& grid, int maxCells);
    bool HasPendingLight() const { return !updates_.empty(); }

private:
    std::vector<uint16_t> data_;
    std::vector<uint16_t> skyHeight_;
    std::vector<LightUpdateBox> updates_;
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

    void SetSky(int wx, int wy, int wz, int sky);
    void SetBlock(int wx, int wy, int wz, int block);
    int GetSkyAt(int wx, int wy, int wz) const;
    int GetBlockAt(int wx, int wy, int wz) const;

    // Run one LightUpdate box; returns cells visited. May enqueue more.
    int RunUpdate(GridData& grid, const LightUpdateBox& box, int cellBudget);

    // Immediate column sky (init / full rebuild) — no queue.
    void PropagateSky(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1);
    void PropagateBlock(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1);
};
