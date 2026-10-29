#include <cstdio>
#include <vector>
#include "GridData.h"
#include <cmath>
#include <cstring>
#include <algorithm>

GridData::GridData() {
    // Matching observed ctor defaults: LeavesQuality = 2, various zero-inits
    // Full field init (MeshBuffer, DDSFile, etc.) still being mapped.
}

GridData::~GridData() {}

void GridData::InitDimensionsFromWorldBounds() {
    // Inclusive integer cells covering the float bounds
    originX = (int)worldMin.x;
    originY = (int)worldMin.y;
    originZ = (int)worldMin.z;
    sizeX = (int)worldMax.x - originX;
    sizeY = (int)worldMax.y - originY;
    sizeZ = (int)worldMax.z - originZ;
    if (sizeX < 1) sizeX = 1;
    if (sizeY < 1) sizeY = 1;
    if (sizeZ < 1) sizeZ = 1;
    size_t n = (size_t)sizeX * sizeY * sizeZ;
    cellTypes.assign(n, 0);   // Air
    cellStates.assign(n, 0);
}

int GridData::CoordToIndex(int x, int y, int z) const {
    int lx = x - originX;
    int ly = y - originY;
    int lz = z - originZ;
    if (lx < 0 || ly < 0 || lz < 0 || lx >= sizeX || ly >= sizeY || lz >= sizeZ)
        return -1;
    return lx + sizeX * (ly + sizeY * lz);
}

void GridData::SetCell(int x, int y, int z, unsigned char type, unsigned short state) {
    int idx = CoordToIndex(x, y, z);
    if (idx < 0) return;
    cellTypes[idx] = type;
    cellStates[idx] = state;
    // Original also marks regions modified and updates meshes; not yet reconstructed.
}

void GridData::SetState(int cellIndex, unsigned short state) {
    if (cellIndex >= 0 && (size_t)cellIndex < cellStates.size())
        cellStates[cellIndex] = state;
}

void GridData::FillCells(int x0, int y0, int z0, int x1, int y1, int z1, unsigned char type) {
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                SetCell(x, y, z, type, 0);
}

void GridData::FillCells(int x0, int y0, int z0, int x1, int y1, int z1, unsigned char type, unsigned short state) {
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                SetCell(x, y, z, type, state);
}

void GridData::FillState(int x0, int y0, int z0, int x1, int y1, int z1, unsigned short state) {
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                int idx = CoordToIndex(x, y, z);
                if (idx >= 0) cellStates[idx] = state;
            }
}

bool GridData::IsUnderwater(const Vector&) { return false; } // TBD

void GridData::GetCellSelectionBox(int, Vector&, Vector&) {}
void GridData::ActivateCell(int, const Vector&) {}

GridData::LeavesQuality GridData::GetLeavesQuality() const {
    return LeavesQuality::High; // matching default observed at +0xec
}
void GridData::SetLeavesQuality(LeavesQuality) {}

void GridData::Region::Init(int rx, int ry, int rz) {
    x = rx; y = ry; z = rz;
    unknown0 = unknown1 = 0x7fffffff;
}
void GridData::Region::Free() {}



unsigned int GridData::EncodeWorldPos(int x, int z) {
    // Exact recovery from 0xa2300:
    // if negative: add 15 before sar 4 (arithmetic floor-divide by 16)
    auto region = [](int v) -> int {
        int t = v;
        if (t < 0) t = v + 15;
        return (t >> 4) + 0x8000;
    };
    unsigned int hx = (unsigned int)region(x) << 16;
    unsigned int hz = (unsigned int)region(z);
    return hx | hz;
}

void GridData::DecodeWorldPos(int& x, int& z, unsigned int key) {
    int rx = (int)((key >> 16) & 0xFFFF) - 0x8000;
    int rz = (int)(key & 0xFFFF) - 0x8000;
    x = rx * 16;
    z = rz * 16;
}

void GridData::MarkModifiedRegions() {
    // Original @ 0x7aad0 — walks dirty cells and flags regions
    meshesDirty_ = true;
}

void GridData::Invalidate() {
    // Original @ 0x794a0 — forces full mesh rebuild
    MarkModifiedRegions();
}




// Amanatides & Woo style voxel DDA — recovers TracePoint behavior
void GridData::TracePoint(const Vector& from, const Vector& to,
                          GridDataTraceResult& result, void* /*entity*/) {
    result.Clear();
    float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (len < 1e-6f) return;
    dx /= len; dy /= len; dz /= len;

    int x = (int)std::floor(from.x);
    int y = (int)std::floor(from.y);
    int z = (int)std::floor(from.z);

    int stepX = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    int stepY = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    int stepZ = dz > 0 ? 1 : (dz < 0 ? -1 : 0);

    auto frac = [](float v) {
        float f = v - std::floor(v);
        return f;
    };
    float tMaxX = (stepX > 0) ? (1.f - frac(from.x)) / std::fabs(dx)
                : (stepX < 0) ? (frac(from.x) / std::fabs(dx)) : 1e30f;
    float tMaxY = (stepY > 0) ? (1.f - frac(from.y)) / std::fabs(dy)
                : (stepY < 0) ? (frac(from.y) / std::fabs(dy)) : 1e30f;
    float tMaxZ = (stepZ > 0) ? (1.f - frac(from.z)) / std::fabs(dz)
                : (stepZ < 0) ? (frac(from.z) / std::fabs(dz)) : 1e30f;
    if (dx == 0) tMaxX = 1e30f;
    if (dy == 0) tMaxY = 1e30f;
    if (dz == 0) tMaxZ = 1e30f;

    float tDeltaX = (dx != 0) ? 1.f / std::fabs(dx) : 1e30f;
    float tDeltaY = (dy != 0) ? 1.f / std::fabs(dy) : 1e30f;
    float tDeltaZ = (dz != 0) ? 1.f / std::fabs(dz) : 1e30f;

    float t = 0.f;
    int face = -1;
    const float maxDist = len;

    for (int i = 0; i < 256 && t <= maxDist; ++i) {
        int cidx = CoordToIndex(x, y, z);
        if (cidx >= 0) {
            unsigned char bt = cellTypes[cidx];
            if (bt != 0) { // non-air
                result.hit = true;
                result.cellX = x; result.cellY = y; result.cellZ = z;
                result.face = face;
                result.distance = t;
                result.blockType = bt;
                result.hitPos = Vector(from.x + dx * t, from.y + dy * t, from.z + dz * t);
                return;
            }
        }
        // step
        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                t = tMaxX; tMaxX += tDeltaX; x += stepX;
                face = (stepX > 0) ? 1 : 0; // entered from -X or +X
            } else {
                t = tMaxZ; tMaxZ += tDeltaZ; z += stepZ;
                face = (stepZ > 0) ? 5 : 4;
            }
        } else {
            if (tMaxY < tMaxZ) {
                t = tMaxY; tMaxY += tDeltaY; y += stepY;
                face = (stepY > 0) ? 3 : 2;
            } else {
                t = tMaxZ; tMaxZ += tDeltaZ; z += stepZ;
                face = (stepZ > 0) ? 5 : 4;
            }
        }
    }
}

bool GridData::GetAddingPosition(const GridDataTraceResult& tr, unsigned char /*blockType*/,
                                 int& outCellIndex, unsigned char& outFace) {
    if (!tr.hit) return false;
    int x = tr.cellX, y = tr.cellY, z = tr.cellZ;
    // place against the hit face (adjacent empty cell)
    switch (tr.face) {
        case 0: x++; break; case 1: x--; break;
        case 2: y++; break; case 3: y--; break;
        case 4: z++; break; case 5: z--; break;
        default: break;
    }
    outCellIndex = CoordToIndex(x, y, z);
    outFace = (unsigned char)tr.face;
    return outCellIndex >= 0;
}

bool GridData::IsPossibleToAdd(const GridDataTraceResult& tr, unsigned char blockType, int /*flags*/) {
    int idx = -1; unsigned char face = 0;
    if (!GetAddingPosition(tr, blockType, idx, face)) return false;
    if (idx < 0 || idx >= (int)cellTypes.size()) return false;
    return cellTypes[idx] == 0; // only into air
}

bool GridData::PlaceBlock(const GridDataTraceResult& tr, unsigned char blockType) {
    if (!IsPossibleToAdd(tr, blockType, 0)) return false;
    int idx = -1; unsigned char face = 0;
    GetAddingPosition(tr, blockType, idx, face);
    // recover x,y,z from index
    int lx = idx % sizeX;
    int ly = (idx / sizeX) % sizeY;
    int lz = idx / (sizeX * sizeY);
    SetCell(originX + lx, originY + ly, originZ + lz, blockType, 0);
    MarkModifiedRegions();
    return true;
}

bool GridData::BreakBlock(const GridDataTraceResult& tr) {
    if (!tr.hit) return false;
    SetCell(tr.cellX, tr.cellY, tr.cellZ, 0, 0); // air
    MarkModifiedRegions();
    return true;
}


// Simplified water tick based on UpdateWaterState @ 0xa1040 + flow direction helpers.
// Rules: water falls into air below; otherwise spreads horizontally to air neighbors
// (finite-ish by limiting steps). Full level-based flow still TBD.
void GridData::UpdateWater(int maxSteps) {
    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0) return;
    const unsigned char WATER = 5;
    struct Cell { int x,y,z; };
    std::vector<Cell> sources;
    sources.reserve(256);
    for (int z = 0; z < sizeZ; ++z)
    for (int y = 0; y < sizeY; ++y)
    for (int x = 0; x < sizeX; ++x) {
        int i = x + sizeX * (y + sizeY * z);
        if (cellTypes[i] == WATER) sources.push_back({originX+x, originY+y, originZ+z});
    }
    int steps = 0;
    bool any = false;
    for (size_t si = 0; si < sources.size() && steps < maxSteps; ++si) {
        int x = sources[si].x, y = sources[si].y, z = sources[si].z;
        // fall
        int below = CoordToIndex(x, y-1, z);
        if (below >= 0 && cellTypes[below] == 0) {
            cellTypes[below] = WATER;
            any = true; steps++;
            continue;
        }
        // horizontal
        const int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (auto& d : dirs) {
            int ni = CoordToIndex(x+d[0], y, z+d[1]);
            if (ni >= 0 && cellTypes[ni] == 0) {
                // only spread if supported or has water neighbor above/side (simple)
                cellTypes[ni] = WATER;
                any = true; steps++;
                if (steps >= maxSteps) break;
            }
        }
    }
    if (any) MarkModifiedRegions();
}

// Save one 16×full-height×16 column strip keyed by EncodeWorldPos
// Original SaveChunk @ 0xa25d0 — writes region key + cell payload
void GridData::SaveChunk() {
    // no-op without active stream; use SaveWorld for full dump
}

bool GridData::SaveWorld(const char* path) {
    FILE* fp = std::fopen(path, "wb");
    if (!fp) return false;
    // Simple portable format (documented; not byte-identical to original):
    // magic "ELW1", origin, size, then type[] then state[]
    const char magic[4] = {'E','L','W','1'};
    std::fwrite(magic, 1, 4, fp);
    int hdr[6] = { originX, originY, originZ, sizeX, sizeY, sizeZ };
    std::fwrite(hdr, sizeof(int), 6, fp);
    if (!cellTypes.empty())
        std::fwrite(cellTypes.data(), 1, cellTypes.size(), fp);
    if (!cellStates.empty())
        std::fwrite(cellStates.data(), sizeof(unsigned short), cellStates.size(), fp);
    std::fclose(fp);
    return true;
}

bool GridData::LoadWorld(const char* path) {
    FILE* fp = std::fopen(path, "rb");
    if (!fp) return false;
    char magic[4];
    if (std::fread(magic, 1, 4, fp) != 4 || magic[0]!='E' || magic[1]!='L' || magic[2]!='W' || magic[3]!='1') {
        std::fclose(fp); return false;
    }
    int hdr[6];
    if (std::fread(hdr, sizeof(int), 6, fp) != 6) { std::fclose(fp); return false; }
    originX = hdr[0]; originY = hdr[1]; originZ = hdr[2];
    sizeX = hdr[3]; sizeY = hdr[4]; sizeZ = hdr[5];
    size_t n = (size_t)sizeX * sizeY * sizeZ;
    cellTypes.assign(n, 0);
    cellStates.assign(n, 0);
    if (std::fread(cellTypes.data(), 1, n, fp) != n) { std::fclose(fp); return false; }
    std::fread(cellStates.data(), sizeof(unsigned short), n, fp); // optional
    std::fclose(fp);
    meshesDirty_ = true;
    return true;
}

bool GridData::LoadChunk(int regionX, int regionZ) {
    // Region key for lookup — full page cache not yet recovered
    (void)EncodeWorldPos(regionX * 16, regionZ * 16);
    return false; // page-cache path TBD
}
