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

// Builds visible faces for a 16³ region (cull faces against solid neighbors)
// Mirrors GridData::Region::AddMesh high-level behavior
class RegionMesher {
public:
    // region origin in world cell coords; size fixed at 16
    static void Build(GridData& grid, int originX, int originY, int originZ,
                      const std::vector<BlockDef>& defs, MeshBuffer& out,
                      const LightMap* lights = nullptr);

private:
    static bool IsSolid(GridData& grid, int x, int y, int z);
    static void EmitFace(MeshBuffer& out, int x, int y, int z, int face,
                         float u0, float v0, float u1, float v1, uint32_t color);
};

// DrawPrimitive recovered @ 0xcfa30 — uses client-state arrays:
// glVertexPointer(3, GL_FLOAT, 24, ...)
// glColorPointer(4, GL_UNSIGNED_BYTE, 24, ...)
// glTexCoordPointer(2, GL_FLOAT, 24, ...)
// stride 0x18 confirms PCT1Vertex = 24 bytes
void DrawPrimitive(unsigned int mode, const PCT1Vertex* verts, const uint16_t* indices,
                   int indexCount, int /*unused*/);
