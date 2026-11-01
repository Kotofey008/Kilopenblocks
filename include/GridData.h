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

// Binary layout recovered from TracePoint writes to result (edi):
//   +0x00 float t / distance fraction along ray
//   +0x04 float hitPos.z (interpolated)
//   +0x08 float hitPos.y
//   +0x0c float hitPos.x
//   +0x10 int  cellIndex (linear)
//   +0x14 int  face / flags
//   +0x18 int  cellX (world)
//   +0x1c int  cellY
//   +0x20 int  cellZ
//   +0x24 int  0 (padding / unused)
// Current struct is a readable projection of that layout.
struct GridDataTraceResult {
    bool hit = false;
    int cellX = 0, cellY = 0, cellZ = 0;
    int cellIndex = -1;     // linear index when available
    int face = -1;          // 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z (binary face enum)
    float distance = 0.f;   // t along ray (0..1 of segment or absolute)
    Vector hitPos;
    unsigned char blockType = 0;
    void Clear() {
        hit = false; face = -1; distance = 0.f;
        cellX = cellY = cellZ = 0; cellIndex = -1; blockType = 0;
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
    // Trace / interaction (binary-recovered)
    bool FindFirstUsedCell(const Vector& from, const Vector& to, float& outT, int& outCellIndex);
    bool IsHidden(int regionCoordA, int regionCoordB) const;
    void TracePoint(const Vector& from, const Vector& to, GridDataTraceResult& result, void* entity);
    bool GetAddingPosition(const GridDataTraceResult& tr, unsigned char blockType, int& outCellIndex, unsigned char& outFace);
    bool IsPossibleToAdd(const GridDataTraceResult& tr, unsigned char blockType, int flags);
    unsigned short ConfigureState(const GridDataTraceResult& tr, unsigned char blockType, const Vector& lookDir);
    bool PlaceBlock(const GridDataTraceResult& tr, unsigned char blockType);
    bool BreakBlock(const GridDataTraceResult& tr);
    void GetCellSelectionBox(int, Vector&, Vector&);
    void ActivateCell(int, const Vector&);
    void Invalidate();
    void MarkModifiedRegions();
    bool MeshesDirty() const { return meshesDirty_; }
    void ClearMeshesDirty() { meshesDirty_ = false; }
    void UpdateWater(int maxSteps = 64);
    // Water height helpers (Material::Min/MaxWaterHeight @ 0x86b70 / 0x86c70)
    // matProp01 = material+0x01, matProp03 = material+0x03 (base height)
    static int MinWaterHeight(unsigned char matProp01, unsigned char matProp03, unsigned short state);
    static int MaxWaterHeight(unsigned char matProp01, unsigned char matProp03, unsigned short state);

    LeavesQuality GetLeavesQuality() const;
    void SetLeavesQuality(LeavesQuality q);

    // Bounds (from scene Globals)
    Vector worldMin{-576.f, -64.f, -576.f};
    Vector worldMax{576.f, 192.f, 576.f};

    // Storage (original uses multiple parallel arrays at fixed offsets;
    // current vectors approximate types@+0x74 and states@+0x78).
    // Full layout (light maps, region refcounts, dirty counters at +0x230..+0x288)
    // is only partially recovered; see docs/AUDIT.md and SetCell notes.
    std::vector<unsigned char> cellTypes;   // type per cell  (orig +0x74)
    std::vector<unsigned short> cellStates; // state per cell (orig +0x78)
    // Hierarchical occupancy for STATE_BIT_4000 (orig +0x260..+0x278 family)
    // L0 = >>4, L1 = >>8, L2 = >>12, L3 = >>16
    std::vector<uint8_t> aux4000L0_, aux4000L1_, aux4000L2_, aux4000L3_;
    void UpdateHierarchy4000(int cellIndex, bool bitOn);
    int sizeX = 0, sizeY = 0, sizeZ = 0;     // dims used for index (orig +0x44 / +0x50 related)
    int originX = 0, originY = 0, originZ = 0;
    bool meshesDirty_ = true;

    // Region dirty tracking (region size 16, stride 0xa4 observed in InitRegions).
    // Key = EncodeWorldPos(regionX*16, regionZ*16) or simpler (rx,ry,rz) for now.
    struct RegionKey {
        int rx, ry, rz;
        bool operator==(const RegionKey& o) const { return rx==o.rx && ry==o.ry && rz==o.rz; }
    };
    std::vector<RegionKey> dirtyRegions_;
    void MarkRegionDirty(int x, int y, int z);
    bool IsRegionDirty(int rx, int ry, int rz) const;
    void ClearDirtyRegions() { dirtyRegions_.clear(); }

    // Observed state bit masks from SetCell / SetState disassembly
    static constexpr unsigned short STATE_BIT_2000 = 0x2000;
    static constexpr unsigned short STATE_BIT_3C0  = 0x03C0;
    static constexpr unsigned short STATE_BIT_4000 = 0x4000;
    static constexpr unsigned short STATE_MASK_BFFF = 0xBFFF;

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
