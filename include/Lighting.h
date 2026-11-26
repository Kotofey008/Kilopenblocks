#pragma once
#include "GridData.h"
#include <vector>
#include <cstdint>
#include <queue>

// Lighting — binary addresses (x86, rizin):
//   GetLight 0x94480, GetLightInside 0x94400, GetLightColor 0x947e0
//   UpdateLightMap 0x94920, UpdateSunRays 0x92440 / 0x92bf0
//   InvalidateSunRays 0x92c50, SetAddLights 0x92cb0
//   AddLightUpdate 0x94b60, AddLight 0x95b90, AddLights 0x96740
//   InvalidateLighting 0x898f0
//
// GridData light layout (binary):
//   +0x7c  ushort* light[sky|block]   +0x88 count
//   +0x68 / +0x6c  sky height byte maps (column)
//   +0x84  sun-ray dirty per region column
//   +0x280 hierarchy ushort |0x8000 dirty
//   +0x288/+0x290/+0x298/+0x2a0  4-level dirty counters
//   +0x218/+0x21c/+0x220  AddLights queue (count / cap / entries)
//   +0xf0  batchFlag
//
// Packing (GetLightColor @ 0x947e0):
//   bits[0:3] sky  bits[4:7] block  bits[11:13] mode 0x3800
//   scale: 0/0x1800→0xA4  0x1000/0x2800→0xD2  0x2000→0x94

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

class LightMap {
public:
    void Init(GridData& grid);

    // Binary UpdateSunRays — builds sky height map (+0x68 family) + fills sky nibble.
    void UpdateSunRays(GridData& grid);
    void UpdateSunRaysLocal(GridData& grid, int minX, int maxX, int minZ, int maxZ);
    // Binary InvalidateSunRays(rx,rz) — mark 16×16 column sky height dirty.
    void InvalidateSunRays(int worldX, int worldZ);

    // Binary SetAddLights(rx,rz) — seed queue for one region column from height map.
    void SetAddLights(GridData& grid, int regionX, int regionZ);

    // Binary AddLightUpdate(x,y,z,linearIndex) — recompute one cell from neighbours.
    // Returns true if stored value changed (neighbours should be queued).
    bool AddLightUpdate(GridData& grid, int x, int y, int z);

    // Binary AddLight(x,y,z,ushort) — seed block channel + mark hierarchy dirty.
    void AddLight(GridData& grid, int x, int y, int z, uint16_t packed);

    // Binary AddLights() — drain hierarchy/queue. maxSteps mirrors per-frame budget.
    void AddLights(GridData& grid, int maxSteps = 4096);

    // Full rebuild (InvalidateLighting path): zero, UpdateSunRays all, SetAddLights all, AddLights.
    void PropagateBlockLights(GridData& grid);
    // Local: InvalidateSunRays columns in AABB, UpdateSunRaysLocal, SetAddLights, AddLights.
    void PropagateBlockLightsLocal(GridData& grid, int minX, int maxX, int minY, int maxY,
                                   int minZ, int maxZ);

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

    // Sky height for column (world xz → first opaque y from top, or sizeY if open).
    int GetSkyHeight(int wx, int wz) const;

private:
    std::vector<uint16_t> data_;
    // Binary +0x68: per-XZ column sky height (y of first opaque from top, or sizeY).
    std::vector<uint8_t> skyHeight_;
    // Binary +0x84: sun-ray column dirty (1 = needs UpdateSunRays).
    std::vector<uint8_t> sunRayDirty_;
    // Binary +0x21c/+0x220 queue: linear lightmap indices pending AddLightUpdate.
    std::queue<int> lightQueue_;
    std::vector<uint8_t> inQueue_;

    uint32_t colorTable_[256]{};
    float brightness_ = 1.f;
    float brightnessApplied_ = -1.f;
    float curveR_[16]{};
    float curveG_[16]{};
    float curveB_[16]{};
    float curveRow_[16]{};
    int sizeX_ = 0, sizeY_ = 0, sizeZ_ = 0;
    int ox_ = 0, oy_ = 0, oz_ = 0;
    int regionCountX_ = 0, regionCountZ_ = 0;
    bool dirty_ = true;
    bool tableDirty_ = true;

    int Index(int x, int y, int z) const;
    int XZIndex(int lx, int lz) const { return lx + sizeX_ * lz; }
    int RegionIndex(int rx, int rz) const { return rx + regionCountX_ * rz; }
    void InitDefaultCurves();
    void EnsureAuxSize();
    void Enqueue(int lightIdx);
    void UpdateSunRaysColumn(GridData& grid, int lx, int lz);
};
