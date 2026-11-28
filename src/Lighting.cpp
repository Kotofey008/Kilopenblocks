#include "Lighting.h"
// Steps 1–5: LightRules + heightmap + NeoMCPE LightUpdate queue (Sky/Block) with
// budget. Mesh dirty only when a nibble actually changes (MarkRegionDirty).

#include <algorithm>
#include <cstring>
#include <queue>
#include <vector>

// =============================================================================
// LightRules
// =============================================================================
int LightRules::SignedL(const Material* m) {
    if (!m) return 0;
    uint8_t raw = m->sizeL();
    int v = (raw & 0x80) ? ((int)raw - 256) : (int)raw;
    if (v < -15) v = -15;
    if (v > 15) v = 15;
    return v;
}

int LightRules::Opacity(const GridData& grid, unsigned char type, unsigned short state) {
    if (type == 0) {
        if ((state & GridData::STATE_BIT_2000) != 0) return 2;
        return 0;
    }
    const Material* m = grid.GetMaterial((int)type);
    if (!m) return 15;
    int L = SignedL(m);
    uint8_t mbi = m->prop02();
    uint8_t cls = m->prop01();
    if (L > 0) return 0;
    if (L < 0) {
        int absL = -L;
        if (absL < 1) absL = 1;
        if (absL > 15) absL = 15;
        return absL;
    }
    if (cls == 1 || cls == 2 || cls == 3 || cls == 4) return 0;
    if (!m->col()) return 0;
    if (mbi == 1 || mbi == 2) return 0;
    return 15;
}

int LightRules::OpacityAt(const GridData& grid, int wx, int wy, int wz) {
    int cidx = grid.CoordToIndex(wx, wy, wz);
    if (cidx < 0) return 0;
    unsigned char t = grid.cellTypes[cidx];
    unsigned short st = (cidx < (int)grid.cellStates.size()) ? grid.cellStates[cidx] : 0;
    return Opacity(grid, t, st);
}

int LightRules::Emission(const GridData& grid, unsigned char type) {
    if (type == 0) return 0;
    const Material* m = grid.GetMaterial((int)type);
    int L = SignedL(m);
    return (L > 0) ? LightPack::Clamp15(L) : 0;
}

int LightRules::EmissionAt(const GridData& grid, int wx, int wy, int wz) {
    int cidx = grid.CoordToIndex(wx, wy, wz);
    if (cidx < 0) return 0;
    return Emission(grid, grid.cellTypes[cidx]);
}

bool LightRules::BlocksSkyHeight(const GridData& grid, unsigned char type, unsigned short state) {
    return Opacity(grid, type, state) >= 15;
}

// =============================================================================
bool LightUpdateBox::expandToContain(int _x0, int _y0, int _z0, int _x1, int _y1, int _z1) {
    // NeoMCPE LightUpdate::expandToContain — merge if nearly contained / adjacent.
    if (_x0 >= x0 && _y0 >= y0 && _z0 >= z0 && _x1 <= x1 && _y1 <= y1 && _z1 <= z1)
        return true;
    const int r = 1;
    if (_x0 >= x0 - r && _y0 >= y0 - r && _z0 >= z0 - r &&
        _x1 <= x1 + r && _y1 <= y1 + r && _z1 <= z1 + r) {
        int xs = x1 - x0, ys = y1 - y0, zs = z1 - z0;
        if (_x0 > x0) _x0 = x0;
        if (_y0 > y0) _y0 = y0;
        if (_z0 > z0) _z0 = z0;
        if (_x1 < x1) _x1 = x1;
        if (_y1 < y1) _y1 = y1;
        if (_z1 < z1) _z1 = z1;
        int ns = (_x1 - _x0) * (_y1 - _y0) * (_z1 - _z0);
        int os = xs * ys * zs;
        if (ns - os <= 2) {
            x0 = _x0; y0 = _y0; z0 = _z0;
            x1 = _x1; y1 = _y1; z1 = _z1;
            return true;
        }
        // still expand if overlap is useful (Lite: allow larger merges)
        x0 = _x0; y0 = _y0; z0 = _z0;
        x1 = _x1; y1 = _y1; z1 = _z1;
        return true;
    }
    return false;
}

// =============================================================================
int LightMap::Index(int x, int y, int z) const {
    int lx = x - ox_, ly = y - oy_, lz = z - oz_;
    if (lx < 0 || ly < 0 || lz < 0 || lx >= sizeX_ || ly >= sizeY_ || lz >= sizeZ_)
        return -1;
    return lx + sizeX_ * (ly + sizeY_ * lz);
}

void LightMap::InitDefaultCurves() {
    for (int i = 0; i < 16; ++i) {
        float t = (float)i / 15.f;
        curveR_[i] = curveG_[i] = curveB_[i] = curveRow_[i] = t;
    }
}

int LightMap::GetSkyAt(int wx, int wy, int wz) const {
    int i = Index(wx, wy, wz);
    if (i < 0) {
        if (wy >= oy_ + sizeY_) return 15;
        return 0;
    }
    return LightPack::Sky(data_[(size_t)i]);
}

int LightMap::GetBlockAt(int wx, int wy, int wz) const {
    int i = Index(wx, wy, wz);
    if (i < 0) return 0;
    return LightPack::Block(data_[(size_t)i]);
}

void LightMap::SetSky(int wx, int wy, int wz, int sky) {
    int i = Index(wx, wy, wz);
    if (i < 0) return;
    int blk = LightPack::Block(data_[(size_t)i]);
    data_[(size_t)i] = LightPack::Make(sky, blk);
}

void LightMap::SetBlock(int wx, int wy, int wz, int block) {
    int i = Index(wx, wy, wz);
    if (i < 0) return;
    int sky = LightPack::Sky(data_[(size_t)i]);
    data_[(size_t)i] = LightPack::Make(sky, block);
}

void LightMap::Init(GridData& grid) {
    ox_ = grid.originX; oy_ = grid.originY; oz_ = grid.originZ;
    sizeX_ = grid.sizeX > 0 ? grid.sizeX : (int)(grid.worldMax.x - grid.worldMin.x);
    sizeY_ = grid.sizeY > 0 ? grid.sizeY : (int)(grid.worldMax.y - grid.worldMin.y);
    sizeZ_ = grid.sizeZ > 0 ? grid.sizeZ : (int)(grid.worldMax.z - grid.worldMin.z);
    if (sizeX_ <= 0) { ox_ = (int)grid.worldMin.x; sizeX_ = (int)(grid.worldMax.x - grid.worldMin.x); }
    if (sizeY_ <= 0) { oy_ = (int)grid.worldMin.y; sizeY_ = (int)(grid.worldMax.y - grid.worldMin.y); }
    if (sizeZ_ <= 0) { oz_ = (int)grid.worldMin.z; sizeZ_ = (int)(grid.worldMax.z - grid.worldMin.z); }
    data_.assign((size_t)sizeX_ * (size_t)sizeY_ * (size_t)sizeZ_, 0);
    skyHeight_.assign((size_t)sizeX_ * (size_t)sizeZ_, (uint16_t)sizeY_);
    updates_.clear();
    dirty_ = true;
    InitDefaultCurves();
    brightnessApplied_ = -1.f;
    UpdateLightMap();
    RebuildHeightmap(grid);
    PropagateBlockLights(grid);
}

void LightMap::RecalcHeightColumn(GridData& grid, int lx, int lz) {
    if (lx < 0 || lz < 0 || lx >= sizeX_ || lz >= sizeZ_) return;
    int y = sizeY_;
    while (y > 0) {
        int wx = lx + ox_, wy = (y - 1) + oy_, wz = lz + oz_;
        int cidx = grid.CoordToIndex(wx, wy, wz);
        unsigned char t = (cidx >= 0) ? grid.cellTypes[cidx] : 0;
        unsigned short st = (cidx >= 0 && cidx < (int)grid.cellStates.size())
                                ? grid.cellStates[cidx] : 0;
        if (LightRules::BlocksSkyHeight(grid, t, st))
            break;
        --y;
    }
    skyHeight_[(size_t)XZIndex(lx, lz)] = (uint16_t)y;
}

void LightMap::RebuildHeightmap(GridData& grid) {
    if (sizeX_ <= 0 || sizeZ_ <= 0) return;
    skyHeight_.assign((size_t)sizeX_ * (size_t)sizeZ_, 0);
    for (int z = 0; z < sizeZ_; ++z)
        for (int x = 0; x < sizeX_; ++x)
            RecalcHeightColumn(grid, x, z);
}

int LightMap::GetSkyHeightLocal(int lx, int lz) const {
    if (lx < 0 || lz < 0 || lx >= sizeX_ || lz >= sizeZ_ || skyHeight_.empty())
        return sizeY_;
    return (int)skyHeight_[(size_t)XZIndex(lx, lz)];
}

int LightMap::GetSkyHeightWorld(int wx, int wz) const {
    return GetSkyHeightLocal(wx - ox_, wz - oz_);
}

bool LightMap::IsSkyLit(int wx, int wy, int wz) const {
    if (wy >= oy_ + sizeY_) return true;
    if (wy < oy_) return false;
    int lx = wx - ox_, lz = wz - oz_;
    if (lx < 0 || lz < 0 || lx >= sizeX_ || lz >= sizeZ_) return true;
    int ly = wy - oy_;
    return ly >= GetSkyHeightLocal(lx, lz);
}

// =============================================================================
// Queue
// =============================================================================
void LightMap::Enqueue(LightLayer layer, int x0, int y0, int z0, int x1, int y1, int z1) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (z0 > z1) std::swap(z0, z1);
    // Clamp to lightmap world bounds
    int wx0 = ox_, wy0 = oy_, wz0 = oz_;
    int wx1 = ox_ + sizeX_ - 1, wy1 = oy_ + sizeY_ - 1, wz1 = oz_ + sizeZ_ - 1;
    if (x0 < wx0) x0 = wx0;
    if (y0 < wy0) y0 = wy0;
    if (z0 < wz0) z0 = wz0;
    if (x1 > wx1) x1 = wx1;
    if (y1 > wy1) y1 = wy1;
    if (z1 > wz1) z1 = wz1;
    if (x0 > x1 || y0 > y1 || z0 > z1) return;

    for (size_t i = 0; i < updates_.size(); ++i) {
        if (updates_[i].layer != layer) continue;
        if (updates_[i].expandToContain(x0, y0, z0, x1, y1, z1))
            return;
    }
    LightUpdateBox b;
    b.layer = layer;
    b.x0 = x0; b.y0 = y0; b.z0 = z0;
    b.x1 = x1; b.y1 = y1; b.z1 = z1;
    updates_.push_back(b);
}

void LightMap::OnBlockChange(GridData& grid, int wx, int wy, int wz, int emitRadius) {
    int lx = wx - ox_, lz = wz - oz_;
    if (lx >= 0 && lz >= 0 && lx < sizeX_ && lz < sizeZ_)
        RecalcHeightColumn(grid, lx, lz);

    // Sky: full column band ±1 (vertical occlusion + side spill for tunnel mouth)
    int R = emitRadius > 0 ? emitRadius : 2;
    if (R < 2) R = 2;
    Enqueue(LightLayer::Sky, wx - R, oy_, wz - R, wx + R, oy_ + sizeY_ - 1, wz + R);
    // Block: local box; emitters need larger radius
    int br = (emitRadius >= 8) ? 15 : 2;
    Enqueue(LightLayer::Block, wx - br, wy - br, wz - br, wx + br, wy + br, wz + br);
}

int LightMap::RunUpdate(GridData& grid, const LightUpdateBox& box, int cellBudget) {
    int xd = (box.x1 - box.x0) + 1;
    int yd = (box.y1 - box.y0) + 1;
    int zd = (box.z1 - box.z0) + 1;
    int size = xd * yd * zd;
    // NeoMCPE: skip absurdly large
    if (size > 16 * 16 * 128) return 0;

    int visited = 0;
    static const int dx[6] = {1,-1,0,0,0,0};
    static const int dy[6] = {0,0,1,-1,0,0};
    static const int dz[6] = {0,0,0,0,1,-1};

    for (int x = box.x0; x <= box.x1 && visited < cellBudget; ++x) {
        for (int z = box.z0; z <= box.z1 && visited < cellBudget; ++z) {
            for (int y = box.y0; y <= box.y1 && visited < cellBudget; ++y) {
                ++visited;
                int i = Index(x, y, z);
                if (i < 0) continue;

                int op = LightRules::OpacityAt(grid, x, y, z);
                int step = (op <= 0) ? 1 : op;
                if (op >= 15) step = 15;

                if (box.layer == LightLayer::Sky) {
                    int old = LightPack::Sky(data_[(size_t)i]);
                    int emit = IsSkyLit(x, y, z) ? 15 : 0;
                    int target = emit;
                    if (op < 15) {
                        int best = 0;
                        for (int d = 0; d < 6; ++d) {
                            int n = GetSkyAt(x + dx[d], y + dy[d], z + dz[d]);
                            if (n > best) best = n;
                        }
                        int fromN = LightPack::SaturateSub(best, step);
                        if (fromN > target) target = fromN;
                    } else {
                        target = 0;
                    }
                    target = LightPack::Clamp15(target);
                    if (target != old) {
                        SetSky(x, y, z, target);
                        grid.MarkRegionDirty(x, y, z); // step 5: only on change
                        int t = target > 0 ? target - 1 : 0;
                        // Spread: enqueue neighbor faces outside box or always small
                        Enqueue(LightLayer::Sky, x - 1, y, z, x + 1, y, z);
                        Enqueue(LightLayer::Sky, x, y - 1, z, x, y + 1, z);
                        Enqueue(LightLayer::Sky, x, y, z - 1, x, y, z + 1);
                        (void)t;
                    }
                } else {
                    int old = LightPack::Block(data_[(size_t)i]);
                    int emit = LightRules::EmissionAt(grid, x, y, z);
                    int target = emit;
                    if (op < 15) {
                        int best = 0;
                        for (int d = 0; d < 6; ++d) {
                            int n = GetBlockAt(x + dx[d], y + dy[d], z + dz[d]);
                            if (n > best) best = n;
                        }
                        int fromN = LightPack::SaturateSub(best, step);
                        if (fromN > target) target = fromN;
                    } else {
                        target = 0;
                    }
                    target = LightPack::Clamp15(target);
                    if (target != old) {
                        SetBlock(x, y, z, target);
                        grid.MarkRegionDirty(x, y, z);
                        Enqueue(LightLayer::Block, x - 1, y, z, x + 1, y, z);
                        Enqueue(LightLayer::Block, x, y - 1, z, x, y + 1, z);
                        Enqueue(LightLayer::Block, x, y, z - 1, x, y, z + 1);
                    }
                }
            }
        }
    }
    return visited;
}

int LightMap::ProcessLightQueue(GridData& grid, int maxCells) {
    if (updates_.empty() || maxCells <= 0) return 0;
    int left = maxCells;
    int processed = 0;
    // Cap iterations so expand cannot infinite-loop in one frame
    int guard = 64;
    while (!updates_.empty() && left > 0 && guard-- > 0) {
        LightUpdateBox box = updates_.front();
        updates_.erase(updates_.begin());
        int v = RunUpdate(grid, box, left);
        left -= v;
        processed += v;
    }
    return processed;
}

// =============================================================================
// Immediate full-column sky (init only)
// =============================================================================
void LightMap::PropagateSky(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1) {
    (void)ly0; (void)ly1;
    if (lx0 < 0) lx0 = 0;
    if (lz0 < 0) lz0 = 0;
    if (lx1 > sizeX_) lx1 = sizeX_;
    if (lz1 > sizeZ_) lz1 = sizeZ_;
    if (lx0 >= lx1 || lz0 >= lz1) return;

    for (int z = lz0; z < lz1; ++z) {
        for (int x = lx0; x < lx1; ++x) {
            RecalcHeightColumn(grid, x, z);
            int br = 15;
            for (int y = sizeY_ - 1; y >= 0; --y) {
                int idx = x + sizeX_ * (y + sizeY_ * z);
                int wx = x + ox_, wy = y + oy_, wz = z + oz_;
                int op = LightRules::OpacityAt(grid, wx, wy, wz);
                if (op < 0) op = 0;
                int blk = LightPack::Block(data_[(size_t)idx]);
                if (op >= 15) {
                    for (int yy = y; yy >= 0; --yy) {
                        int i2 = x + sizeX_ * (yy + sizeY_ * z);
                        int b2 = LightPack::Block(data_[(size_t)i2]);
                        data_[(size_t)i2] = LightPack::Make(0, b2);
                    }
                    break;
                }
                br = LightPack::SaturateSub(br, op);
                data_[(size_t)idx] = LightPack::Make(br, blk);
                if (br == 0) {
                    for (int yy = y - 1; yy >= 0; --yy) {
                        int i2 = x + sizeX_ * (yy + sizeY_ * z);
                        int b2 = LightPack::Block(data_[(size_t)i2]);
                        data_[(size_t)i2] = LightPack::Make(0, b2);
                    }
                    break;
                }
            }
        }
    }
}

void LightMap::PropagateBlock(GridData& grid, int lx0, int lx1, int ly0, int ly1, int lz0, int lz1) {
    for (int z = lz0; z < lz1; ++z)
        for (int y = ly0; y < ly1; ++y)
            for (int x = lx0; x < lx1; ++x) {
                int idx = x + sizeX_ * (y + sizeY_ * z);
                int sky = LightPack::Sky(data_[(size_t)idx]);
                data_[(size_t)idx] = LightPack::Make(sky, 0);
            }

    std::queue<int> q;
    auto inR = [&](int x, int y, int z) {
        return x >= lx0 && x < lx1 && y >= ly0 && y < ly1 && z >= lz0 && z < lz1;
    };
    auto setBlk = [&](int x, int y, int z, int blk) {
        blk = LightPack::Clamp15(blk);
        int idx = x + sizeX_ * (y + sizeY_ * z);
        int sky = LightPack::Sky(data_[(size_t)idx]);
        int cur = LightPack::Block(data_[(size_t)idx]);
        if (blk > cur) {
            data_[(size_t)idx] = LightPack::Make(sky, blk);
            q.push(idx);
        }
    };

    for (int z = lz0; z < lz1; ++z)
        for (int y = ly0; y < ly1; ++y)
            for (int x = lx0; x < lx1; ++x) {
                int emit = LightRules::EmissionAt(grid, x + ox_, y + oy_, z + oz_);
                if (emit > 0) setBlk(x, y, z, emit);
            }

    static const int dx[6] = {1,-1,0,0,0,0};
    static const int dy[6] = {0,0,1,-1,0,0};
    static const int dz[6] = {0,0,0,0,1,-1};
    while (!q.empty()) {
        int idx = q.front();
        q.pop();
        int level = LightPack::Block(data_[(size_t)idx]);
        if (level <= 1) continue;
        int x = idx % sizeX_;
        int y = (idx / sizeX_) % sizeY_;
        int z = idx / (sizeX_ * sizeY_);
        for (int d = 0; d < 6; ++d) {
            int nx = x + dx[d], ny = y + dy[d], nz = z + dz[d];
            if (!inR(nx, ny, nz)) continue;
            int op = LightRules::OpacityAt(grid, nx + ox_, ny + oy_, nz + oz_);
            if (op >= 15) continue;
            int step = (op <= 0) ? 1 : op;
            int nl = LightPack::SaturateSub(level, step);
            setBlk(nx, ny, nz, nl);
        }
    }
}

void LightMap::PropagateBlockLights(GridData& grid) {
    updates_.clear();
    RebuildHeightmap(grid);
    PropagateSky(grid, 0, sizeX_, 0, sizeY_, 0, sizeZ_);
    PropagateBlock(grid, 0, sizeX_, 0, sizeY_, 0, sizeZ_);
}

void LightMap::PropagateBlockLightsLocal(GridData& grid, int minX, int maxX, int minY, int maxY,
                                         int minZ, int maxZ) {
    // Step 3/4 path: enqueue instead of synchronous full local rebuild.
    (void)grid;
    Enqueue(LightLayer::Sky, minX, oy_, minZ, maxX - 1, oy_ + sizeY_ - 1, maxZ - 1);
    Enqueue(LightLayer::Block, minX, minY, minZ, maxX - 1, maxY - 1, maxZ - 1);
}

void LightMap::RefreshColumnSky(GridData& grid, int worldOx, int worldOz, int colsX, int colsZ) {
    int lx0 = worldOx - ox_;
    int lz0 = worldOz - oz_;
    int lx1 = lx0 + (colsX > 0 ? colsX * 16 : 16);
    int lz1 = lz0 + (colsZ > 0 ? colsZ * 16 : 16);
    if (lx0 < 0) lx0 = 0;
    if (lz0 < 0) lz0 = 0;
    if (lx1 > sizeX_) lx1 = sizeX_;
    if (lz1 > sizeZ_) lz1 = sizeZ_;
    PropagateSky(grid, lx0, lx1, 0, sizeY_, lz0, lz1);
}

void LightMap::UpdateSunRays(GridData& grid) {
    RebuildHeightmap(grid);
    PropagateSky(grid, 0, sizeX_, 0, sizeY_, 0, sizeZ_);
}

void LightMap::UpdateSunRaysLocal(GridData& grid, int minX, int maxX, int minZ, int maxZ) {
    Enqueue(LightLayer::Sky, minX, oy_, minZ, maxX, oy_ + sizeY_ - 1, maxZ);
    (void)grid;
}

void LightMap::InvalidateSunRays(int, int) { dirty_ = true; }

void LightMap::AddLight(int x, int y, int z, uint16_t packed) {
    int i = Index(x, y, z);
    if (i < 0) return;
    int nb = LightPack::Block(packed);
    int sky = LightPack::Sky(data_[(size_t)i]);
    int cur = LightPack::Block(data_[(size_t)i]);
    if (nb > cur)
        data_[(size_t)i] = LightPack::Make(sky, nb);
}

uint16_t LightMap::GetLight(int x, int y, int z) const {
    int i = Index(x, y, z);
    if (i < 0) {
        if (y >= oy_ + sizeY_) return LightPack::Make(15, 0);
        return 0;
    }
    uint16_t v = data_[(size_t)i];
    return LightPack::Make(LightPack::Sky(v), LightPack::Block(v), LightPack::Mode(v));
}

uint16_t LightMap::GetFaceLight(int x, int y, int z, int face) const {
    int nx = x, ny = y, nz = z;
    uint16_t mode = LightPack::MODE_DEFAULT;
    switch (face) {
        case 0: nx++; mode = LightPack::MODE_DEFAULT; break;
        case 1: nx--; mode = LightPack::MODE_1800;    break;
        case 2: ny++; mode = LightPack::MODE_0800;    break;
        case 3: ny--; mode = LightPack::MODE_2000;    break;
        case 4: nz++; mode = LightPack::MODE_1000;    break;
        case 5: nz--; mode = LightPack::MODE_2800;    break;
        default: break;
    }
    return (uint16_t)((GetLight(nx, ny, nz) & ~LightPack::MODE_MASK) | mode);
}

void LightMap::SetBrightness(float b) {
    if (b < 0.f) b = 0.f;
    if (b > 1.f) b = 1.f;
    brightness_ = b;
}

void LightMap::UpdateLightMap() {
    static const int kSky[16] = {
        12,  28,  44,  60,  78,  96, 116, 136,
       156, 176, 196, 216, 236, 255, 255, 255
    };
    static const int kBlk[16] = {
        12,  28,  44,  60,  78,  96, 116, 136,
       156, 176, 196, 216, 236, 255, 255, 255
    };
    if (brightnessApplied_ >= 0.f) {
        brightnessApplied_ = brightness_;
        return;
    }
    brightnessApplied_ = brightness_;
    for (int sky = 0; sky < 16; ++sky) {
        for (int block = 0; block < 16; ++block) {
            int s = kSky[sky];
            int b = kBlk[block];
            int r = s > b ? s : b;
            int g = s > b ? s : b;
            int bl = s > b ? s : b;
            if (b > s) {
                r = b;
                g = (b * 230) / 255;
                bl = (b * 200) / 255;
            }
            colorTable_[sky * 16 + block] =
                0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
        }
    }
    tableDirty_ = false;
}

uint32_t LightMap::GetLightColor(uint16_t packed) const {
    int sky = LightPack::Sky(packed);
    int block = LightPack::Block(packed);
    if (sky == 0 && block == 0)
        return 0xFF000000u;
    uint32_t uVar1 = colorTable_[(sky << 4) | block];
    uint32_t mode = (uint32_t)(packed & LightPack::MODE_MASK);
    int scale;
    if (mode == 0x2000) scale = 0x94;
    else if (mode == 0x2800 || mode == 0x1000) scale = 0xd2;
    else if (mode == 0x1800 || mode == 0) scale = 0xa4;
    else return uVar1;
    uint32_t r = ((uVar1 >> 16) & 0xff) * scale >> 8;
    uint32_t g = ((uVar1 >> 8) & 0xff) * scale >> 8;
    uint32_t b = (uVar1 & 0xff) * scale >> 8;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
