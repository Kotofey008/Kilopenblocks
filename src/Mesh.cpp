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
    // NeedFace* @ 0x833d0 family — per-class jump in binary.
    //
    // Adjacent-face cull (drop shared face between two cells) runs ONLY when
    // BOTH materials satisfy (after LoadMaterials gating):
    //   BMT (+0x01) == 0
    //   height (+0x00) == 0x10  // file BH 0 or 16 → full cell; BH 1..15 partial
    //   MBI  (+0x02) == 0       // fully opaque
    // Any other neighbour (slab, glass MBI=1, ice MBI=2, leaves, BMT≠0, air, …)
    // does NOT suppress the face.
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
    const Material* selfMat = grid.GetMaterial((int)selfType);

    auto isMutualOccluder = [](unsigned char t, const Material* mat) -> bool {
        if (t == 0 || !mat) return false;
        if (mat->prop01() != 0) return false;     // BMT
        if (mat->height() != 0x10) return false;  // full BH only
        if (mat->prop02() != 0) return false;     // MBI
        return true;
    };

    auto isPartialHeight = [](const Material* mat) -> bool {
        if (!mat || mat->prop01() != 0) return false;
        uint8_t ht = mat->height();
        return ht != 0 && ht < 0x10;
    };

    if (nIdx < 0) {
        if (isPartialHeight(selfMat)) return 2;
        return 1;
    }

    unsigned char nType = grid.cellTypes[nIdx];
    const Material* nMat = grid.GetMaterial((int)nType);

    // Only full opaque solid touching full opaque solid → cull shared face.
    if (isMutualOccluder(selfType, selfMat) && isMutualOccluder(nType, nMat))
        return 0;

    // MBI=1 same-type adjacent face cull (glass / leaves + All/LeavesQuality):
    // Binary: MBI==1 + same type → drop shared face, except All(+0x07)!=0 with
    // LeavesQuality Fancy (2): keep internal faces.
    //   Off (0): cull same-id (and mesh layer forced to 0 in AddBlockFace/Build)
    //   Fast (1): cull same-id (cutout layer, like glass)
    //   Fancy (2): keep same-id faces for All=1
    if (nType == selfType && selfMat && selfMat->prop02() == 1) {
        if (selfMat->allFaces()) {
            // All=1 leaves: only Fancy keeps inter-block faces
            if (grid.GetLeavesQuality() != GridData::LeavesQuality::Fancy)
                return 0;
        } else {
            return 0; // MBI=1 All=0 (e.g. glass): always cull same-id face
        }
    }

    if (isPartialHeight(selfMat))
        return 2;

    if (selfMat) {
        uint8_t cls = selfMat->prop01();
        if (cls == 6 || cls == 7 || cls == 10) return 2;
    }
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
    if (t == 11) return false;
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
    // Delegate to GetWaterBlockHeight (units 0..0x80); 0x80 → full cell
    int units = grid.GetWaterBlockHeight(x, y, z, 0, 0);
    if (units <= 0) return 0.f;
    if (units >= 0x80) return 1.f;
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
    // WaterNeedFace* family: LiquidType-aware cull with mutual IsFlowAllowed
    int lt = CellLiquidType(grid, x, y, z);
    if (lt < 0) return 0;
    int nx = x, ny = y, nz = z;
    GridData::FlowType outF = GridData::FlowXP, inF = GridData::FlowXN;
    switch (face) {
        case 0: nx++; outF = GridData::FlowXP; inF = GridData::FlowXN; break;
        case 1: nx--; outF = GridData::FlowXN; inF = GridData::FlowXP; break;
        case 2: ny++; outF = GridData::FlowYP; inF = GridData::FlowYN; break;
        case 3: ny--; outF = GridData::FlowYN; inF = GridData::FlowYP; break;
        case 4: nz++; outF = GridData::FlowZP; inF = GridData::FlowZN; break;
        case 5: nz--; outF = GridData::FlowZN; inF = GridData::FlowZP; break;
        default: return 0;
    }
    int self = grid.CoordToIndex(x, y, z);
    int ni = grid.CoordToIndex(nx, ny, nz);
    if (ni < 0) return 1;
    int nlt = CellLiquidType(grid, nx, ny, nz);
    if (nlt == lt) {
        // Binary WaterNeedFace*: cull only when mutual IsFlowAllowed both ways
        if (self >= 0 &&
            grid.IsFlowAllowed(self, outF) &&
            grid.IsFlowAllowed(ni, inF))
            return 0;
        // Same liquid but flow blocked in a direction → still no internal face
        // when both cells are full liquid columns (height continuity is meshed
        // via corner heights on YP, not side faces between equal liquids).
        return 0;
    }
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

// UVR face remap table from libExplorationLite.so @ 0x10a1a0 (24 dwords).
// Index = binaryFace + facing*6; values are material face indices in binary
// order: 0=XP 1=YP 2=ZP 3=XN 4=YN 5=ZN. Used when (UVR & 1) != 0.
// AddBlockFace @ ghidra: iVar18 = table[param_5 + (state&3)*6].
static const int kUvrFaceTable[4][6] = {
    { 0, 1, 2, 3, 4, 5 }, // facing 0
    { 5, 1, 0, 2, 4, 3 }, // facing 1
    { 3, 1, 5, 0, 4, 2 }, // facing 2
    { 2, 1, 3, 5, 4, 0 }, // facing 3
};

// ConfigureState stores state&3 = argmin{lx,lz,-lx,-lz}. World face toward the
// player is opposite look; table facing 0 keeps mat ZP on world ZP, so player
// looking +Z would see ZN. Index the table with (state+2)&3 so front faces the
// player (validated for UVR=1).
static int UvrTableFacing(int stateFacing, uint8_t uvr) {
    int f = stateFacing & 3;
    // UVR bit2 (value 4): binary remaps facing 1→3, 2→0 before table index
    if (uvr & 4) {
        if (f == 1) f = 3;
        else if (f == 2) f = 0;
    }
    return (f + 2) & 3;
}

void RegionMesher::FaceUV(const Material& mat, int ourFace, float& u0, float& v0, float& u1, float& v1,
                          unsigned short orientState) {
    uint8_t uvr = mat.uvr();
    int binFace = OurToBinFace(ourFace); // 0=XP .. 5=ZN
    int matBin = binFace;
    if (uvr != 0) {
        if (uvr & 1) {
            int facing = UvrTableFacing((int)(orientState & 3), uvr);
            matBin = kUvrFaceTable[facing][binFace];
            // UVR bit1 (value 2, set in UVR=3): after table, ZP/ZN material indices
            // are opposite the player-facing convention used for UVR=1 sides.
            // Binary table @ 0x10a1a0 maps world face→mat face; with (state+2)&3
            // X sides match, but ZP(2)/ZN(5) read inverted for bit1 blocks (bed).
            // Swap mat ZP↔ZN when bit1 set so near face gets the intended front tile.
            if ((uvr & 2) != 0) {
                if (matBin == 2) matBin = 5;
                else if (matBin == 5) matBin = 2;
            }
        }
    }
    Texture::TileUV((int)mat.uFace(matBin), (int)mat.vFace(matBin), u0, v0, u1, v1);
}

uint32_t RegionMesher::FaceColor(const LightMap* lights, int x, int y, int z, int face) {
    if (!lights) return 0xFFFFFFFFu;
    uint32_t col = lights->GetLightColor(lights->GetFaceLight(x, y, z, face));
    if ((col & 0xFFu) < 32 && ((col >> 8) & 0xFFu) < 32)
        col = 0xFFC0C0C0u;
    return col;
}

void RegionMesher::EmitFace(MeshBuffer& out, int x, int y, int z, int face,
                            float u0, float v0, float u1, float v1, uint32_t color,
                            int uvRot) {
    EmitAxisFace(out, (float)x, (float)y, (float)z,
                 (float)x + 1.f, (float)y + 1.f, (float)z + 1.f,
                 face, u0, v0, u1, v1, color, uvRot);
}

// Emit one face of an AABB [x0,x1]×[y0,y1]×[z0,z1] (world cell units).
void RegionMesher::EmitAxisFace(MeshBuffer& out, float x0, float y0, float z0,
                                float x1, float y1, float z1, int ourFace,
                                float u0, float v0, float u1, float v1, uint32_t color,
                                int uvRot) {
    PCT1Vertex v[4];
    auto set = [&](int i, float px, float py, float pz, float u, float vv) {
        v[i].x = px; v[i].y = py; v[i].z = pz;
        v[i].color = color; v[i].u = u; v[i].v = vv;
    };
    // Default UV corners in Emit order.
    float cu[4] = { u0, u0, u1, u1 };
    float cv[4] = { v1, v0, v0, v1 };
    // UVR&2 Y-face path: uvRot bit2 set marks binary local_28 layouts; low 2 bits = facing.
    if ((uvRot & 4) != 0 && (ourFace == 2 || ourFace == 3)) {
        static const int kYuv[4][8] = {
            { 0, 0,  0,16,  16,16,  16, 0 },
            { 0,16, 16,16,  16, 0,   0, 0 },
            {16,16, 16, 0,   0, 0,   0,16 },
            {16, 0,  0, 0,   0,16,  16,16 },
        };
        int f = uvRot & 3;
        for (int i = 0; i < 4; ++i) {
            int bu = kYuv[f][i * 2];
            int bv = kYuv[f][i * 2 + 1];
            cu[i] = (bu == 0) ? u0 : u1;
            cv[i] = (bv == 0) ? v0 : v1;
        }
    }
    switch (ourFace) {
        case 0: // +X at x1
            set(0, x1, y0, z0, cu[0], cv[0]); set(1, x1, y1, z0, cu[1], cv[1]);
            set(2, x1, y1, z1, cu[2], cv[2]); set(3, x1, y0, z1, cu[3], cv[3]); break;
        case 1: // -X at x0
            set(0, x0, y0, z1, cu[0], cv[0]); set(1, x0, y1, z1, cu[1], cv[1]);
            set(2, x0, y1, z0, cu[2], cv[2]); set(3, x0, y0, z0, cu[3], cv[3]); break;
        case 2: // +Y at y1
            set(0, x0, y1, z0, cu[0], cv[0]); set(1, x0, y1, z1, cu[1], cv[1]);
            set(2, x1, y1, z1, cu[2], cv[2]); set(3, x1, y1, z0, cu[3], cv[3]); break;
        case 3: // -Y at y0
            set(0, x0, y0, z1, cu[0], cv[0]); set(1, x0, y0, z0, cu[1], cv[1]);
            set(2, x1, y0, z0, cu[2], cv[2]); set(3, x1, y0, z1, cu[3], cv[3]); break;
        case 4: // +Z at z1
            set(0, x1, y0, z1, cu[0], cv[0]); set(1, x1, y1, z1, cu[1], cv[1]);
            set(2, x0, y1, z1, cu[2], cv[2]); set(3, x0, y0, z1, cu[3], cv[3]); break;
        case 5: // -Z at z0
            set(0, x0, y0, z0, cu[0], cv[0]); set(1, x0, y1, z0, cu[1], cv[1]);
            set(2, x1, y1, z0, cu[2], cv[2]); set(3, x1, y0, z0, cu[3], cv[3]); break;
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
    FaceUV(mat, ourFace, u0, v0, u1, v1, st);
    uint32_t col = FaceColor(lights, x, y, z, ourFace);
    float fx = (float)x, fy = (float)y, fz = (float)z;
    // UVR bit1 (value 2): special Y-face UV corner layout (AddBlockFace local_28).
    // UVR=3 sets bits 0+1. Index with same facing as kUvrFaceTable.
    int uvRot = 0;
    if ((mat.uvr() & 2) != 0 && (ourFace == 2 || ourFace == 3))
        uvRot = UvrTableFacing((int)st & 3, mat.uvr()) | 4;

    if (cls == 0) {
        // Solid / slab from Material+0x00 height (1..16 after LoadMaterials BH gate).
        // state&4 → upside-down (slab flush to cell ceiling) — same bit used in
        // NeedFaceYP/YN boundary paths and ConfigureState orientation.
        int ht = (int)mat.height();
        if (ht <= 0 || ht > 16) ht = 16;
        const bool upside = (st & 4) != 0;
        float y0 = fy;
        float y1 = fy + (float)ht / 16.f;
        if (upside) {
            y1 = fy + 1.f;
            y0 = y1 - (float)ht / 16.f;
        }
        // Full cube: standard face
        if (ht >= 16 && !upside) {
            EmitFace(out, x, y, z, ourFace, u0, v0, u1, v1, col, uvRot);
            return;
        }
        // Partial height: emit only the occupied Y-range of the cell.
        // Side faces (XP/XN/ZP/ZN): scale V so the texture maps to the slab
        // height (bottom-aligned or top-aligned with upside-down).
        float su0 = u0, sv0 = v0, su1 = u1, sv1 = v1;
        if (ourFace == 0 || ourFace == 1 || ourFace == 4 || ourFace == 5) {
            const float frac = (float)ht / 16.f;
            if (!upside) {
                // Bottom slab: keep v1 (bottom of tile), pull v0 down toward v1
                sv0 = v1 + (v0 - v1) * frac;
            } else {
                // Top slab: keep v0 (top of tile), pull v1 up toward v0
                sv1 = v0 + (v1 - v0) * frac;
            }
        }
        EmitAxisFace(out, fx, y0, fz, fx + 1.f, y1, fz + 1.f,
                     ourFace, su0, sv0, su1, sv1, col, uvRot);
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
                         const std::vector<BlockDef>& defs, MeshBuffer layers[7],
                         const LightMap* lights) {
    // Binary: MeshBuffer 7 layers; PushQuads(this, MBI, ...) → layer[MBI].
    // LeavesQuality (grid+0xec)==0 forces leaves types to layer 0.
    // Does NOT change NeedFace / AddBlockFace / BH geometry — only destination buffer.
    for (int i = 0; i < 7; ++i) layers[i].Clear();
    const int S = 16;
    (void)defs;
    const int leavesQ = (int)grid.GetLeavesQuality(); // GridData+0xec

    for (int z = 0; z < S; ++z)
    for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
        int wx = ox + x, wy = oy + y, wz = oz + z;
        int cidx = grid.CoordToIndex(wx, wy, wz);
        if (cidx < 0) continue;
        unsigned char t = grid.cellTypes[cidx];
        unsigned short st = (cidx < (int)grid.cellStates.size()) ? grid.cellStates[cidx] : 0;
        const int liquidType = CellLiquidType(grid, wx, wy, wz);
        if (t == 0 && liquidType < 0) continue;

        // World liquids → layers 3/4 (water) or 5/6 (lava); still vs flow by level
        if (liquidType >= 0) {
            int level = (int)((st & 0x1c00) >> 10);
            int layer = (liquidType == 0)
                ? ((level <= 1) ? 3 : 4)
                : ((level <= 1) ? 5 : 6);
            MeshBuffer& dst = layers[layer];
            float h = WaterHeightFrac(grid, wx, wy, wz);
            if (h > 0.f) {
                for (int face = 0; face < 6; ++face) {
                    if (WaterNeedFace(grid, wx, wy, wz, face) == 0) continue;
                    float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
                    uint32_t col = (liquidType == 0) ? 0xA0FFFFFF : 0xE0FF6030;
                    if (lights) {
                        uint32_t lc = lights->GetLightColor(lights->GetFaceLight(wx, wy, wz, face));
                        col = (lc & 0x00FFFFFFu) | ((liquidType == 0) ? 0xA0000000u : 0xE0000000u);
                    }
                    EmitWaterFace(grid, dst, wx, wy, wz, face, u0, v0, u1, v1, col);
                }
            }
            continue;
        }

        const Material* mat = grid.GetMaterial((int)t);
        if (!mat) continue;

        int mbi = (int)mat->prop02();
        if (mbi < 0) mbi = 0;
        if (mbi > 6) mbi = 6;
        // Binary AddBlockFace: when LeavesQuality(+0xec)==0 (Off), force layer 0
        // for leaf types 10 / 0x14 / 0x55 (All=1 MBI=1 in stock blocks.txt).
        if (leavesQ == (int)GridData::LeavesQuality::Off &&
            (t == 10 || t == 0x14 || t == 0x55))
            mbi = 0;

        MeshBuffer& dst = layers[mbi];
        // MBI 3–6: still solid block geometry (AddBlockFace / models). No liquid
        // meshing (WaterNeedFace / height field). Draw binds water/lava texture
        // for those layers — texture override only (binary layer routing).

        uint8_t cls = mat->prop01();
        if (cls == 1 || cls == 4) {
            AddCross(dst, wx, wy, wz, *mat, lights);
        } else if (cls == 2) {
            AddTorch(dst, grid, wx, wy, wz, *mat, lights);
        } else if (cls == 3) {
            AddLadder(dst, grid, wx, wy, wz, *mat, lights);
        } else if (cls == 0 || cls == 5 || cls == 6 || cls == 7 ||
                   cls == 8 || cls == 9 || cls == 10) {
            for (int face = 0; face < 6; ++face) {
                int nf = NeedFace(grid, wx, wy, wz, face);
                if (nf == 0) continue;
                AddBlockFace(dst, grid, wx, wy, wz, *mat, face, nf, lights);
            }
        } else {
            for (int face = 0; face < 6; ++face) {
                int nf = NeedFace(grid, wx, wy, wz, face);
                if (nf == 0) continue;
                AddBlockFace(dst, grid, wx, wy, wz, *mat, face, nf, lights);
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

// DrawCursor @ 0x59a70
// GetCellSelectionBox → expand by eps=0.01 → line-list AABB → DrawPrimitive(GL_LINES).
// face 0..5 reserved for binary face-emphasis jumptable (optional); full box always drawn.
void DrawCursor(GridData& grid, int cellIndex, int /*unusedHitFlags*/, int /*face*/, uint32_t color) {
    if (cellIndex < 0) return;
    Vector mn, mx;
    grid.GetCellSelectionBox(cellIndex, mn, mx);
    const float eps = 0.01f; // binary float @ 0x107e30
    mn.x -= eps; mn.y -= eps; mn.z -= eps;
    mx.x += eps; mx.y += eps; mx.z += eps;

    // 8 corners
    PCT1Vertex v[8];
    auto set = [&](int i, float x, float y, float z) {
        v[i].x = x; v[i].y = y; v[i].z = z;
        v[i].color = color;
        v[i].u = 0.f; v[i].v = 0.f;
    };
    set(0, mn.x, mn.y, mn.z);
    set(1, mx.x, mn.y, mn.z);
    set(2, mx.x, mx.y, mn.z);
    set(3, mn.x, mx.y, mn.z);
    set(4, mn.x, mn.y, mx.z);
    set(5, mx.x, mn.y, mx.z);
    set(6, mx.x, mx.y, mx.z);
    set(7, mn.x, mx.y, mx.z);

    // 12 edges as line list (binary AddBBox topology)
    static const uint16_t idx[24] = {
        0,1, 1,2, 2,3, 3,0, // bottom
        4,5, 5,6, 6,7, 7,4, // top
        0,4, 1,5, 2,6, 3,7  // verticals
    };
#if defined(__ANDROID__)
#ifndef GL_LINES
#define GL_LINES 0x0001
#endif
    // Binary: disable texture for untextured cursor lines (RenderState path)
    glDisable(GL_TEXTURE_2D);
    DrawPrimitive(GL_LINES, v, idx, 24, 0);
    glEnable(GL_TEXTURE_2D);
#else
    (void)v; (void)idx;
#endif
}

