#include "Lighting.h"
#include "TreeBuilder.h"
#include <algorithm>
#include <cstring>
#include <queue>

int LightMap::Index(int x, int y, int z) const {
    int lx = x - ox_, ly = y - oy_, lz = z - oz_;
    if (lx < 0 || ly < 0 || lz < 0 || lx >= sizeX_ || ly >= sizeY_ || lz >= sizeZ_)
        return -1;
    return lx + sizeX_ * (ly + sizeY_ * lz);
}

void LightMap::Init(GridData& grid) {
    ox_ = grid.originX; oy_ = grid.originY; oz_ = grid.originZ;
    sizeX_ = grid.sizeX > 0 ? grid.sizeX : (int)(grid.worldMax.x - grid.worldMin.x);
    sizeY_ = grid.sizeY > 0 ? grid.sizeY : (int)(grid.worldMax.y - grid.worldMin.y);
    sizeZ_ = grid.sizeZ > 0 ? grid.sizeZ : (int)(grid.worldMax.z - grid.worldMin.z);
    if (sizeX_ <= 0) { ox_ = (int)grid.worldMin.x; sizeX_ = (int)(grid.worldMax.x - grid.worldMin.x); }
    if (sizeY_ <= 0) { oy_ = (int)grid.worldMin.y; sizeY_ = (int)(grid.worldMax.y - grid.worldMin.y); }
    if (sizeZ_ <= 0) { oz_ = (int)grid.worldMin.z; sizeZ_ = (int)(grid.worldMax.z - grid.worldMin.z); }
    data_.assign((size_t)sizeX_ * sizeY_ * sizeZ_, 0);
    dirty_ = true;
    UpdateSunRays(grid);
    PropagateBlockLights(grid);
}

static bool IsOpaque(unsigned char t) {
    if (t == BlockId::Air || t == BlockId::Water) return false;
    if (t == BlockId::Leaves1 || t == BlockId::Leaves2) return false;
    if (t == BlockId::Flower1 || t == BlockId::Mushroom1 || t == BlockId::Mushroom2) return false;
    if (t == 11) return false; // Glass
    if (t == 17 || t == 18) return false; // Torch / Active Torch
    return true;
}

// Emission level for light sources (from BMT/block type evidence)
static int EmissionLevel(unsigned char t) {
    if (t == 18) return 14; // Active Torch (observed AddTorch)
    if (t == 17) return 14; // Torch
    // Lava if present as fluid type with emission — check later
    return 0;
}

void LightMap::UpdateSunRays(GridData& grid) {
    for (int z = 0; z < sizeZ_; ++z) {
        for (int x = 0; x < sizeX_; ++x) {
            int sky = 15;
            for (int y = sizeY_ - 1; y >= 0; --y) {
                int idx = x + sizeX_ * (y + sizeY_ * z);
                int wx = x + ox_, wy = y + oy_, wz = z + oz_;
                int cidx = grid.CoordToIndex(wx, wy, wz);
                unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : BlockId::Air;
                if (IsOpaque(t)) {
                    sky = 0;
                } else if (t == BlockId::Leaves1 || t == BlockId::Leaves2 || t == BlockId::Water) {
                    if (sky > 0) sky -= 1;
                }
                int block = LightPack::Block(data_[idx]);
                data_[idx] = LightPack::Make(sky, block);
            }
        }
    }
    dirty_ = false;
}

void LightMap::AddLight(int x, int y, int z, uint16_t packed) {
    // Matches GridData::AddLight(x,y,z,ushort) — seeds block-light channel
    int i = Index(x, y, z);
    if (i < 0) return;
    int newBlock = LightPack::Block(packed);
    int sky = LightPack::Sky(data_[i]);
    int cur = LightPack::Block(data_[i]);
    if (newBlock > cur)
        data_[i] = LightPack::Make(sky, newBlock);
}

void LightMap::PropagateBlockLights(GridData& grid) {
    // BFS outward from emitters; level decreases by 1 per step through air
    // Mirrors AddLightUpdate flood behavior (large 4132 B function)
    std::queue<int> q;
    // Seed from all torch-like blocks
    for (int z = 0; z < sizeZ_; ++z)
    for (int y = 0; y < sizeY_; ++y)
    for (int x = 0; x < sizeX_; ++x) {
        int wx = x + ox_, wy = y + oy_, wz = z + oz_;
        int cidx = grid.CoordToIndex(wx, wy, wz);
        if (cidx < 0) continue;
        int emit = EmissionLevel(grid.cellTypes[cidx]);
        if (emit <= 0) continue;
        int idx = x + sizeX_ * (y + sizeY_ * z);
        int sky = LightPack::Sky(data_[idx]);
        data_[idx] = LightPack::Make(sky, emit);
        q.push(idx);
    }

    const int dx[6] = {1,-1,0,0,0,0};
    const int dy[6] = {0,0,1,-1,0,0};
    const int dz[6] = {0,0,0,0,1,-1};

    while (!q.empty()) {
        int idx = q.front(); q.pop();
        int level = LightPack::Block(data_[idx]);
        if (level <= 1) continue;
        int lx = idx % sizeX_;
        int ly = (idx / sizeX_) % sizeY_;
        int lz = idx / (sizeX_ * sizeY_);
        for (int d = 0; d < 6; ++d) {
            int nx = lx + dx[d], ny = ly + dy[d], nz = lz + dz[d];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= sizeX_ || ny >= sizeY_ || nz >= sizeZ_)
                continue;
            int nidx = nx + sizeX_ * (ny + sizeY_ * nz);
            int wx = nx + ox_, wy = ny + oy_, wz = nz + oz_;
            int cidx = grid.CoordToIndex(wx, wy, wz);
            unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : BlockId::Air;
            if (IsOpaque(t)) continue;
            int newLevel = level - 1;
            int cur = LightPack::Block(data_[nidx]);
            if (newLevel > cur) {
                int sky = LightPack::Sky(data_[nidx]);
                data_[nidx] = LightPack::Make(sky, newLevel);
                q.push(nidx);
            }
        }
    }
}

uint16_t LightMap::GetLight(int x, int y, int z) const {
    int i = Index(x, y, z);
    if (i < 0) return LightPack::Make(15, 0);
    return data_[i];
}

uint16_t LightMap::GetFaceLight(int x, int y, int z, int face) const {
    int nx = x, ny = y, nz = z;
    switch (face) {
        case 0: nx++; break; case 1: nx--; break;
        case 2: ny++; break; case 3: ny--; break;
        case 4: nz++; break; case 5: nz--; break;
    }
    return GetLight(nx, ny, nz);
}

uint32_t LightMap::GetLightColor(uint16_t packed) {
    int sky = LightPack::Sky(packed);
    int block = LightPack::Block(packed);
    int level = sky > block ? sky : block;
    float t = level / 15.f;
    t = t * t;
    int c = (int)(t * 255.f);
    if (c < 16) c = 16;
    int r = c, g = c, b = c;
    if (sky >= block) {
        b = std::min(255, c + 10);
    } else {
        // torch-ish warm tint when block light dominates
        r = std::min(255, c + 20);
        g = std::min(255, c + 10);
        b = std::max(16, c - 5);
    }
    return (uint32_t)(0xFF000000u | (r) | (g << 8) | (b << 16));
}
