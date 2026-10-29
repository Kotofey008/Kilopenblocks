#pragma once
#include "Vector.h"
#include <cstdint>
#include <vector>
#include <string>

// Evidence from symbols + disassembly:
// - SetCell(x, y, z, unsigned char type, unsigned short state)
// - Separate SetState(int, unsigned short)
// - FillCells / FillState for ranges
// - Region nested class (Init(int,int,int), Update, AddMesh, Draw, Free)
// - Chunk intermediate (CopyChunkToRegions / CopyRegionsToChunk)
// - LeavesQuality stored at offset +0xec (default 2)
// - MeshBuffer at +0x90, CellStateBuffer
// - World bounds from start.scene: WorldSizeMin/Max
// Exact packing of type+state per cell and region size (possible 16^3) still partially open;
// 0xa4 (164) appears as structure stride in InitRegions.

struct GridDataTraceResult {
    bool hit = false;
    int cellX = 0, cellY = 0, cellZ = 0;
    int face = -1;          // 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
    float distance = 0.f;
    Vector hitPos;
    unsigned char blockType = 0;
    void Clear() {
        hit = false; face = -1; distance = 0.f;
        cellX = cellY = cellZ = 0; blockType = 0;
        hitPos = Vector();
    }
};

class GridData {
public:
    enum class LeavesQuality : int { Low = 0, Medium = 1, High = 2, Ultra = 3 };

    struct Region {
        int unknown0 = 0x7fffffff;
        int unknown1 = 0x7fffffff;
        int x = 0, y = 0, z = 0; // from Init
        void Init(int rx, int ry, int rz);
        void Free();
    };

    GridData();
    ~GridData();

    // Core write (observed signature)
    void SetCell(int x, int y, int z, unsigned char type, unsigned short state);
    void SetState(int cellIndex, unsigned short state); // observed
    void FillCells(int x0, int y0, int z0, int x1, int y1, int z1, unsigned char type);
    void FillCells(int x0, int y0, int z0, int x1, int y1, int z1, unsigned char type, unsigned short state);
    void FillState(int x0, int y0, int z0, int x1, int y1, int z1, unsigned short state);

    bool IsUnderwater(const Vector& pos);
    void TracePoint(const Vector& from, const Vector& to, GridDataTraceResult& result, void* entity);
    bool GetAddingPosition(const GridDataTraceResult& tr, unsigned char blockType, int& outCellIndex, unsigned char& outFace);
    bool IsPossibleToAdd(const GridDataTraceResult& tr, unsigned char blockType, int flags);
    bool PlaceBlock(const GridDataTraceResult& tr, unsigned char blockType);
    bool BreakBlock(const GridDataTraceResult& tr);
    void GetCellSelectionBox(int, Vector&, Vector&);
    void ActivateCell(int, const Vector&);
    void Invalidate();
    void MarkModifiedRegions();
    bool MeshesDirty() const { return meshesDirty_; }
    void ClearMeshesDirty() { meshesDirty_ = false; }
    void UpdateWater(int maxSteps = 64);

    LeavesQuality GetLeavesQuality() const;
    void SetLeavesQuality(LeavesQuality q);

    // Bounds (from scene Globals)
    Vector worldMin{-576.f, -64.f, -576.f};
    Vector worldMax{576.f, 192.f, 576.f};

    // Placeholder storage until exact layout recovered.
    // Will be replaced once SetCell body fully understood.
    // For now allows higher-level systems to compile and load block defs.
    std::vector<unsigned char> cellTypes;   // type per cell
    std::vector<unsigned short> cellStates; // state per cell
    int sizeX = 0, sizeY = 0, sizeZ = 0;
    int originX = 0, originY = 0, originZ = 0;
    bool meshesDirty_ = true;

    void InitDimensionsFromWorldBounds();
    int CoordToIndex(int x, int y, int z) const;

    // Chunk key encoding (recovered from disassembly @ 0xa2300):
    // region = floor_div16(coord); key = ((regionX+0x8000)<<16) | (regionZ+0x8000)
    static unsigned int EncodeWorldPos(int x, int z);
    static void DecodeWorldPos(int& x, int& z, unsigned int key);

    // Save one chunk region (observed @ 0xa25d0)
    void SaveChunk();
    bool LoadChunk(int regionX, int regionZ);
    bool SaveWorld(const char* path);
    bool LoadWorld(const char* path);
};
