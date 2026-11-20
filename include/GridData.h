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


// GridData::Material — 34 bytes (0x22).
// Material::Init @ 0x98640, LoadMaterials @ 0x987d0 (blocks.txt via MtlDescFileName).
// Allocation: InitMaterials sets +0xe0=0x100, +0xe8 = new byte[0x2200].
//
// blocks.txt columns → fields (ParseString 29 ints, exact from decomp):
//   Index → slot
//   S-Index → +0x20 (int16); invalid → 0xffff
//   Uxp Vxp Uyp Vyp Uzp Vzp Uxn Vxn Uyn Vyn Uzn Vzn
//     → +0x0c..+0x11 U (XP YP ZP XN YN ZN), +0x12..+0x17 V
//   BH  → +0x00 after BMT gating (and may drive +0x03/+0x04)
//   BMT → +0x01 class 0..0x0a (NeedFace / IsFlowAllowed jump key)
//   MBI → +0x02 (clamp 0..6)
//   SAU → +0x05 (!=0)
//   Col → +0x06 (!=0)
//   All → +0x07 (!=0)
//   UVR → +0x08
//   XT YT ZT → +0x09 +0x0a +0x0b
//   W H → +0x18 +0x19 (clamp 0..16)
//   L   → +0x1a
//   FTL FTW → +0x1b +0x1c (flow perm base; IsFlowAllowed uses +0x1b+FlowType)
//
// Post-process by class (+0x01):
//   0 + height<0xe: +0x03=height, +0x04=((0x10-height)>>1)-1
//   6: +0x03=8, +0x04=3
//   10: +0x03=2, +0x04=6
#pragma pack(push, 1)
struct Material {
    uint8_t raw[0x22];
    uint8_t height() const { return raw[0x00]; }       // BH-derived
    uint8_t prop01() const { return raw[0x01]; }       // BMT class
    uint8_t prop02() const { return raw[0x02]; }       // MBI
    uint8_t waterBase() const { return raw[0x03]; }    // derived / water
    uint8_t waterCompanion() const { return raw[0x04]; }
    bool sau() const { return raw[0x05] != 0; }
    bool col() const { return raw[0x06] != 0; }        // collision / solid-ish
    bool allFaces() const { return raw[0x07] != 0; }
    uint8_t uvr() const { return raw[0x08]; }
    uint8_t xt() const { return raw[0x09]; }
    uint8_t yt() const { return raw[0x0a]; }
    uint8_t zt() const { return raw[0x0b]; }
    uint8_t uFace(int face) const { return raw[0x0c + (face % 6)]; }
    uint8_t vFace(int face) const { return raw[0x12 + (face % 6)]; }
    uint8_t sizeW() const { return raw[0x18]; }
    uint8_t sizeH() const { return raw[0x19]; }
    uint8_t sizeL() const { return raw[0x1a]; }
    uint8_t flowPerm(int flowType) const {
        // FTL at +0x1b, FTW at +0x1c. Binary indexes +0x1b+FlowType, but only two
        // columns exist in blocks.txt; +0x20 is S-Index — must not be read as perm.
        // FlowXP=0→FTL, FlowXN=1→FTW; Y/Z reuse FTL (or FTW if FTL clear).
        int ft = flowType % 6;
        if (ft == 0) return raw[0x1b];
        if (ft == 1) return raw[0x1c] ? raw[0x1c] : raw[0x1b];
        // YP/YN/ZP/ZN: expanded slots 0x1d..0x1f if set, else FTL then FTW
        if (ft >= 2 && ft <= 4) {
            uint8_t p = raw[0x1b + ft]; // 0x1d,0x1e,0x1f
            if (p) return p;
            return raw[0x1b] ? raw[0x1b] : raw[0x1c];
        }
        // FlowZN=5 would land on S-Index — use FTW/FTL only
        return raw[0x1c] ? raw[0x1c] : raw[0x1b];
    }
    int16_t selIndex() const {
        return (int16_t)(raw[0x20] | (raw[0x21] << 8));
    }
    void initDefault(int index) {
        // Material::Init(this, 0, 0, (short)index)
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
    // Highest S-Index+1 (binary GridData+0xe4) — palette page wrap bound
    int MaxSelIndex() const { return maxSelIndex_; }
    unsigned short ConfigureState(const GridDataTraceResult& tr, unsigned char blockType, const Vector& lookDir);
    bool PlaceBlock(const GridDataTraceResult& tr, unsigned char blockType,
                    const Vector* playerPos = nullptr, const Vector* playerSize = nullptr);
    bool BreakBlock(const GridDataTraceResult& tr);
    void GetCellSelectionBox(int cellIndex, Vector& outMin, Vector& outMax);
    // GetCellCollisionBoxArray @ 0x8xx — up to 4 world-space AABBs for physics.
    // Returns count written to outMins/outMaxs (arrays of size >= 4).
    // 0 = no collision (air, water, plants, Col=0, …).
    struct CellAabb { Vector mn; Vector mx; };
    int GetCellCollisionBoxArray(int cellIndex, CellAabb* out, int maxOut) const;
    // True if material has collision (Col flag) and is not a pure decoration class.
    bool CellHasCollision(int cellIndex) const;
    void ActivateCell(int, const Vector&);
    // GridData::IsActiveCell @ 0xa1a20 — jump table type-0x11 .. type-0x52.
    // True only for 18 types recovered from the table:
    // 17,18,34,35,39,40,44,45,67..71,78..82.
    bool IsActiveCellType(unsigned char blockType) const;
    void Invalidate();
    void MarkModifiedRegions();
    bool MeshesDirty() const { return meshesDirty_; }
    void ClearMeshesDirty() { meshesDirty_ = false; }
    void UpdateWater(int maxSteps = 64);

    // Binary liquid pipeline (UpdateWaterState @ 0xa1040):
    //   UpdateOneWaterState → queue 0xFE SpreadWater / 0xFF SpreadLava / SetCell
    //   Spread* mark-only receive gates; SetCell applies state.
    void UpdateOneWaterState(int x, int y, int z);
    void SpreadWater(int x, int y, int z, int srcLevel);
    void SpreadLava(int x, int y, int z, int srcLevel);
    bool WaterThresholdTest(int fromIdx, int toIdx, int levelHint) const;
    unsigned short FindFlow(int x, int y, int z, int cellIdx, int maxDepth);
    // GetWaterBlockHeight @ 0x8b4a0 — height units 0..0x80
    int GetWaterBlockHeight(int x, int y, int z, int dx = 0, int dz = 0) const;
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
    int maxSelIndex_ = 0; // binary +0xe4
    // Liquid command queue: type 0xFE=SpreadWater, 0xFF=SpreadLava, else SetCell type
    struct LiquidCmd {
        int x, y, z;
        unsigned char type;   // 0xFE / 0xFF / cell type for SetCell
        unsigned short state; // level for Spread*, full state for SetCell
    };
    std::vector<LiquidCmd> liquidQueue_;
    void QueueLiquidCmd(int x, int y, int z, unsigned char type, unsigned short state);
    void FlushLiquidQueue(int& stepsLeft);

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
    // --- Liquid state (binary world model, NOT material type 5) ---
    // AddGrassAndWater: type=0, state=0x2000
    // AddLiquidSource(LiquidType==0): type=0, state=0xe000  (water source)
    // AddLiquidSource(LiquidType!=0): type=0, state=0xc3c0  (lava source)
    // AddOrDeleteBlock place type5 → type=0, state=0xc3c0 (lava!)
    // Material id 5 in blocks.txt is a hidden UV/icon row (S-Index -1); never used
    // by natural generation. Flowing water keeps type=0 and packs level in bits 10-12.
    static constexpr unsigned short STATE_BIT_2000 = 0x2000;   // fluid (water) flag
    static constexpr unsigned short STATE_BIT_3C0  = 0x03C0;   // lava / logged liquid nibble
    static constexpr unsigned short STATE_BIT_4000 = 0x4000;
    static constexpr unsigned short STATE_MASK_BFFF = 0xBFFF;
    static constexpr unsigned short STATE_LIQUID_ANY = 0x3FC0; // any liquid present
    static constexpr unsigned short STATE_LEVEL_MASK = 0x1C00;
    static constexpr unsigned short STATE_LEVEL_SHIFT = 10;
    static constexpr unsigned short WATER_FILL_STATE   = 0x2000; // lake fill
    static constexpr unsigned short WATER_SOURCE_STATE = 0xE000; // 0xC000|0x2000
    static constexpr unsigned short LAVA_SOURCE_STATE  = 0xC3C0; // 0xC000|0x03C0

    // LiquidType @ AddWaterFace / AddLiquidSource
    enum LiquidType : int { LiquidWater = 0, LiquidLava = 1 };

    static bool HasLiquidState(unsigned short st) { return (st & STATE_LIQUID_ANY) != 0; }
    static bool IsWaterFluidState(unsigned short st) { return (st & STATE_BIT_2000) != 0; }
    static bool IsLavaFluidState(unsigned short st) {
        return (st & STATE_BIT_3C0) != 0 && (st & STATE_BIT_2000) == 0;
    }
    static int LiquidLevel(unsigned short st) {
        return (int)((st & STATE_LEVEL_MASK) >> STATE_LEVEL_SHIFT);
    }
    // Build water state for level 0..7 (source=0). Keeps fluid bit; sources may use 0xE000.
    static unsigned short MakeWaterState(int level, bool asSource = false) {
        level = level < 0 ? 0 : (level > 7 ? 7 : level);
        if (asSource && level == 0) return WATER_SOURCE_STATE;
        return (unsigned short)(STATE_BIT_2000 | ((level & 7) << STATE_LEVEL_SHIFT));
    }
    static unsigned short MakeLavaState(int /*level*/ = 0) { return LAVA_SOURCE_STATE; }

    // True if cell is world-liquid (air+state or any type with fluid bits). Type 5 alone
    // without liquid state is the hidden material block, not simulated water.
    bool IsLiquidCell(int x, int y, int z) const;
    bool IsWaterFluidCell(int x, int y, int z) const;
    bool IsLavaFluidCell(int x, int y, int z) const;

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
