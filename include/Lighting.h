#pragma once
#include "GridData.h"
#include <vector>
#include <cstdint>

// Lighting — binary addresses:
//   GetLight 0x94480, GetLightInside 0x94400, GetLightColor 0x947e0
//   UpdateLightMap 0x94920, UpdateSunRays, AddLight / AddLightUpdate
//
// Packing (GetLightColor @ 0x947e0):
//   bits[0:3]  sky nibble
//   bits[4:7]  block nibble
//   bits[11:13] mode (0 / 0x1000 / 0x1800 / 0x2000 / 0x2800)
// Color = lightTable[(sky<<4)|block] then per-channel scale by mode:
//   0 / 0x1800 → *0xA4/256,  0x1000/0x2800 → *0xD2/256,  0x2000 → *0x94/256
//
// UpdateLightMap @ 0x94920 rebuilds 16×16 ARGB table from GridData+0x1f4 brightness
// when it differs from +0x1f8. Face-mode OR applied in GetLight by face index.

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
    void UpdateSunRaysLocal(GridData& grid, int minX, int maxX, int minZ, int maxZ);
    void PropagateBlockLights(GridData& grid);
    void AddLight(int x, int y, int z, uint16_t packed);
    void Invalidate() { dirty_ = true; }
    bool Empty() const { return data_.empty(); }

    // Binary GridData+0x1f4 brightness (ctor default 1.0). Drives UpdateLightMap.
    void SetBrightness(float b);
    float GetBrightness() const { return brightness_; }
    // Binary UpdateLightMap @ 0x94920 — rebuild 16×16 table if brightness changed.
    void UpdateLightMap();

    uint16_t GetLight(int x, int y, int z) const;
    uint16_t GetFaceLight(int x, int y, int z, int face) const;
    // Binary GetLightColor — table lookup + mode scale (needs UpdateLightMap first).
    uint32_t GetLightColor(uint16_t packed) const;

    // True after brightness/table change — caller should remesh visible geometry.
    bool ConsumeTableDirty() {
        bool d = tableDirty_;
        tableDirty_ = false;
        return d;
    }

private:
    std::vector<uint16_t> data_;
    // 16×16 ARGB8888 light LUT (binary DDSFile 16×16 at GridData+0x1fc / +0x214).
    uint32_t colorTable_[256]{};
    float brightness_ = 1.f;      // +0x1f4
    float brightnessApplied_ = -1.f; // +0x1f8
    // Per-index curves at GridData +0xf4 / +0x134 / +0x174 / +0x1b4 (16 floats).
    // Not found initialized in ctor; use linear 0..15/15 so table is well-defined.
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
};
