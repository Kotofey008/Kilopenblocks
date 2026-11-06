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

    // Simplified PushQuads matching observed signature shape:
    // (x,y,z face origin, dir, UV shorts, lighting, color, flags)
    void PushQuad(const PCT1Vertex verts[4]);

    const std::vector<PCT1Vertex>& Vertices() const { return verts_; }
    const std::vector<uint16_t>& Indices() const { return indices_; }
    int VertexCount() const { return (int)verts_.size(); }
    int IndexCount() const { return (int)indices_.size(); }

private:
    std::vector<PCT1Vertex> verts_;
    std::vector<uint16_t> indices_;
};

// RegionMesher approximates Region::AddMesh @ 0x9b090 (3164B).
// Binary uses NeedFaceXP/YP/ZP/XN/YN/ZN + AddBlockFace(14KB) / AddWaterFace /
// AddCross / AddTorch / AddLadder — NOT a single greedy solid cull.
// Current: face-cull + optional greedy. See docs/MESHER_LIGHTING_WATER.md.
class RegionMesher {
public:
    // region origin in world cell coords; size fixed at 16
    static void Build(GridData& grid, int originX, int originY, int originZ,
                      const std::vector<BlockDef>& defs, MeshBuffer& out,
                      const LightMap* lights = nullptr);
    // Optional second buffer for translucent water faces (drawn after solids).
    static void Build(GridData& grid, int originX, int originY, int originZ,
                      const std::vector<BlockDef>& defs, MeshBuffer& solidOut,
                      MeshBuffer* waterOut, const LightMap* lights);

    // NeedFace* recovery (NeedFaceXP @ 0x833d0 template).
    // Returns: 0 = no face, 1 = emit face, 2 = special/partial (binary).
    // face: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
    static int NeedFace(GridData& grid, int x, int y, int z, int face);

    // WaterNeedFace* @ 0x855c0–0x867f0 — cull water|water, emit against air/solid.
    static int WaterNeedFace(GridData& grid, int x, int y, int z, int face);

    // GetWaterBlockHeight @ 0x8b4a0 simplified → [0,1] fraction of cell height.
    static float WaterHeightFrac(GridData& grid, int x, int y, int z);

    static bool IsWaterCell(GridData& grid, int x, int y, int z);

private:
    static bool IsSolid(GridData& grid, int x, int y, int z);
    static void EmitFace(MeshBuffer& out, int x, int y, int z, int face,
                         float u0, float v0, float u1, float v1, uint32_t color);
    // AddWaterFace @ 0x8cc70 — side/top faces use water surface height (not full cube).
    static void EmitWaterFace(MeshBuffer& out, int x, int y, int z, int face,
                              float heightFrac, float u0, float v0, float u1, float v1,
                              uint32_t color);
};

// DrawPrimitive recovered @ 0xcfa30 — uses client-state arrays:
// glVertexPointer(3, GL_FLOAT, 24, ...)
// glColorPointer(4, GL_UNSIGNED_BYTE, 24, ...)
// glTexCoordPointer(2, GL_FLOAT, 24, ...)
// stride 0x18 confirms PCT1Vertex = 24 bytes
void DrawPrimitive(unsigned int mode, const PCT1Vertex* verts, const uint16_t* indices,
                   int indexCount, int /*unused*/);
