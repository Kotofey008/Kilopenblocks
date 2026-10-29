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

bool RegionMesher::IsSolid(GridData& grid, int x, int y, int z) {
    int idx = grid.CoordToIndex(x, y, z);
    if (idx < 0) return false;
    unsigned char t = grid.cellTypes[idx];
    return t != BlockId::Air && t != BlockId::Water &&
           t != BlockId::Flower1 && t != BlockId::Mushroom1 &&
           t != BlockId::Mushroom2 && t != BlockId::Leaves1 &&
           t != BlockId::Leaves2 && t != 11 && t != 17 && t != 18;
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

// Emit a rectangular quad in face plane spanning [u0..u1) x [v0..v1) in the 2D axes
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

void RegionMesher::Build(GridData& grid, int ox, int oy, int oz,
                         const std::vector<BlockDef>& defs, MeshBuffer& out,
                         const LightMap* lights) {
    out.Clear();
    const int S = 16;
    const float tile = 1.f / 16.f;

    // --- Greedy meshing per face direction ---
    // mask[S][S]: 0 = skip, else pack type in low 8 + light key
    // For each of 6 faces, walk slices and merge runs.

    for (int face = 0; face < 6; ++face) {
        // d = primary axis of the face normal
        // u,v = the two axes in the plane
        int du_axis, dv_axis, dn_axis;
        // axis indices: 0=x 1=y 2=z
        if (face == 0 || face == 1) { dn_axis = 0; du_axis = 2; dv_axis = 1; } // X faces: plane Z,Y
        else if (face == 2 || face == 3) { dn_axis = 1; du_axis = 0; dv_axis = 2; } // Y: X,Z
        else { dn_axis = 2; du_axis = 0; dv_axis = 1; } // Z: X,Y

        for (int slice = 0; slice < S; ++slice) {
            // Build mask for this slice
            int mask[16][16];
            uint32_t colors[16][16];
            float uvs[16][16][4]; // u0,v0,u1,v1
            memset(mask, 0, sizeof(mask));

            for (int b = 0; b < S; ++b) {
                for (int a = 0; a < S; ++a) {
                    int coords[3];
                    coords[dn_axis] = slice;
                    coords[du_axis] = a;
                    coords[dv_axis] = b;
                    int x = ox + coords[0], y = oy + coords[1], z = oz + coords[2];

                    int cidx = grid.CoordToIndex(x, y, z);
                    if (cidx < 0) continue;
                    unsigned char t = grid.cellTypes[cidx];
                    if (t == BlockId::Air) continue;

                    // neighbor in face direction
                    int ncoords[3] = {coords[0], coords[1], coords[2]};
                    if (face == 0 || face == 2 || face == 4) ncoords[dn_axis]++;
                    else ncoords[dn_axis]--;
                    int nx = ox + ncoords[0], ny = oy + ncoords[1], nz = oz + ncoords[2];
                    if (IsSolid(grid, nx, ny, nz)) continue; // occluded

                    mask[b][a] = (int)t + 1;
                    float uu0=0,vv0=0,uu1=tile,vv1=tile;
                    if (t < defs.size()) GetFaceUV(defs[t], face, uu0, vv0, uu1, vv1);
                    uvs[b][a][0]=uu0; uvs[b][a][1]=vv0; uvs[b][a][2]=uu1; uvs[b][a][3]=vv1;
                    uint32_t col = 0xFFFFFFFFu;
                    if (lights) col = LightMap::GetLightColor(lights->GetFaceLight(x,y,z,face));
                    colors[b][a] = col;
                }
            }

            // Greedy merge on mask
            bool used[16][16] = {};
            for (int b = 0; b < S; ++b) {
                for (int a = 0; a < S; ++a) {
                    if (!mask[b][a] || used[b][a]) continue;
                    int type = mask[b][a];
                    uint32_t col = colors[b][a];
                    // width
                    int w = 1;
                    while (a + w < S && mask[b][a+w] == type && colors[b][a+w] == col && !used[b][a+w])
                        w++;
                    // height
                    int h = 1;
                    bool ok = true;
                    while (b + h < S && ok) {
                        for (int k = 0; k < w; ++k) {
                            if (mask[b+h][a+k] != type || colors[b+h][a+k] != col || used[b+h][a+k]) {
                                ok = false; break;
                            }
                        }
                        if (ok) h++;
                    }
                    for (int j = 0; j < h; ++j)
                        for (int k = 0; k < w; ++k)
                            used[b+j][a+k] = true;

                    // world origin of this quad
                    int coords[3];
                    coords[dn_axis] = slice;
                    coords[du_axis] = a;
                    coords[dv_axis] = b;
                    int x = ox + coords[0], y = oy + coords[1], z = oz + coords[2];

                    // map w,h to the correct axes for EmitGreedyQuad
                    // face 0/1: du=z(w), dv=y(h)
                    // face 2/3: du=x(w), dv=z(h)
                    // face 4/5: du=x(w), dv=y(h)
                    EmitGreedyQuad(out, face, x, y, z, w, h,
                                   uvs[b][a][0], uvs[b][a][1], uvs[b][a][2], uvs[b][a][3], col);
                }
            }
        }
    }
}
