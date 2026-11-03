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


// GridData::Material — 34 bytes (0x22). Recovered from Material::Init @ 0x98640
// and LoadMaterials @ 0x987d0 (parses assets/scenes/blocks.txt text).
// Allocation: InitMaterials sets +0xe0=0x100, +0xe8 = new byte[0x2200].
//
// Verified field map:
//   +0x00 byte0          Init=0x10; LoadMaterials overwrites
//   +0x01 prop01         type class 0..0x0a (NeedFace jump key)
//   +0x02 prop02         opacity-related (NeedFace checks ==0)
//   +0x03 waterBase      Min/MaxWaterHeight
//   +0x04                Init=0x07
//   +0x05..+0x0b         misc / flags (+0x07 setnz in LoadMaterials)
//   +0x0c..+0x11 uFace[6] atlas U per face XP,YP,ZP,XN,YN,ZN
//   +0x12..+0x17 vFace[6] atlas V per face
//   +0x18..+0x1a         flags (AddMesh reads +0x18/+0x19 occlusion)
//   +0x1b+FlowType       IsFlowAllowed permission byte (FlowType 0..5)
//   +0x1e short          Init from Material::Init 3rd arg
//   +0x20 short          Init=0xffff
// FlowType index into +0x1b overlaps trailing shorts — exact ownership open.
#pragma pack(push, 1)
struct Material {
    uint8_t raw[0x22];
    uint8_t prop01() const { return raw[0x01]; }
    uint8_t prop02() const { return raw[0x02]; }
    uint8_t waterBase() const { return raw[0x03]; }
    uint8_t uFace(int face) const { return raw[0x0c + (face % 6)]; }
    uint8_t vFace(int face) const { return raw[0x12 + (face % 6)]; }
    uint8_t flowPerm(int flowType) const { return raw[0x1b + (flowType % 6)]; }
    void initDefault(int index) {
        for (int i = 0; i < 0x22; ++i) raw[i] = 0;
        raw[0x00] = 0x10;
        raw[0x04] = 0x07;
        raw[0x06] = 0x01;
        raw[0x1e] = (uint8_t)(index & 0xff);
        raw[0x1f] = (uint8_t)((index >> 8) & 0xff);
        raw[0x20] = 0xff;
        raw[0x21] = 0xff;
    }
};
#pragma pack(pop)
static_assert(sizeof(Material) == 0x22, "Material must be 34 bytes");


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

    // Binary IsUnderwater @ 0x7b350 — fluid at position. Partial recovery: water-type cell.
    bool IsUnderwater(const Vector& pos) const;
    // Binary OnLadder @ 0x7b090 — not fully recovered; returns false until ladder mats wired.
    bool OnLadder(const Vector& feet, const Vector& bodySize, const Vector& offset) const;
    // Trace / interaction (binary-recovered)
    bool FindFirstUsedCell(const Vector& from, const Vector& to, float& outT, int& outCellIndex);
    bool IsHidden(int regionCoordA, int regionCoordB) const;
    void TracePoint(const Vector& from, const Vector& to, GridDataTraceResult& result, void* entity);
    bool GetAddingPosition(const GridDataTraceResult& tr, unsigned char blockType, int& outCellIndex, unsigned char& outFace);
    bool IsPossibleToAdd(const GridDataTraceResult& tr, unsigned char blockType, int flags,
                         const Vector* playerPos = nullptr, const Vector* playerSize = nullptr);
    // Material S-Index (+0x20 short) → cell type; binary MtlSelIndexToCell @ 0x98ed0
    unsigned char MtlSelIndexToCell(int selIndex) const;
    int CellToMtlSelIndex(unsigned char cellType) const;
    unsigned short ConfigureState(const GridDataTraceResult& tr, unsigned char blockType, const Vector& lookDir);
    bool PlaceBlock(const GridDataTraceResult& tr, unsigned char blockType,
                    const Vector* playerPos = nullptr, const Vector* playerSize = nullptr);
    bool BreakBlock(const GridDataTraceResult& tr);
    void GetCellSelectionBox(int, Vector&, Vector&);
    void ActivateCell(int, const Vector&);
    void Invalidate();
    void MarkModifiedRegions();
    bool MeshesDirty() const { return meshesDirty_; }
    void ClearMeshesDirty() { meshesDirty_ = false; }
    void UpdateWater(int maxSteps = 64);
    // InitMaterials @ 0x98720 / LoadMaterials @ 0x987d0 (blocks.txt)
    bool InitMaterials();
    bool LoadMaterials(const char* path);
    const Material* GetMaterial(int type) const;
    // Water height helpers (Material::Min/MaxWaterHeight @ 0x86b70 / 0x86c70)
    // matProp01 = material+0x01, matProp03 = material+0x03 (base height)

    // FlowType / LiquidType — used by IsFlowAllowed* and UpdateWaterState (binary enums).
    // FlowType indexes material permission byte at mat+0x1b+flowType.
    enum FlowType : int {
        FlowXP = 0, FlowXN = 1, FlowYP = 2, FlowYN = 3, FlowZP = 4, FlowZN = 5
    };
    // IsFlowAllowed* @ 0x851a0 / 0x852c0 / 0x85380 / 0x854a0 / 0x85cb0 / 0x86370
    // Returns 0 (blocked) or 0x0f (allowed) per recovered arithmetic; other values rare.
    // permByte = materials[type].bytes[0x1b + flowType]; without full table, uses type heuristics.
    int IsFlowAllowed(int cellIndex, FlowType flow) const;

    static int MinWaterHeight(unsigned char matProp01, unsigned char matProp03, unsigned short state);
    static int MaxWaterHeight(unsigned char matProp01, unsigned char matProp03, unsigned short state);

    LeavesQuality GetLeavesQuality() const;
    void SetLeavesQuality(LeavesQuality q);
    // Binary GridData::Resize(int,int,int,int,int,int,int) @ 0x81fb0
    bool Resize(int minX, int minY, int minZ, int maxX, int maxY, int maxZ, int flags);
    void Store();   // @ 0xa3770 — snapshot before resize
    void Restore(const Vector& playerPos); // @ 0xa4d50
    void InvalidateLighting(); // @ 0x798f0
    int GetViewDistanceCells() const { return viewDistanceCells_; }
    void SetViewDistanceCells(int v) { viewDistanceCells_ = v; }

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
    // materials at binary +0xe8; capacity +0xe0 (=256)
    std::vector<Material> materials;
    int materialCount = 0;
    bool meshesDirty_ = true;
    LeavesQuality leavesQuality_ = LeavesQuality::High; // +0xec default 2
    int viewDistanceCells_ = 64; // binary GridData +0x22c (from draw-distance table)
    // Store() snapshot for Resize/Restore (binary 0xa3770 / 0xa4d50)
    std::vector<unsigned char> storeTypes_;
    std::vector<unsigned short> storeStates_;
    int storeOriginX_ = 0, storeOriginY_ = 0, storeOriginZ_ = 0;
    int storeSizeX_ = 0, storeSizeY_ = 0, storeSizeZ_ = 0;

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
