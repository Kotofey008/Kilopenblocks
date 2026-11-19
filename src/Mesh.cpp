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
    // NeedFaceXP @ 0x833d0 — material class jump (simplified, structure preserved).
    //
    // Non-solid rendering (no full-cube solid faces / mutual face cull like water):
    //   • alpha (All / glass FTL) 
    //   • class 0 with height ∉ {0,16} (slabs)
    //   • classes 1..10 (cross, torch, ladder, fence, stairs, door, lamp, gate, trapdoor)
    // Neighbours must not keep faces that these cells cover; pairs of such
    // cells cull shared faces the same way WaterNeedFace culls water|water.
    int selfIdx = grid.CoordToIndex(x, y, z);
    if (selfIdx < 0) return 0;
    unsigned char selfType = grid.cellTypes[selfIdx];
    if (selfType == 0) return 0;

    int nx = x, ny = y, nz = z;
    switch (face) {
        case 0: nx++; break; case 1: nx--; break;
        case 2: ny++; break; case 3: ny--; break;
        case 4: nz++; break; case 5: nz--; break;
        default: return 0;
    }
    int nIdx = grid.CoordToIndex(nx, ny, nz);
    if (nIdx < 0) return 1;

    unsigned char nType = grid.cellTypes[nIdx];
    const Material* selfMat = grid.GetMaterial((int)selfType);
    const Material* nMat = grid.GetMaterial((int)nType);

    auto isFullOpaqueSolid = [](unsigned char t, const Material* mat) -> bool {
        if (t == 0) return false;
        if (!mat) {
            if (t == 5 || t == 11 || t == 10 || t == 20) return false;
            if (t == 12 || t == 13 || t == 14 || t == 17 || t == 18) return false;
            return true;
        }
        if (mat->prop01() != 0) return false;
        if (!mat->col()) return false;
        if (mat->allFaces()) return false;
        if (mat->height() != 0x10) return false;
        if (mat->flowPerm(0) != 0) return false; // glass
        return true;
    };

    // Specialized / alpha / partial — not drawn as full solid cubes.
    auto isNonSolidModel = [](unsigned char t, const Material* mat) -> bool {
        if (t == 0) return false;
        if (!mat) {
            if (t == 5 || t == 11 || t == 10 || t == 20) return true;
            if (t == 12 || t == 13 || t == 14 || t == 17 || t == 18) return true;
            return false;
        }
        uint8_t cls = mat->prop01();
        if (cls >= 1 && cls <= 10) return true;
        if (cls == 0) {
            uint8_t ht = mat->height();
            if (ht != 0 && ht != 0x10) return true;     // slab
            if (mat->allFaces()) return true;           // leaves alpha
            if (mat->flowPerm(0) != 0) return true;     // glass alpha
            if (!mat->col()) return true;               // water-like
        }
        return false;
    };

    // Neighbour covers this face → cull (no see-through hole).
    auto coversFace = [&](unsigned char t, const Material* mat) -> bool {
        if (t == 0) return false;
        if (isFullOpaqueSolid(t, mat)) return true;
        if (!mat) return false;
        uint8_t cls = mat->prop01();
        // Decorative cross/torch never cover
        if (cls == 1 || cls == 2 || cls == 4) return false;
        // Ladder / fence / stairs / door / gate / trapdoor / slab / alpha with Col
        if (cls >= 3 && cls <= 10) return mat->col() || cls == 3;
        if (cls == 0) {
            if (!mat->col()) return false; // water
            if (mat->allFaces()) return true;
            if (mat->flowPerm(0) != 0) return true; // glass
            uint8_t ht = mat->height();
            if (ht != 0 && ht != 0x10) return true; // slab
            return ht == 0x10;
        }
        return false;
    };

    const bool selfNS = isNonSolidModel(selfType, selfMat);
    const bool neighNS = isNonSolidModel(nType, nMat);

    // Like water|water: non-solid models do not emit faces against each other.
    if (selfNS && neighNS)
        return 0;

    // Neighbour geometry covers this face → cull.
    if (coversFace(nType, nMat)) {
        // Partial self (slab/stairs/door/trapdoor) still needs geometry vs air only;
        // against a covering neighbour there is no exposed face.
        return 0;
    }

    // Exposed to air / non-covering neighbour.
    if (selfMat) {
        uint8_t cls = selfMat->prop01();
        uint8_t ht = selfMat->height();
        if (cls == 6 || cls == 7 || cls == 10) return 2;
        if (cls == 0 && ht != 0 && ht < 0x10) return 2;
        if (selfNS) return 1; // glass, leaves, fence, … emit against air
    }
    if (isFullOpaqueSolid(selfType, selfMat))
        return 1;
    return 1;
}

bool RegionMesher::IsSolid(GridData& grid, int x, int y, int z) {
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0) return false;
    unsigned char t = grid.cellTypes[idx];
    if (t == 0) return false;
    const Material* mat = grid.GetMaterial((int)t);
    if (mat) {
        // Collision solid: Col set and not a pure decoration class
        uint8_t cls = mat->prop01();
        if (cls == 1 || cls == 2 || cls == 3) return false; // flower/torch/ladder
        if (!mat->col()) return false;
        return true;
    }
    if (t == 5 || t == 11) return false;
    if (t == 10 || t == 20 || t == 17 || t == 18) return false;
    if (t == 12 || t == 13 || t == 14) return false;
    return true;
}

bool RegionMesher::IsWaterCell(GridData& grid, int x, int y, int z) {
    // Ghidra: liquid water iff (state & 0x2000). Type is normally 0.
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0 || idx >= (int)grid.cellStates.size()) return false;
    return (grid.cellStates[idx] & 0x2000) != 0;
}

bool RegionMesher::IsLavaCell(GridData& grid, int x, int y, int z) {
    // Ghidra Region::AddMesh: (state & 0x3c0) != 0 && (state & 0x2000) == 0
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0 || idx >= (int)grid.cellStates.size()) return false;
    unsigned short st = grid.cellStates[idx];
    return (st & 0x03c0) != 0 && (st & 0x2000) == 0;
}

int RegionMesher::CellLiquidType(GridData& grid, int x, int y, int z) {
    if (IsWaterCell(grid, x, y, z)) return 0; // LiquidWater
    if (IsLavaCell(grid, x, y, z)) return 1;  // LiquidLava
    return -1;
}

// GetWaterBlockHeight @ 0x8b4a0
// Units 0..16 (MaxWaterHeight returns 0x10). Binary:
//   water above + flow → 0x80 (full block in <<7 mesh space = 16/16)
//   level==0 (source): height = 0x0e (14)
//   level>0: height = 0x0f - 2*level  (13,11,…,1)
float RegionMesher::WaterHeightFrac(GridData& grid, int x, int y, int z) {
    // GetWaterBlockHeight @ 0x8b4a0
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0) return 0.f;
    unsigned short st = (idx < (int)grid.cellStates.size()) ? grid.cellStates[idx] : 0;
    if ((st & 0x2000) == 0 && (st & 0x03c0) == 0)
        return 0.f;
    int lt = CellLiquidType(grid, x, y, z);
    // Full column if same liquid type above
    if (lt == 0 && IsWaterCell(grid, x, y + 1, z)) return 1.f;
    if (lt == 1 && IsLavaCell(grid, x, y + 1, z)) return 1.f;
    int level = (int)((st & 0x1c00) >> 10);
    if (level > 0 && level < 7) {
        bool belowSame = (lt == 0) ? IsWaterCell(grid, x, y - 1, z)
                                   : IsLavaCell(grid, x, y - 1, z);
        if (belowSame) {
            bool hasChild = false;
            static const int offs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& o : offs) {
                int nx = x + o[0], nz = z + o[1];
                if (CellLiquidType(grid, nx, y, nz) != lt) continue;
                int ni = grid.CoordToIndex(nx, y, nz);
                if (ni < 0 || ni >= (int)grid.cellStates.size()) continue;
                int nl = (int)((grid.cellStates[ni] & 0x1c00) >> 10);
                if (nl == level + 1) { hasChild = true; break; }
            }
            if (!hasChild) return 1.f / 16.f;
        }
    }
    int units = (level == 0) ? 0xe : (0xf - 2 * level);
    if (units < 1) units = 1;
    if (units > 16) units = 16;
    return (float)units / 16.f;
}

// Corner height for seamless liquid surface (GetWaterBlockFaceYP samples
// GetWaterBlockHeight at neighbour offsets). Corner (cx,cz) ∈ {0,1}² is the
// max height among the up-to-4 water cells that touch that vertical edge.
static float WaterCornerHeight(GridData& grid, int x, int y, int z, int cx, int cz) {
    // Match liquid type of centre cell (water corners only with water, lava with lava)
    int lt = RegionMesher::CellLiquidType(grid, x, y, z);
    float best = 0.f;
    bool any = false;
    for (int ox = x + cx - 1; ox <= x + cx; ++ox) {
        for (int oz = z + cz - 1; oz <= z + cz; ++oz) {
            if (RegionMesher::CellLiquidType(grid, ox, y, oz) != lt) continue;
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
    // WaterNeedFace* family: cull against same LiquidType when mutual flow allows.
    int lt = CellLiquidType(grid, x, y, z);
    if (lt < 0) return 0;
    int nx = x, ny = y, nz = z;
    switch (face) {
        case 0: nx++; break; case 1: nx--; break;
        case 2: ny++; break; case 3: ny--; break;
        case 4: nz++; break; case 5: nz--; break;
        default: return 0;
    }
    int nIdx = grid.CoordToIndex(nx, ny, nz);
    if (nIdx < 0) return 1;
    int nlt = CellLiquidType(grid, nx, ny, nz);
    if (nlt == lt) {
        // Same liquid: cull shared face (binary mutual IsFlowAllowed → no face)
        // YP: still no face if both fluid of same type (connected surface)
        return 0;
    }
    // Neighbour different liquid / air / solid → emit
    return 1;
}

// ---------------------------------------------------------------------------
// Face index maps: our 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
// binary AddBlockFace: 0=XP 1=YP 2=ZP 3=XN 4=YN 5=ZN
// Material UV slots follow binary order XP YP ZP XN YN ZN.
// ---------------------------------------------------------------------------
int RegionMesher::OurToBinFace(int ourFace) {
    static const int m[6] = { 0, 3, 1, 4, 2, 5 };
    return (ourFace >= 0 && ourFace < 6) ? m[ourFace] : 0;
}
int RegionMesher::BinToOurFace(int binFace) {
    static const int m[6] = { 0, 2, 4, 1, 3, 5 };
    return (binFace >= 0 && binFace < 6) ? m[binFace] : 0;
}

void RegionMesher::FaceUV(const Material& mat, int ourFace, float& u0, float& v0, float& u1, float& v1) {
    int mi = OurToBinFace(ourFace);
    Texture::TileUV((int)mat.uFace(mi), (int)mat.vFace(mi), u0, v0, u1, v1);
}

uint32_t RegionMesher::FaceColor(const LightMap* lights, int x, int y, int z, int face) {
    if (!lights) return 0xFFFFFFFFu;
    uint32_t col = LightMap::GetLightColor(lights->GetFaceLight(x, y, z, face));
    if ((col & 0xFFu) < 32 && ((col >> 8) & 0xFFu) < 32)
        col = 0xFFC0C0C0u;
    return col;
}

void RegionMesher::EmitFace(MeshBuffer& out, int x, int y, int z, int face,
                            float u0, float v0, float u1, float v1, uint32_t color) {
    EmitAxisFace(out, (float)x, (float)y, (float)z,
                 (float)x + 1.f, (float)y + 1.f, (float)z + 1.f,
                 face, u0, v0, u1, v1, color);
}

// Emit one face of an AABB [x0,x1]×[y0,y1]×[z0,z1] (world cell units).
void RegionMesher::EmitAxisFace(MeshBuffer& out, float x0, float y0, float z0,
                                float x1, float y1, float z1, int ourFace,
                                float u0, float v0, float u1, float v1, uint32_t color) {
    PCT1Vertex v[4];
    auto set = [&](int i, float px, float py, float pz, float u, float vv) {
        v[i].x = px; v[i].y = py; v[i].z = pz;
        v[i].color = color; v[i].u = u; v[i].v = vv;
    };
    switch (ourFace) {
        case 0: // +X at x1
            set(0, x1, y0, z0, u0, v1); set(1, x1, y1, z0, u0, v0);
            set(2, x1, y1, z1, u1, v0); set(3, x1, y0, z1, u1, v1); break;
        case 1: // -X at x0
            set(0, x0, y0, z1, u0, v1); set(1, x0, y1, z1, u0, v0);
            set(2, x0, y1, z0, u1, v0); set(3, x0, y0, z0, u1, v1); break;
        case 2: // +Y at y1
            set(0, x0, y1, z0, u0, v1); set(1, x0, y1, z1, u0, v0);
            set(2, x1, y1, z1, u1, v0); set(3, x1, y1, z0, u1, v1); break;
        case 3: // -Y at y0
            set(0, x0, y0, z1, u0, v1); set(1, x0, y0, z0, u0, v0);
            set(2, x1, y0, z0, u1, v0); set(3, x1, y0, z1, u1, v1); break;
        case 4: // +Z at z1
            set(0, x1, y0, z1, u0, v1); set(1, x1, y1, z1, u0, v0);
            set(2, x0, y1, z1, u1, v0); set(3, x0, y0, z1, u1, v1); break;
        case 5: // -Z at z0
            set(0, x0, y0, z0, u0, v1); set(1, x0, y1, z0, u0, v0);
            set(2, x1, y1, z0, u1, v0); set(3, x1, y0, z0, u1, v1); break;
        default: return;
    }
    out.PushQuad(v);
}

// AddCross @ 0x8d750 — two crossed quads (or PushTwoSidedQuads for class 1).
// UV from mat[+0x0e]/[+0x14] (ZP U/V tile). Position offset XT/YT/ZT (signed bytes).
// Class 1: two-sided single pair; class 4: PushQuads + PushBackQuads.
void RegionMesher::AddCross(MeshBuffer& out, int x, int y, int z,
                            const Material& mat, const LightMap* lights) {
    float u0, v0, u1, v1;
    // binary uses param_4[0xe] and [0x14] = Uzp / Vzp tiles
    Texture::TileUV((int)mat.raw[0x0e], (int)mat.raw[0x14], u0, v0, u1, v1);
    // XT YT ZT are signed offsets in 1/16 cell units (char at +0x09..+0x0b)
    float ox = (float)x + (int8_t)mat.raw[0x09] / 16.f;
    float oy = (float)y + (int8_t)mat.raw[0x0a] / 16.f;
    float oz = (float)z + (int8_t)mat.raw[0x0b] / 16.f;
    uint32_t col = FaceColor(lights, x, y, z, 2);
    // Cross in XZ: plane diagonals spanning full cell height
    PCT1Vertex q[4];
    auto push = [&](float x0, float y0, float z0, float x1, float y1, float z1,
                    float x2, float y2, float z2, float x3, float y3, float z3) {
        q[0] = {x0,y0,z0,col,u0,v1}; q[1] = {x1,y1,z1,col,u0,v0};
        q[2] = {x2,y2,z2,col,u1,v0}; q[3] = {x3,y3,z3,col,u1,v1};
        out.PushQuad(q);
    };
    // Diagonal 1: (0,0)-(1,1)  and back-face for two-sided
    push(ox, oy, oz,  ox, oy+1, oz,  ox+1, oy+1, oz+1,  ox+1, oy, oz+1);
    push(ox+1, oy, oz+1,  ox+1, oy+1, oz+1,  ox, oy+1, oz,  ox, oy, oz);
    // Diagonal 2: (0,1)-(1,0)
    push(ox+1, oy, oz,  ox+1, oy+1, oz,  ox, oy+1, oz+1,  ox, oy, oz+1);
    push(ox, oy, oz+1,  ox, oy+1, oz+1,  ox+1, oy+1, oz,  ox+1, oy, oz);
}

// AddTorch @ 0x8db10 — thin 6-face stem; orientation from state&7 via rodata table.
// Recovered: PushQuads with 6 faces, UV from mat[+0x0e]/[+0x14], base at cell origin.
void RegionMesher::AddTorch(MeshBuffer& out, GridData& grid, int x, int y, int z,
                            const Material& mat, const LightMap* lights) {
    int idx = grid.CoordToIndex(x, y, z);
    unsigned short st = (idx >= 0 && idx < (int)grid.cellStates.size()) ? grid.cellStates[idx] : 0;
    int orient = st & 7; // 0=floor, 1..4 walls (binary table)
    float u0, v0, u1, v1;
    Texture::TileUV((int)mat.raw[0x0e], (int)mat.raw[0x14], u0, v0, u1, v1);
    uint32_t col = FaceColor(lights, x, y, z, 2);
    // Stem cross-section ~2/16, height ~10/16 standing; wall-mounted tilt simplified
    float cx = (float)x + 0.5f, cy = (float)y, cz = (float)z + 0.5f;
    float hw = 1.f / 16.f; // half width
    float h = 10.f / 16.f;
    float y0 = cy, y1 = cy + h;
    if (orient >= 1 && orient <= 4) {
        // wall torch: shorter stick protruding from face
        h = 8.f / 16.f;
        y0 = cy + 3.f / 16.f;
        y1 = y0 + h;
        float d = 3.f / 16.f;
        if (orient == 1) { cx = (float)x + d; }      // +X wall inward
        else if (orient == 2) { cx = (float)x + 1.f - d; }
        else if (orient == 3) { cz = (float)z + d; }
        else { cz = (float)z + 1.f - d; }
    }
    // four side faces + top
    EmitAxisFace(out, cx-hw, y0, cz-hw, cx+hw, y1, cz+hw, 0, u0, v0, u1, v1, col);
    EmitAxisFace(out, cx-hw, y0, cz-hw, cx+hw, y1, cz+hw, 1, u0, v0, u1, v1, col);
    EmitAxisFace(out, cx-hw, y0, cz-hw, cx+hw, y1, cz+hw, 4, u0, v0, u1, v1, col);
    EmitAxisFace(out, cx-hw, y0, cz-hw, cx+hw, y1, cz+hw, 5, u0, v0, u1, v1, col);
    EmitAxisFace(out, cx-hw, y0, cz-hw, cx+hw, y1, cz+hw, 2, u0, v0, u1, v1, col);
}

// AddLadder @ 0x8dc50 — single plane against wall from state&7.
void RegionMesher::AddLadder(MeshBuffer& out, GridData& grid, int x, int y, int z,
                             const Material& mat, const LightMap* lights) {
    int idx = grid.CoordToIndex(x, y, z);
    unsigned short st = (idx >= 0 && idx < (int)grid.cellStates.size()) ? grid.cellStates[idx] : 0;
    int orient = st & 7;
    float u0, v0, u1, v1;
    Texture::TileUV((int)mat.raw[0x0e], (int)mat.raw[0x14], u0, v0, u1, v1);
    uint32_t col = FaceColor(lights, x, y, z, 2);
    float fx = (float)x, fy = (float)y, fz = (float)z;
    float t = 1.f / 16.f; // thickness off wall
    PCT1Vertex q[4];
    auto push = [&](float x0, float y0, float z0, float x1, float y1, float z1,
                    float x2, float y2, float z2, float x3, float y3, float z3) {
        q[0] = {x0,y0,z0,col,u0,v1}; q[1] = {x1,y1,z1,col,u0,v0};
        q[2] = {x2,y2,z2,col,u1,v0}; q[3] = {x3,y3,z3,col,u1,v1};
        out.PushQuad(q);
        // back face
        q[0] = {x3,y3,z3,col,u1,v1}; q[1] = {x2,y2,z2,col,u1,v0};
        q[2] = {x1,y1,z1,col,u0,v0}; q[3] = {x0,y0,z0,col,u0,v1};
        out.PushQuad(q);
    };
    // orient 0..3 ≈ -Z +Z -X +X (matches common ladder encodings)
    if (orient == 0) {
        push(fx, fy, fz+t, fx, fy+1, fz+t, fx+1, fy+1, fz+t, fx+1, fy, fz+t);
    } else if (orient == 1) {
        push(fx+1, fy, fz+1-t, fx+1, fy+1, fz+1-t, fx, fy+1, fz+1-t, fx, fy, fz+1-t);
    } else if (orient == 2) {
        push(fx+t, fy, fz+1, fx+t, fy+1, fz+1, fx+t, fy+1, fz, fx+t, fy, fz);
    } else {
        push(fx+1-t, fy, fz, fx+1-t, fy+1, fz, fx+1-t, fy+1, fz+1, fx+1-t, fy, fz+1);
    }
}

// AddBlockFace @ 0x877a0 — geometry by Material class (+0x01).
// Class 0: full cube or slab (height from +0x00 / state bit 4 upside-down).
// Class 5: fence posts + bars.
// Class 6: stairs (state&3 orientation, state&4 upside-down).
// Class 7: door (state bits orientation / open / upper).
// Class 8: small lamp (inset cube).
// Class 9: fence gate.
// Class 10: trapdoor.
void RegionMesher::AddBlockFace(MeshBuffer& out, GridData& grid, int x, int y, int z,
                                const Material& mat, int ourFace, int needFlags,
                                const LightMap* lights) {
    (void)needFlags;
    int idx = grid.CoordToIndex(x, y, z);
    unsigned short st = (idx >= 0 && idx < (int)grid.cellStates.size()) ? grid.cellStates[idx] : 0;
    uint8_t cls = mat.prop01();
    float u0, v0, u1, v1;
    FaceUV(mat, ourFace, u0, v0, u1, v1);
    uint32_t col = FaceColor(lights, x, y, z, ourFace);
    float fx = (float)x, fy = (float)y, fz = (float)z;

    if (cls == 0) {
        // Solid / slab. height units 0..16 in +0x00; state&4 → top slab.
        int ht = (int)mat.height();
        if (ht <= 0 || ht > 16) ht = 16;
        float y0 = fy, y1 = fy + (float)ht / 16.f;
        if (st & 4) { // upside-down slab path from binary
            y1 = fy + 1.f;
            y0 = y1 - (float)ht / 16.f;
        }
        // Full cube fast path
        if (ht >= 16 && !(st & 4)) {
            EmitFace(out, x, y, z, ourFace, u0, v0, u1, v1, col);
            return;
        }
        EmitAxisFace(out, fx, y0, fz, fx+1, y1, fz+1, ourFace, u0, v0, u1, v1, col);
        return;
    }

    if (cls == 5) {
        // Fence: center post always; bars toward solid/fence neighbours on side faces.
        const float p0 = 6.f/16.f, p1 = 10.f/16.f;
        if (ourFace == 2 || ourFace == 3) {
            EmitAxisFace(out, fx+p0, fy, fz+p0, fx+p1, fy+1, fz+p1, ourFace, u0, v0, u1, v1, col);
        } else {
            // post sides only for matching axis
            EmitAxisFace(out, fx+p0, fy, fz+p0, fx+p1, fy+1, fz+p1, ourFace, u0, v0, u1, v1, col);
            // connecting bar if neighbour is solid/fence
            int nx = x, ny = y, nz = z;
            if (ourFace == 0) nx++; else if (ourFace == 1) nx--;
            else if (ourFace == 4) nz++; else if (ourFace == 5) nz--;
            bool neighFence = false;
            {
                int ni = grid.CoordToIndex(nx, ny, nz);
                if (ni >= 0) {
                    const Material* nm = grid.GetMaterial((int)grid.cellTypes[ni]);
                    if (nm && nm->prop01() == 5) neighFence = true;
                }
            }
            if (IsSolid(grid, nx, ny, nz) || neighFence) {
                float yb0 = fy + 6.f/16.f, yb1 = fy + 9.f/16.f;
                if (ourFace == 0)
                    EmitAxisFace(out, fx+p1, yb0, fz+p0, fx+1, yb1, fz+p1, ourFace, u0, v0, u1, v1, col);
                else if (ourFace == 1)
                    EmitAxisFace(out, fx, yb0, fz+p0, fx+p0, yb1, fz+p1, ourFace, u0, v0, u1, v1, col);
                else if (ourFace == 4)
                    EmitAxisFace(out, fx+p0, yb0, fz+p1, fx+p1, yb1, fz+1, ourFace, u0, v0, u1, v1, col);
                else if (ourFace == 5)
                    EmitAxisFace(out, fx+p0, yb0, fz, fx+p1, yb1, fz+p0, ourFace, u0, v0, u1, v1, col);
            }
        }
        return;
    }

    if (cls == 6) {
        // Stairs: state&3 = facing (0=+Z 1=-Z 2=+X 3=-X), state&4 = upside-down.
        int facing = st & 3;
        bool upside = (st & 4) != 0;
        // Emit two half-slabs forming a stair step relative to face.
        // Bottom half always; top half only on the high side.
        float yLow0 = upside ? fy + 0.5f : fy;
        float yLow1 = yLow0 + 0.5f;
        float yHigh0 = upside ? fy : fy + 0.5f;
        float yHigh1 = yHigh0 + 0.5f;
        // Which faces get the high step
        bool highFace = false;
        if (facing == 0 && ourFace == 4) highFace = true;      // +Z high
        if (facing == 1 && ourFace == 5) highFace = true;
        if (facing == 2 && ourFace == 0) highFace = true;
        if (facing == 3 && ourFace == 1) highFace = true;
        // Side faces: both halves with split AABB
        if (ourFace == 2 || ourFace == 3) {
            // top/bottom: full footprint at high/low height
            if (ourFace == 2) {
                EmitAxisFace(out, fx, yHigh1, fz, fx+1, yHigh1, fz+1, ourFace, u0, v0, u1, v1, col);
            } else {
                EmitAxisFace(out, fx, yLow0, fz, fx+1, yLow0, fz+1, ourFace, u0, v0, u1, v1, col);
            }
            return;
        }
        // For horizontal faces: emit low slab full, high slab on high half
        EmitAxisFace(out, fx, yLow0, fz, fx+1, yLow1, fz+1, ourFace, u0, v0, u1, v1, col);
        // high step occupies half the cell toward facing
        float hx0=fx, hx1=fx+1, hz0=fz, hz1=fz+1;
        if (facing == 0) { hz0 = fz + 0.5f; }
        else if (facing == 1) { hz1 = fz + 0.5f; }
        else if (facing == 2) { hx0 = fx + 0.5f; }
        else { hx1 = fx + 0.5f; }
        EmitAxisFace(out, hx0, yHigh0, hz0, hx1, yHigh1, hz1, ourFace, u0, v0, u1, v1, col);
        (void)highFace;
        return;
    }

    if (cls == 7) {
        // Door: thin plane; state&3 facing, bit3 open, bit4 upper half (binary-ish).
        int facing = st & 3;
        bool open = (st & 4) != 0;
        float t = 3.f / 16.f;
        if (open) {
            // swing 90° — approximate as orthogonal plane
            facing = (facing + 1) & 3;
        }
        if (facing == 0 && (ourFace == 4 || ourFace == 5))
            EmitAxisFace(out, fx, fy, fz, fx+1, fy+1, fz+t, ourFace, u0, v0, u1, v1, col);
        else if (facing == 1 && (ourFace == 4 || ourFace == 5))
            EmitAxisFace(out, fx, fy, fz+1-t, fx+1, fy+1, fz+1, ourFace, u0, v0, u1, v1, col);
        else if (facing == 2 && (ourFace == 0 || ourFace == 1))
            EmitAxisFace(out, fx, fy, fz, fx+t, fy+1, fz+1, ourFace, u0, v0, u1, v1, col);
        else if (facing == 3 && (ourFace == 0 || ourFace == 1))
            EmitAxisFace(out, fx+1-t, fy, fz, fx+1, fy+1, fz+1, ourFace, u0, v0, u1, v1, col);
        else if (ourFace == 2 || ourFace == 3) {
            // top/bottom thin strip
            if (facing <= 1)
                EmitAxisFace(out, fx, fy, (facing==0?fz:fz+1-t), fx+1, fy+1, (facing==0?fz+t:fz+1), ourFace, u0, v0, u1, v1, col);
            else
                EmitAxisFace(out, (facing==2?fx:fx+1-t), fy, fz, (facing==2?fx+t:fx+1), fy+1, fz+1, ourFace, u0, v0, u1, v1, col);
        }
        return;
    }

    if (cls == 8) {
        // Small lamp — inset cube 4/16..12/16
        float a = 4.f/16.f, b = 12.f/16.f;
        EmitAxisFace(out, fx+a, fy+a, fz+a, fx+b, fy+b, fz+b, ourFace, u0, v0, u1, v1, col);
        return;
    }

    if (cls == 9) {
        // Fence gate — similar to fence but with gate panel in middle height
        const float p0 = 6.f/16.f, p1 = 10.f/16.f;
        EmitAxisFace(out, fx+p0, fy, fz+p0, fx+p1, fy+1, fz+p1, ourFace, u0, v0, u1, v1, col);
        float yb0 = fy + 5.f/16.f, yb1 = fy + 12.f/16.f;
        int facing = st & 3;
        if ((facing <= 1 && (ourFace == 4 || ourFace == 5)) ||
            (facing >= 2 && (ourFace == 0 || ourFace == 1))) {
            EmitAxisFace(out, fx, yb0, fz, fx+1, yb1, fz+1, ourFace, u0, v0, u1, v1, col);
        }
        return;
    }

    if (cls == 10) {
        // Trapdoor: flat 3/16 slab; state&4 open → vertical against wall.
        int facing = st & 3;
        bool open = (st & 4) != 0;
        if (!open) {
            float y0 = (st & 8) ? fy + 13.f/16.f : fy;
            float y1 = y0 + 3.f/16.f;
            EmitAxisFace(out, fx, y0, fz, fx+1, y1, fz+1, ourFace, u0, v0, u1, v1, col);
        } else {
            float t = 3.f/16.f;
            if (facing == 0)
                EmitAxisFace(out, fx, fy, fz, fx+1, fy+1, fz+t, ourFace, u0, v0, u1, v1, col);
            else if (facing == 1)
                EmitAxisFace(out, fx, fy, fz+1-t, fx+1, fy+1, fz+1, ourFace, u0, v0, u1, v1, col);
            else if (facing == 2)
                EmitAxisFace(out, fx, fy, fz, fx+t, fy+1, fz+1, ourFace, u0, v0, u1, v1, col);
            else
                EmitAxisFace(out, fx+1-t, fy, fz, fx+1, fy+1, fz+1, ourFace, u0, v0, u1, v1, col);
        }
        return;
    }

    // Fallback: full face
    EmitFace(out, x, y, z, ourFace, u0, v0, u1, v1, col);
}

// AddWaterFace — kept from prior recovery (corner heights, dual YP)
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

    switch (face) {
        case 0: {
            float t0 = fy + h10, t1 = fy + h11;
            float vt0 = v0 + (v1 - v0) * (1.f - h10);
            float vt1 = v0 + (v1 - v0) * (1.f - h11);
            set(0, fx+1, fy, fz,   u0, v1); set(1, fx+1, t0, fz,   u0, vt0);
            set(2, fx+1, t1, fz+1, u1, vt1); set(3, fx+1, fy, fz+1, u1, v1);
            push(); break;
        }
        case 1: {
            float t0 = fy + h01, t1 = fy + h00;
            float vt0 = v0 + (v1 - v0) * (1.f - h01);
            float vt1 = v0 + (v1 - v0) * (1.f - h00);
            set(0, fx, fy, fz+1, u0, v1); set(1, fx, t0, fz+1, u0, vt0);
            set(2, fx, t1, fz,   u1, vt1); set(3, fx, fy, fz,   u1, v1);
            push(); break;
        }
        case 2: {
            set(0, fx,   fy+h00, fz,   u0, v1);
            set(1, fx,   fy+h01, fz+1, u0, v0);
            set(2, fx+1, fy+h11, fz+1, u1, v0);
            set(3, fx+1, fy+h10, fz,   u1, v1);
            push();
            // dual-sided
            set(0, fx+1, fy+h10, fz,   u1, v1);
            set(1, fx+1, fy+h11, fz+1, u1, v0);
            set(2, fx,   fy+h01, fz+1, u0, v0);
            set(3, fx,   fy+h00, fz,   u0, v1);
            push();
            break;
        }
        case 3: {
            set(0, fx,   fy, fz+1, u0, v1); set(1, fx,   fy, fz,   u0, v0);
            set(2, fx+1, fy, fz,   u1, v0); set(3, fx+1, fy, fz+1, u1, v1);
            push(); break;
        }
        case 4: {
            float t0 = fy + h01, t1 = fy + h11;
            float vt0 = v0 + (v1 - v0) * (1.f - h01);
            float vt1 = v0 + (v1 - v0) * (1.f - h11);
            set(0, fx+1, fy, fz+1, u0, v1); set(1, fx+1, t1, fz+1, u0, vt1);
            set(2, fx,   t0, fz+1, u1, vt0); set(3, fx,   fy, fz+1, u1, v1);
            push(); break;
        }
        case 5: {
            float t0 = fy + h00, t1 = fy + h10;
            float vt0 = v0 + (v1 - v0) * (1.f - h00);
            float vt1 = v0 + (v1 - v0) * (1.f - h10);
            set(0, fx,   fy, fz, u0, v1); set(1, fx,   t0, fz, u0, vt0);
            set(2, fx+1, t1, fz, u1, vt1); set(3, fx+1, fy, fz, u1, v1);
            push(); break;
        }
        default: break;
    }
}

void RegionMesher::Build(GridData& grid, int ox, int oy, int oz,
                         const std::vector<BlockDef>& defs, MeshBuffer& out,
                         const LightMap* lights) {
    Build(grid, ox, oy, oz, defs, out, nullptr, lights);
}

void RegionMesher::Build(GridData& grid, int ox, int oy, int oz,
                         const std::vector<BlockDef>& defs, MeshBuffer& solidOut,
                         MeshBuffer* waterOut, const LightMap* lights) {
    // Region::AddMesh @ 0x9b090 control flow (verified from Ghidra):
    //   type = types[cell]; if 0 → skip
    //   mat = materials[type]
    //   class = mat[+0x01]
    //   if class not in {0,5,6,7,8,9,0xa}:
    //     1|4 → AddCross; 2 → AddTorch; 3 → AddLadder; done
    //   else:
    //     for each face bit: NeedFace* → AddBlockFace(face, flags)
    //   if state fluid bits: WaterNeedFace* → AddWaterFace
    solidOut.Clear();
    if (waterOut) waterOut->Clear();
    const int S = 16;
    (void)defs;

    for (int z = 0; z < S; ++z)
    for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
        int wx = ox + x, wy = oy + y, wz = oz + z;
        int cidx = grid.CoordToIndex(wx, wy, wz);
        if (cidx < 0) continue;
        unsigned char t = grid.cellTypes[cidx];
        unsigned short st = (cidx < (int)grid.cellStates.size()) ? grid.cellStates[cidx] : 0;
        // Binary: liquid is state-driven (type usually 0). Do not skip type 0 fluids.
        const int liquidType = CellLiquidType(grid, wx, wy, wz); // 0 water, 1 lava, -1 none
        if (t == 0 && liquidType < 0) continue;

        const Material* mat = (t != 0) ? grid.GetMaterial((int)t) : nullptr;
        // Region::AddMesh end: if state&0x2000 → LiquidType 0; else if state&0x3c0 → LiquidType 1
        if (liquidType >= 0) {
            MeshBuffer& dst = waterOut ? *waterOut : solidOut;
            float h = WaterHeightFrac(grid, wx, wy, wz);
            if (h > 0.f) {
                for (int face = 0; face < 6; ++face) {
                    if (WaterNeedFace(grid, wx, wy, wz, face) == 0) continue;
                    float u0=0,v0=0,u1=1.f/16.f,v1=1.f/16.f;
                    if (liquidType == 0) {
                        // Water UVs from material row 5 atlas tiles (binary uses water tex / atlas)
                        const Material* wm = grid.GetMaterial(5);
                        if (wm) FaceUV(*wm, face, u0, v0, u1, v1);
                        else Texture::TileUV(6, 0, u0, v0, u1, v1);
                    } else {
                        // Lava: distinct atlas tile (commonly near still/lava in blocks.png)
                        Texture::TileUV(13, 14, u0, v0, u1, v1);
                    }
                    uint32_t col = (liquidType == 0) ? 0xA0FFFFFF : 0xE0FF6030;
                    if (lights) {
                        uint32_t lc = LightMap::GetLightColor(lights->GetFaceLight(wx, wy, wz, face));
                        col = (lc & 0x00FFFFFFu) | ((liquidType == 0) ? 0xA0000000u : 0xE0000000u);
                    }
                    EmitWaterFace(grid, dst, wx, wy, wz, face, u0, v0, u1, v1, col);
                }
            }
            continue;
        }

        // Special model classes (not in face set {0,5,6,7,8,9,10})
        if (mat) {
            uint8_t cls = mat->prop01();
            if (cls == 1 || cls == 4) {
                AddCross(solidOut, wx, wy, wz, *mat, lights);
                // still allow liquid overlay below
            } else if (cls == 2) {
                AddTorch(solidOut, grid, wx, wy, wz, *mat, lights);
            } else if (cls == 3) {
                AddLadder(solidOut, grid, wx, wy, wz, *mat, lights);
            } else if (cls == 0 || cls == 5 || cls == 6 || cls == 7 ||
                       cls == 8 || cls == 9 || cls == 10) {
                for (int face = 0; face < 6; ++face) {
                    int nf = NeedFace(grid, wx, wy, wz, face);
                    if (nf == 0) continue;
                    AddBlockFace(solidOut, grid, wx, wy, wz, *mat, face, nf, lights);
                }
            } else {
                // unknown class → treat as solid faces
                for (int face = 0; face < 6; ++face) {
                    int nf = NeedFace(grid, wx, wy, wz, face);
                    if (nf == 0) continue;
                    AddBlockFace(solidOut, grid, wx, wy, wz, *mat, face, nf, lights);
                }
            }
        } else {
            for (int face = 0; face < 6; ++face) {
                if (NeedFace(grid, wx, wy, wz, face) == 0) continue;
                float u0=0,v0=0,u1=1.f/16.f,v1=1.f/16.f;
                Texture::TileUV((int)t % 16, (int)t / 16, u0, v0, u1, v1);
                EmitFace(solidOut, wx, wy, wz, face, u0, v0, u1, v1, FaceColor(lights, wx, wy, wz, face));
            }
        }

    }
}

// DrawPrimitive @ 0xcfa30 — GLES client arrays, PCT1Vertex stride 24
#if defined(__ANDROID__)
#include <GLES/gl.h>
void DrawPrimitive(unsigned int mode, const PCT1Vertex* verts, const uint16_t* indices,
                   int indexCount, int) {
    // Binary DrawPrimitive @ 0xcfa30: client arrays only. Texture enable/bind is
    // owned by RenderState / caller (Screen::ApplyState) — do NOT force GL_TEXTURE_2D
    // here or untextured sky/stars sample an incomplete unit and go black.
    if (!verts || !indices || indexCount <= 0) return;
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    const char* base = reinterpret_cast<const char*>(verts);
    glVertexPointer(3, GL_FLOAT, 24, base + 0);
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
