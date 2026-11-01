#include "Mesh.h"
#include "Texture.h"
#include "TreeBuilder.h"
#include <algorithm>
#include <cstring>
#include <vector>

void MeshBuffer::Create(int maxVerts, int maxIndices) {
    verts_.clear();
    indices_.clear();
    verts_.reserve(maxVerts > 0 ? maxVerts : 4096);
    indices_.reserve(maxIndices > 0 ? maxIndices : 6144);
}

void MeshBuffer::Clear() {
    verts_.clear();
    indices_.clear();
}

void MeshBuffer::PushQuad(const PCT1Vertex verts[4]) {
    uint16_t base = (uint16_t)verts_.size();
    for (int i = 0; i < 4; ++i) verts_.push_back(verts[i]);
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

// ---------------------------------------------------------------------------
// NeedFace* recovery — template from NeedFaceXP @ 0x833d0 (1365 B)
//
// Verified control flow:
//   1. selfType = types[cellIndex]; material = table[selfType] (stride 34 at +0xe8)
//   2. neighbour in face direction (with dim wrap using +0x44)
//   3. neighType = types[neighbour]; neighMat = table[neighType]
//   4. jump table on material property byte (+0x01), values 0..0x0a
//   5. checks: material+0x02 opacity flag; state bits 0x03/0x07/0x0f
//   6. return 0 (cull), 1 (emit), 2 (special/partial)
//
// Material prop +0x01 classes observed: 0 solid, 3?, 5?, 6/7 special, 0x0a liquid-like
// LeavesQuality (+0xec) gates leaves path inside AddMesh, not inside NeedFace itself.
//
// Current implementation encodes recovered opaque/transparent rules without the
// full material table (not yet loaded as binary layout). Same rules applied to
// all six faces (NeedFaceXP/XN/YP/YN/ZP/ZN share structure; axis only changes
// neighbour offset).
// ---------------------------------------------------------------------------
int RegionMesher::NeedFace(GridData& grid, int x, int y, int z, int face) {
    int selfIdx = grid.CoordToIndex(x, y, z);
    if (selfIdx < 0) return 0;
    unsigned char selfType = grid.cellTypes[selfIdx];
    if (selfType == 0) return 0; // air has no faces

    int nx = x, ny = y, nz = z;
    switch (face) {
        case 0: nx++; break; case 1: nx--; break;
        case 2: ny++; break; case 3: ny--; break;
        case 4: nz++; break; case 5: nz--; break;
        default: return 0;
    }
    int nIdx = grid.CoordToIndex(nx, ny, nz);
    // Out of world → emit face (sky/edge)
    if (nIdx < 0) return 1;

    unsigned char nType = grid.cellTypes[nIdx];
    // Binary: if neighbour fully occludes (opaque solid same/compatible) → 0
    // Transparent/non-solid neighbour types always need face (return 1)
    // Types treated as non-occluding (recovered from IsSolid + NeedFace paths):
    //   0 air, 5 water, 10/20 leaves, flowers/mushrooms, 11 glass, 17/18 torch
    auto occludes = [](unsigned char t) -> bool {
        if (t == 0 || t == 5 || t == 11) return false;
        if (t == 10 || t == 20) return false; // leaves — LeavesQuality may change this
        if (t == 17 || t == 18) return false; // torch
        // flowers / mushrooms (decorative cross meshes, not solid cubes)
        if (t == 12 || t == 13 || t == 14) return false;
        return true; // solid cube
    };

    if (!occludes(nType)) return 1;

    // Neighbour is solid. Binary NeedFaceXP cases (verified returns):
    //   0 = cull
    //   1 = emit full face
    //   2 = special/partial (prop+0x02==0 paths, type mismatch with neigh)
    //   3 = observed in one jump-table case (state&7==3 related)
    // Without material table (+0xe8 stride 34), return-2/3 paths stay closed.
    // Solid-vs-solid → 0 (cull) matches the common opaque case.
    (void)selfType;
    return 0;
}

bool RegionMesher::IsSolid(GridData& grid, int x, int y, int z) {
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0) return false;
    unsigned char t = grid.cellTypes[idx];
    // Keep consistent with NeedFace occludes()
    if (t == 0 || t == 5 || t == 11) return false;
    if (t == 10 || t == 20 || t == 17 || t == 18) return false;
    if (t == 12 || t == 13 || t == 14) return false;
    return true;
}

void RegionMesher::EmitFace(MeshBuffer& out, int x, int y, int z, int face,
                            float u0, float v0, float u1, float v1, uint32_t color) {
    PCT1Vertex v[4];
    auto set = [&](int i, float px, float py, float pz, float u, float vv) {
        v[i].x = px; v[i].y = py; v[i].z = pz;
        v[i].color = color; v[i].u = u; v[i].v = vv;
    };
    float fx = (float)x, fy = (float)y, fz = (float)z;
    switch (face) {
        case 0:
            set(0, fx+1, fy,   fz,   u0, v1); set(1, fx+1, fy+1, fz,   u0, v0);
            set(2, fx+1, fy+1, fz+1, u1, v0); set(3, fx+1, fy,   fz+1, u1, v1); break;
        case 1:
            set(0, fx, fy,   fz+1, u0, v1); set(1, fx, fy+1, fz+1, u0, v0);
            set(2, fx, fy+1, fz,   u1, v0); set(3, fx, fy,   fz,   u1, v1); break;
        case 2:
            set(0, fx,   fy+1, fz,   u0, v1); set(1, fx,   fy+1, fz+1, u0, v0);
            set(2, fx+1, fy+1, fz+1, u1, v0); set(3, fx+1, fy+1, fz,   u1, v1); break;
        case 3:
            set(0, fx,   fy, fz+1, u0, v1); set(1, fx,   fy, fz,   u0, v0);
            set(2, fx+1, fy, fz,   u1, v0); set(3, fx+1, fy, fz+1, u1, v1); break;
        case 4:
            set(0, fx+1, fy,   fz+1, u0, v1); set(1, fx+1, fy+1, fz+1, u0, v0);
            set(2, fx,   fy+1, fz+1, u1, v0); set(3, fx,   fy,   fz+1, u1, v1); break;
        case 5:
            set(0, fx,   fy,   fz, u0, v1); set(1, fx,   fy+1, fz, u0, v0);
            set(2, fx+1, fy+1, fz, u1, v0); set(3, fx+1, fy,   fz, u1, v1); break;
        default: return;
    }
    out.PushQuad(v);
}

// ---------------------------------------------------------------------------
// NON-ORIGINAL: EmitGreedyQuad is NOT present in the binary mesher path.
// Region::AddMesh emits faces individually via NeedFace* + AddBlockFace.
// Kept isolated for reference only; RegionMesher::Build must not call it.
// ---------------------------------------------------------------------------
#if 0  // disabled — non-original greedy path
static void EmitGreedyQuad(MeshBuffer& out, int face,
                           int x0, int y0, int z0, int du, int dv,
                           float uu0, float vv0, float uu1, float vv1, uint32_t color) {
    PCT1Vertex v[4];
    auto set = [&](int i, float px, float py, float pz, float u, float vv) {
        v[i].x = px; v[i].y = py; v[i].z = pz;
        v[i].color = color; v[i].u = u; v[i].v = vv;
    };
    // Tile UVs across the merged area (repeat atlas tile)
    float tile = 1.f / 16.f;
    // For simplicity keep single-tile UV range scaled by size (looks stretched on multi-tile merges)
    // Better: UV length = du/dv so texture repeats
    float su = (float)du, sv = (float)dv;
    float u0 = uu0, v0 = vv0, u1 = uu0 + su * (uu1 - uu0), v1 = vv0 + sv * (vv1 - vv0);

    switch (face) {
        case 0: // +X  plane at x0+1; axes y,z
            set(0, x0+1, y0,      z0,      u0, v1);
            set(1, x0+1, y0+dv,   z0,      u0, v0);
            set(2, x0+1, y0+dv,   z0+du,   u1, v0);
            set(3, x0+1, y0,      z0+du,   u1, v1);
            break;
        case 1: // -X
            set(0, x0, y0,      z0+du, u0, v1);
            set(1, x0, y0+dv,   z0+du, u0, v0);
            set(2, x0, y0+dv,   z0,    u1, v0);
            set(3, x0, y0,      z0,    u1, v1);
            break;
        case 2: // +Y  axes x,z
            set(0, x0,    y0+1, z0,    u0, v1);
            set(1, x0,    y0+1, z0+dv, u0, v0);
            set(2, x0+du, y0+1, z0+dv, u1, v0);
            set(3, x0+du, y0+1, z0,    u1, v1);
            break;
        case 3: // -Y
            set(0, x0,    y0, z0+dv, u0, v1);
            set(1, x0,    y0, z0,    u0, v0);
            set(2, x0+du, y0, z0,    u1, v0);
            set(3, x0+du, y0, z0+dv, u1, v1);
            break;
        case 4: // +Z  axes x,y
            set(0, x0+du, y0,    z0+1, u0, v1);
            set(1, x0+du, y0+dv, z0+1, u0, v0);
            set(2, x0,    y0+dv, z0+1, u1, v0);
            set(3, x0,    y0,    z0+1, u1, v1);
            break;
        case 5: // -Z
            set(0, x0,    y0,    z0, u0, v1);
            set(1, x0,    y0+dv, z0, u0, v0);
            set(2, x0+du, y0+dv, z0, u1, v0);
            set(3, x0+du, y0,    z0, u1, v1);
            break;
        default: return;
    }
    out.PushQuad(v);
}

#endif // non-original greedy

void RegionMesher::Build(GridData& grid, int ox, int oy, int oz,
                         const std::vector<BlockDef>& defs, MeshBuffer& out,
                         const LightMap* lights) {
    // Per-face emission (no greedy merge) — correct single-tile UVs from BlockDef atlas indices
    out.Clear();
    const int S = 16;
    for (int z = 0; z < S; ++z)
    for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
        int wx = ox + x, wy = oy + y, wz = oz + z;
        int cidx = grid.CoordToIndex(wx, wy, wz);
        if (cidx < 0) continue;
        unsigned char t = grid.cellTypes[cidx];
        if (t == 0) continue; // air

        // Skip non-cube decorative for now (still emit leaves/wood)
        for (int face = 0; face < 6; ++face) {
            int nx = wx, ny = wy, nz = wz;
            if (face == 0) nx++; else if (face == 1) nx--;
            else if (face == 2) ny++; else if (face == 3) ny--;
            else if (face == 4) nz++; else nz--;
            // NeedFace* (binary): 0=cull, 1/2=emit
            if (NeedFace(grid, wx, wy, wz, face) == 0)
                continue;
            float u0=0,v0=0,u1=1.f/16.f,v1=1.f/16.f;
            if (t < defs.size()) GetFaceUV(defs[t], face, u0, v0, u1, v1);
            else Texture::TileUV((int)t % 16, (int)t / 16, u0, v0, u1, v1);
            uint32_t col = 0xFFFFFFFFu;
            if (lights) col = LightMap::GetLightColor(lights->GetFaceLight(wx, wy, wz, face));
            EmitFace(out, wx, wy, wz, face, u0, v0, u1, v1, col);
        }
    }
}


// DrawPrimitive @ 0xcfa30 — GLES client arrays, PCT1Vertex stride 24
#if defined(__ANDROID__)
#include <GLES/gl.h>
void DrawPrimitive(unsigned int mode, const PCT1Vertex* verts, const uint16_t* indices,
                   int indexCount, int) {
    if (!verts || !indices || indexCount <= 0) return;
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    const char* base = reinterpret_cast<const char*>(verts);
    glVertexPointer(3, GL_FLOAT, 24, base + 0);
    // GLES: UNSIGNED_BYTE colors are normalized automatically
    glColorPointer(4, GL_UNSIGNED_BYTE, 24, base + 12);
    glTexCoordPointer(2, GL_FLOAT, 24, base + 16);
    glDrawElements(mode, indexCount, GL_UNSIGNED_SHORT, indices);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}
#else
void DrawPrimitive(unsigned int, const PCT1Vertex*, const uint16_t*, int, int) {}
#endif
