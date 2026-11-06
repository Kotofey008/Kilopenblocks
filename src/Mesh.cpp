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

bool RegionMesher::IsWaterCell(GridData& grid, int x, int y, int z) {
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0) return false;
    if (grid.cellTypes[idx] == 5) return true;
    if (idx < (int)grid.cellStates.size() && (grid.cellStates[idx] & 0x3fc0) != 0)
        return true;
    return false;
}

// GetWaterBlockHeight @ 0x8b4a0
// Units 0..16 (MaxWaterHeight returns 0x10). Binary:
//   water above + flow → 0x80 (full block in <<7 mesh space = 16/16)
//   level==0 (source): height = 0x0e (14)
//   level>0: height = 0x0f - 2*level  (13,11,…,1)
float RegionMesher::WaterHeightFrac(GridData& grid, int x, int y, int z) {
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0) return 0.f;
    unsigned short st = (idx < (int)grid.cellStates.size()) ? grid.cellStates[idx] : 0;
    if ((st & 0x3fc0) == 0 && grid.cellTypes[idx] != 5)
        return 0.f;
    if (IsWaterCell(grid, x, y + 1, z))
        return 1.f; // 0x80 path — full column under water above
    int level = (int)((st & 0x1c00) >> 10);
    // Water under a horizontal stream: keep normal height unless dead-end
    // (no child stream at level+1) and level != 7 → then level-7 look (1/16).
    // Corner-max still joins neighbours of any level continuously.
    if (level > 0 && level < 7 && IsWaterCell(grid, x, y - 1, z)) {
        bool hasChildStream = false;
        static const int offs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (auto& o : offs) {
            if (!IsWaterCell(grid, x + o[0], y, z + o[1])) continue;
            int ni = grid.CoordToIndex(x + o[0], y, z + o[1]);
            if (ni < 0 || ni >= (int)grid.cellStates.size()) continue;
            int nl = (int)((grid.cellStates[ni] & 0x1c00) >> 10);
            if (nl == level + 1) { hasChildStream = true; break; }
        }
        if (!hasChildStream)
            return 1.f / 16.f;
    }
    // Source (0): 0x0e; flowing: 0x0f - 2*level  (units out of 16)
    int units = (level == 0) ? 0xe : (0xf - 2 * level);
    if (units < 1) units = 1;
    if (units > 16) units = 16;
    return (float)units / 16.f;
}

// Corner height for seamless liquid surface (GetWaterBlockFaceYP samples
// GetWaterBlockHeight at neighbour offsets). Corner (cx,cz) ∈ {0,1}² is the
// max height among the up-to-4 water cells that touch that vertical edge.
static float WaterCornerHeight(GridData& grid, int x, int y, int z, int cx, int cz) {
    float best = 0.f;
    bool any = false;
    for (int ox = x + cx - 1; ox <= x + cx; ++ox) {
        for (int oz = z + cz - 1; oz <= z + cz; ++oz) {
            if (!RegionMesher::IsWaterCell(grid, ox, y, oz)) continue;
            float h = RegionMesher::WaterHeightFrac(grid, ox, y, oz);
            if (!any || h > best) { best = h; any = true; }
        }
    }
    if (!any)
        return RegionMesher::WaterHeightFrac(grid, x, y, z);
    return best;
}

// WaterNeedFace* family @ 0x855c0–0x867f0
// Core rule (YN/YP recovered): if neighbour is also water and flow allowed both
// ways → cull (return 0). Otherwise emit against air / solid / empty.
int RegionMesher::WaterNeedFace(GridData& grid, int x, int y, int z, int face) {
    if (!IsWaterCell(grid, x, y, z)) return 0;
    int nx = x, ny = y, nz = z;
    switch (face) {
        case 0: nx++; break; case 1: nx--; break;
        case 2: ny++; break; case 3: ny--; break;
        case 4: nz++; break; case 5: nz--; break;
        default: return 0;
    }
    int nIdx = grid.CoordToIndex(nx, ny, nz);
    if (nIdx < 0) return 1; // world edge
    if (IsWaterCell(grid, nx, ny, nz)) {
        // Top face: still emit if this cell is lower than full and neighbour
        // is only partial — but binary typically culls water|water entirely
        // when both are fluid (YN/YP IsFlowAllowed both ways → false).
        // Side faces between water: cull.
        if (face == 2) {
            // YP: emit only if our height is below full and above has lower
            // surface — for connected full column, cull.
            float h = WaterHeightFrac(grid, x, y, z);
            if (h >= 0.999f) return 0;
            // partial top against water above still usually culled if above exists
            return 0;
        }
        return 0;
    }
    // Neighbour air / solid / non-water → emit liquid face
    return 1;
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

// AddWaterFace @ 0x8cc70 — per-corner heights (GetWaterBlockFaceYP style) so
// neighbouring liquid cells share edge elevations (no cracks). YP is emitted
// with both windings so the surface is visible from below water.
void RegionMesher::EmitWaterFace(GridData& grid, MeshBuffer& out, int x, int y, int z, int face,
                                 float u0, float v0, float u1, float v1, uint32_t color) {
    float h00 = WaterCornerHeight(grid, x, y, z, 0, 0);
    float h10 = WaterCornerHeight(grid, x, y, z, 1, 0);
    float h01 = WaterCornerHeight(grid, x, y, z, 0, 1);
    float h11 = WaterCornerHeight(grid, x, y, z, 1, 1);
    float selfH = WaterHeightFrac(grid, x, y, z);
    if (selfH <= 0.f && h00 <= 0.f && h10 <= 0.f && h01 <= 0.f && h11 <= 0.f)
        return;

    PCT1Vertex v[4];
    auto set = [&](int i, float px, float py, float pz, float u, float vv) {
        v[i].x = px; v[i].y = py; v[i].z = pz;
        v[i].color = color; v[i].u = u; v[i].v = vv;
    };
    float fx = (float)x, fy = (float)y, fz = (float)z;

    auto push = [&]() { out.PushQuad(v); };
    auto pushFlip = [&]() {
        // reverse winding for back face
        PCT1Vertex t = v[1]; v[1] = v[3]; v[3] = t;
        out.PushQuad(v);
    };

    switch (face) {
        case 0: { // +X  edge heights h10, h11
            float t0 = fy + h10, t1 = fy + h11;
            float vt0 = v0 + (v1 - v0) * (1.f - h10);
            float vt1 = v0 + (v1 - v0) * (1.f - h11);
            set(0, fx+1, fy, fz,   u0, v1); set(1, fx+1, t0, fz,   u0, vt0);
            set(2, fx+1, t1, fz+1, u1, vt1); set(3, fx+1, fy, fz+1, u1, v1);
            push(); break;
        }
        case 1: { // -X  h00, h01
            float t0 = fy + h01, t1 = fy + h00;
            float vt0 = v0 + (v1 - v0) * (1.f - h01);
            float vt1 = v0 + (v1 - v0) * (1.f - h00);
            set(0, fx, fy, fz+1, u0, v1); set(1, fx, t0, fz+1, u0, vt0);
            set(2, fx, t1, fz,   u1, vt1); set(3, fx, fy, fz,   u1, v1);
            push(); break;
        }
        case 2: { // +Y surface — dual sided
            set(0, fx,   fy+h00, fz,   u0, v1);
            set(1, fx,   fy+h01, fz+1, u0, v0);
            set(2, fx+1, fy+h11, fz+1, u1, v0);
            set(3, fx+1, fy+h10, fz,   u1, v1);
            push();
            // back face for underwater view looking up
            set(0, fx,   fy+h00, fz,   u0, v1);
            set(1, fx+1, fy+h10, fz,   u1, v1);
            set(2, fx+1, fy+h11, fz+1, u1, v0);
            set(3, fx,   fy+h01, fz+1, u0, v0);
            push();
            break;
        }
        case 3: // -Y bottom
            set(0, fx,   fy, fz+1, u0, v1); set(1, fx,   fy, fz,   u0, v0);
            set(2, fx+1, fy, fz,   u1, v0); set(3, fx+1, fy, fz+1, u1, v1);
            push(); break;
        case 4: { // +Z  h01, h11
            float t0 = fy + h11, t1 = fy + h01;
            float vt0 = v0 + (v1 - v0) * (1.f - h11);
            float vt1 = v0 + (v1 - v0) * (1.f - h01);
            set(0, fx+1, fy, fz+1, u0, v1); set(1, fx+1, t0, fz+1, u0, vt0);
            set(2, fx,   t1, fz+1, u1, vt1); set(3, fx,   fy, fz+1, u1, v1);
            push(); break;
        }
        case 5: { // -Z  h00, h10
            float t0 = fy + h00, t1 = fy + h10;
            float vt0 = v0 + (v1 - v0) * (1.f - h00);
            float vt1 = v0 + (v1 - v0) * (1.f - h10);
            set(0, fx,   fy, fz, u0, v1); set(1, fx,   t0, fz, u0, vt0);
            set(2, fx+1, t1, fz, u1, vt1); set(3, fx+1, fy, fz, u1, v1);
            push(); break;
        }
        default: return;
    }
    (void)pushFlip;
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
    Build(grid, ox, oy, oz, defs, out, nullptr, lights);
}

void RegionMesher::Build(GridData& grid, int ox, int oy, int oz,
                         const std::vector<BlockDef>& defs, MeshBuffer& solidOut,
                         MeshBuffer* waterOut, const LightMap* lights) {
    // Region::AddMesh: solids → NeedFace* + AddBlockFace;
    // liquids → WaterNeedFace* + AddWaterFace (separate translucent pass).
    solidOut.Clear();
    if (waterOut) waterOut->Clear();
    const int S = 16;
    static const int kFaceToMat[6] = { 0, 3, 1, 4, 2, 5 };

    for (int z = 0; z < S; ++z)
    for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
        int wx = ox + x, wy = oy + y, wz = oz + z;
        int cidx = grid.CoordToIndex(wx, wy, wz);
        if (cidx < 0) continue;
        unsigned char t = grid.cellTypes[cidx];
        if (t == 0) continue; // air

        const bool liquid = IsWaterCell(grid, wx, wy, wz) || t == 5;

        if (liquid) {
            MeshBuffer& dst = waterOut ? *waterOut : solidOut;
            float h = WaterHeightFrac(grid, wx, wy, wz);
            if (h <= 0.f) continue;
            for (int face = 0; face < 6; ++face) {
                if (WaterNeedFace(grid, wx, wy, wz, face) == 0)
                    continue;
                float u0=0,v0=0,u1=1.f/16.f,v1=1.f/16.f;
                const Material* mat = grid.GetMaterial(5);
                if (mat) {
                    int mi = kFaceToMat[face];
                    Texture::TileUV((int)mat->uFace(mi), (int)mat->vFace(mi), u0, v0, u1, v1);
                } else {
                    Texture::TileUV(6, 0, u0, v0, u1, v1); // blocks.txt water UV
                }
                uint32_t col = 0xA0FFFFFF;
                if (lights) {
                    uint32_t lc = LightMap::GetLightColor(lights->GetFaceLight(wx, wy, wz, face));
                    col = (lc & 0x00FFFFFFu) | 0xA0000000u;
                }
                EmitWaterFace(grid, dst, wx, wy, wz, face, u0, v0, u1, v1, col);
            }
            continue;
        }

        // Solid / leaves / etc.
        for (int face = 0; face < 6; ++face) {
            if (NeedFace(grid, wx, wy, wz, face) == 0)
                continue;
            float u0=0,v0=0,u1=1.f/16.f,v1=1.f/16.f;
            const Material* mat = grid.GetMaterial((int)t);
            if (mat) {
                int mi = kFaceToMat[face];
                Texture::TileUV((int)mat->uFace(mi), (int)mat->vFace(mi), u0, v0, u1, v1);
            } else if (t < defs.size()) {
                GetFaceUV(defs[t], face, u0, v0, u1, v1);
            } else {
                Texture::TileUV((int)t % 16, (int)t / 16, u0, v0, u1, v1);
            }
            uint32_t col = 0xFFFFFFFFu;
            if (lights) {
                col = LightMap::GetLightColor(lights->GetFaceLight(wx, wy, wz, face));
                if ((col & 0xFFu) < 32 && ((col >> 8) & 0xFFu) < 32)
                    col = 0xFFC0C0C0u;
            }
            EmitFace(solidOut, wx, wy, wz, face, u0, v0, u1, v1, col);
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
