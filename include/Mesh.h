#pragma once
#include "Vector.h"
#include "GridData.h"
#include "BlockDef.h"
#include "Lighting.h"
#include <vector>
#include <cstdint>

// PCT1Vertex — Position + Color (packed) + Texcoord (observed in TMeshBuilder<PCT1Vertex, ushort>)
struct PCT1Vertex {
    float x, y, z;
    uint32_t color;   // RGBA packed
    float u, v;
};

// GridData::MeshBuffer — recovered methods:
//   Create(int, int), PushQuads(...), PushBackQuads, PushTwoSidedQuads
class MeshBuffer {
public:
    MeshBuffer() = default;
    void Create(int maxVerts, int maxIndices);
    void Clear();

    void PushQuad(const PCT1Vertex verts[4]);

    const std::vector<PCT1Vertex>& Vertices() const { return verts_; }
    const std::vector<uint16_t>& Indices() const { return indices_; }
    int VertexCount() const { return (int)verts_.size(); }
    int IndexCount() const { return (int)indices_.size(); }

private:
    std::vector<PCT1Vertex> verts_;
    std::vector<uint16_t> indices_;
};

// RegionMesher mirrors Region::AddMesh @ 0x9b090.
// Dispatch on Material[+0x01] (BMT class):
//   1,4 → AddCross @ 0x8d750
//   2   → AddTorch @ 0x8db10
//   3   → AddLadder @ 0x8dc50
//   0,5,6,7,8,9,0xa → NeedFace* + AddBlockFace @ 0x877a0
// liquids (state 0x2000 / type water) → WaterNeedFace* + AddWaterFace
//
// Face index used by NeedFace / emitters (our convention):
//   0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
// Binary AddMesh face arg to AddBlockFace: 0=XP 1=YP 2=ZP 3=XN 4=YN 5=ZN
// (mapped at call site).
class RegionMesher {
public:
    // layers[7] — binary MeshBuffer layers indexed by MBI (0..6).
    static void Build(GridData& grid, int originX, int originY, int originZ,
                      const std::vector<BlockDef>& defs, MeshBuffer layers[7],
                      const LightMap* lights = nullptr);

    // NeedFace* recovery (NeedFaceXP @ 0x833d0 template).
    // Returns: 0 = no face, 1 = emit face, 2 = special/partial.
    static int NeedFace(GridData& grid, int x, int y, int z, int face);

    // WaterNeedFace* @ 0x855c0–0x867f0
    static int WaterNeedFace(GridData& grid, int x, int y, int z, int face);

    static float WaterHeightFrac(GridData& grid, int x, int y, int z);
    enum LiquidType { LiquidWater = 0, LiquidLava = 1 };
    static bool IsWaterCell(GridData& grid, int x, int y, int z);
    static bool IsLavaCell(GridData& grid, int x, int y, int z);
    static int CellLiquidType(GridData& grid, int x, int y, int z); // -1 none, 0 water, 1 lava

private:
    static bool IsSolid(GridData& grid, int x, int y, int z);

    // Material UV for face (binary mat[+0x0c+face]/[+0x12+face], face=XP..ZN order).
    // ourFace 0..5 → mat slot via kOurToMatFace.
    static void FaceUV(const Material& mat, int ourFace, float& u0, float& v0, float& u1, float& v1,
                       unsigned short orientState = 0);

    static uint32_t FaceColor(const LightMap* lights, int x, int y, int z, int face);

    // --- Region::AddMesh specialized emitters (binary names) ---
    static void AddCross(MeshBuffer& out, int x, int y, int z,
                         const Material& mat, const LightMap* lights);
    static void AddTorch(MeshBuffer& out, GridData& grid, int x, int y, int z,
                         const Material& mat, const LightMap* lights);
    static void AddLadder(MeshBuffer& out, GridData& grid, int x, int y, int z,
                          const Material& mat, const LightMap* lights);

    // AddBlockFace @ 0x877a0 — class-driven solid / slab / stairs / fence / door / …
    // needFlags: NeedFace return (1=full, 2=partial).
    static void AddBlockFace(MeshBuffer& out, GridData& grid, int x, int y, int z,
                             const Material& mat, int ourFace, int needFlags,
                             const LightMap* lights);

    // Geometry helpers used by AddBlockFace
    static void EmitAxisFace(MeshBuffer& out, float x0, float y0, float z0,
                             float x1, float y1, float z1, int ourFace,
                             float u0, float v0, float u1, float v1, uint32_t color,
                             int uvRot = 0);
    static void EmitFace(MeshBuffer& out, int x, int y, int z, int face,
                         float u0, float v0, float u1, float v1, uint32_t color,
                         int uvRot = 0);
    static void EmitWaterFace(GridData& grid, MeshBuffer& out, int x, int y, int z, int face,
                              float u0, float v0, float u1, float v1, uint32_t color);

    // Binary face index (XP YP ZP XN YN ZN) ↔ our face index
    static int OurToBinFace(int ourFace);
    static int BinToOurFace(int binFace);
};

void DrawPrimitive(unsigned int mode, const PCT1Vertex* verts, const uint16_t* indices,
                   int indexCount, int /*unused*/);

// DrawCursor @ 0x59a70 — wire AABB of hit cell (GetCellSelectionBox ± eps),
// then DrawPrimitive lines. face arg: binary 0..5 draws optional face emphasis
// (jumptable); >=6 or -1 draws full box only. color is AABBGGRR packed (GLES).
void DrawCursor(GridData& grid, int cellIndex, int unusedHitFlags, int face, uint32_t color);
