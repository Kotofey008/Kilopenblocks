# BMT Models Recovery Tracker

**Goal:** restore original 1:1 behaviour of all block model classes (BMT / Material[+0x01] = 0..10).  
**Source of truth:** Ghidra C dump `ghidra/libExplorationLite_x86.so.c` + Rizin when needed.  
**Last analysis:** 2026-11-28.

Legend:
- **Severity:** P0 = visual/physics breakage or wrong culling; P1 = noticeable wrong shape/UV/light; P2 = missing edge-case / polish.
- **Status:** Open / Partial / Verified.
- **Evidence:** primary address or function name in original.

---

## 0. Shared infrastructure (affects all BMT)

| ID | Component | Severity | Status | Original evidence | Current project state | Required recovery |
|----|-----------|----------|--------|-------------------|-----------------------|-------------------|
| INFRA-01 | NeedFace* jump tables (6 faces × BMT 0..10) | **P0** | **Partial** | NeedFaceXP @ 0x833d0; rizin PIC tables resolved 2026-11-28 | BMT switch from XP tables: 1/2/3/4→0, 5→1, 8→(state&7==3?2:1), 9/10→3, 6/7→2; BMT0 = height+state&4+MBI+same-type All/Leaves. Boundary vs non-boundary still collapsed; full YP/YN + stairs/door bodies open. | Finish face-specific cases and BMT6/7 from 0x834e0/0x83550. |
| INFRA-02 | NeedFace return → AddBlockFace flags | **P0** | **Partial** | AddBlockFace: `(param_6 & 2)==0 → GetLightInside else GetLight` | needFlags bit1 selects neighbour light vs self-cell + face mode. Geometry still does not branch on 2/3 beyond existing slab paths. | Drive partial UV/vert paths from needFlags once tables recovered. |
| INFRA-03 | AddBlockFace light path (GetLightInside vs GetLight) | P1 | **Partial** | same as INFRA-02 | Wired via needFlags bit1 (2026-11-28). mat[+0x1b] override still missing. | Add mat[+0x1b] bit test from original. |
| INFRA-04 | UVR handling inside AddBlockFace | P1 | Partial | mat[+0x08] UVR bits; table lookup `unaff_EBX + 0x829f7 + (face + facing*6)*4` | Partial bit1 for Y-faces + UvrTableFacing | Full UVR bit0/bit1 + facing remap table recovery. |
| INFRA-05 | LeavesQuality force layer | P2 | Partial | AddMesh: if LeavesQuality==0 (Off) force MBI=0 for types 10/0x14/0x55 | Present for those three types | Verify all leaf-like types and exact condition. |
| INFRA-06 | Material post-process (class 0/6/10 → +0x03/+0x04) | P2 | Verified | LoadMaterials after parse | Implemented | Keep; only re-verify if BH gating changes. |
| INFRA-07 | Face enum (0=XP … 5=ZN) | — | Verified | GetAddingPosition jumptable @ 0x99320 | Aligned 2026-11-24 | Do not change. |

---

## 1. Per-BMT geometry & behaviour

### BMT 0 — Solid / Slab

| ID | Issue | Severity | Status | Original | Project | Action |
|----|-------|----------|--------|----------|---------|--------|
| BMT0-01 | Full cube path | P1 | Partial | Exact face verts + UV from tables | EmitFace | Prefer recovered table over EmitFace when height==0x10 && !upside. |
| BMT0-02 | Partial height (slab) geometry | P0 | **Partial** | Height +0x00, state&4 upside; side V reduced by ht; Y at slab plane | y0/y1 1/16, V frac, full only ht≥0x10 (2026-11-28) | Binary face-template ×8 still via floats; UV 0x11 fixed-point not bit-exact. |
| BMT0-03 | Slab ↔ full mutual cull | P0 | **Partial** | NeedFaceXP height/xor; NeedFaceYP/YN edge specials | nHt>0xf→0, nHt<self→2, xor&4; Y-edge from 0x83982/0x848e0 | Region boundary tables still collapsed; confirm stacked slabs in-game. |
| BMT0-04 | Slab merge / FullHeightBlockVariant | P1 | Partial | FullHeightBlockVariant @ 0x98f20; place rules | Present in GridData | Re-verify against binary after NeedFace fixed. |

### BMT 1 & 4 — Cross / Cane

| ID | Issue | Severity | Status | Original | Project | Action |
|----|-------|----------|--------|----------|---------|--------|
| BMT1-01 | Push method difference | P1 | Open | class 1 → PushTwoSidedQuads; class 4 → PushQuads + PushBackQuads | Always 4 quads (two diagonals double-sided) | Restore exact MeshBuffer helpers and call sites. |
| BMT1-02 | UV source | P2 | Partial | param_4[0xe] / [0x14] (Uzp/Vzp) | Same | OK |
| BMT1-03 | XT/YT/ZT offset | P2 | Partial | signed char +0x09..0x0b | Present | OK |
| BMT1-04 | Special jitter (S-Index/flag 0x4c) | P2 | Open | LCG 0x343FD position jitter when *(short*)(+0x1e)==0x4c | Absent | Recover if any block uses it (check blocks.txt / runtime). |
| BMT1-05 | Collision / selection box | P1 | Partial | GetCellSelectionBox BMT1/4 centred by W/H | Present (0x8f080-style) | Verify numbers exact. |

### BMT 2 — Torch

| ID | Issue | Severity | Status | Original | Project | Action |
|----|-------|----------|--------|----------|---------|--------|
| BMT2-01 | Geometry table | **P0** | Open | state&7 → rodata short* table → PushQuads (6 faces) | Thin stem approx + basic orient | Recover table + PushQuads call exactly. |
| BMT2-02 | Light argument | P1 | Open | GetLightInside(..., 1) | Generic FaceColor | Match. |
| BMT2-03 | Collision | P1 | Partial | Non-solid (Col / class) | CellHasCollision returns false for class 2 | OK; confirm selection box. |

### BMT 3 — Ladder

| ID | Issue | Severity | Status | Original | Project | Action |
|----|-------|----------|--------|----------|---------|--------|
| BMT3-01 | Plane placement + side flags | **P0** | Open | Complex region side checks (0x2ed/0x4ad), state&7, wall attach | Single thin plane, orient 0..3 simplified | Recover full AddLadder body. |
| BMT3-02 | Light direction | P1 | Open | Light from orient | Generic | Match. |
| BMT3-03 | OnLadder detection | P1 | Partial | class 3 overlap body AABB | Present | Verify extents. |
| BMT3-04 | Place rules (reject Y faces) | P1 | Partial | IsPossibleToAdd rejects face 1/4 | Present | Keep. |

### BMT 5 — Fence

| ID | Issue | Severity | Status | Original | Project | Action |
|----|-------|----------|--------|----------|---------|--------|
| BMT5-01 | Post + bar geometry | **P0** | Open | Tables inside AddBlockFace | Center post 6-10/16 + neighbour bars (Minecraft-like) | Recover exact verts / connection rules from binary. |
| BMT5-02 | Neighbour test | P1 | Partial | Solid or fence | prop01==5 or IsSolid | Refine to original predicate. |
| BMT5-03 | Collision multi-AABB | P1 | Partial | GetCellCollisionBoxArray | Present approximate | Match binary boxes. |

### BMT 6 — Stairs

| ID | Issue | Severity | Status | Original | Project | Action |
|----|-------|----------|--------|----------|---------|--------|
| BMT6-01 | Step geometry + UV | **P0** | Open | state&3 facing, state&4 upside → tables | Two half-slabs + high-side logic | Recover exact vertex/UV tables. |
| BMT6-02 | NeedFace specials for stairs | P0 | Open | Jumptable cases for class 6 | Missing | Via INFRA-01. |
| BMT6-03 | Collision AABBs | P1 | Partial | Multi-box | Approximate | Match. |

### BMT 7 — Door

| ID | Issue | Severity | Status | Original | Project | Action |
|----|-------|----------|--------|----------|---------|--------|
| BMT7-01 | Mesh (open / upper / facing) | **P0** | Open / Partial | Full logic in AddBlockFace | Mesh path incomplete / fallback | Recover door geometry + state bits. |
| BMT7-02 | Vertical pair (upper/lower) | P1 | Partial | ActivateCell + collision walk Y±1 | Present in ActivateCell / collision | Keep; ensure mesh matches. |
| BMT7-03 | Open bit 0x08 | P1 | Partial | Toggle in ActivateCell | Present | Verify mesh reacts. |
| BMT7-04 | Collision boxes | P1 | Partial | Multi-AABB | Approximate | Match. |

### BMT 8 — Lamp (small)

| ID | Issue | Severity | Status | Original | Project | Action |
|----|-------|----------|--------|----------|---------|--------|
| BMT8-01 | Inset cube geometry | P1 | Partial | Size from W/H/L or fixed inset | Via size / fallback | Recover exact inset offsets. |
| BMT8-02 | Orientation / state bits | P2 | Open | Possible state encoding | Minimal | Check binary. |

### BMT 9 — Fence Gate

| ID | Issue | Severity | Status | Original | Project | Action |
|----|-------|----------|--------|----------|---------|--------|
| BMT9-01 | Geometry (open/closed + facing) | **P0** | Open | Tables in AddBlockFace | Fence-like approximate | Recover exact. |
| BMT9-02 | Open bit + collision | P1 | Partial | ActivateCell open bit | Present | Mesh must follow. |

### BMT 10 — Trapdoor

| ID | Issue | Severity | Status | Original | Project | Action |
|----|-------|----------|--------|----------|---------|--------|
| BMT10-01 | Flat vs open vertical | P1 | Partial | state open + facing; height ~3/16 | Explicit code present | Verify numbers vs binary (3/16, wall attach). |
| BMT10-02 | NeedFace / place rules | P1 | Open | Class 10 specials | Partial | Via INFRA-01 + IsPossibleToAdd. |
| BMT10-03 | Collision | P1 | Partial | Multi-AABB | Approximate | Match. |

---

## 2. Priority order for recovery work (this chat)

1. **INFRA-01 + INFRA-02** — NeedFace jumptables + needFlags wiring  
   (without this every special model still culls wrong)

2. **BMT0-02 / BMT0-03** — exact slab geometry + cull  
   (most common blocks)

3. **BMT2-01, BMT3-01** — Torch & Ladder tables  
   (small functions, high visual impact)

4. **BMT1-01** — Cross PushTwoSided vs Quads+Back

5. **BMT5 / BMT6 / BMT7 / BMT9 / BMT10** — fence, stairs, door, gate, trapdoor  
   (large AddBlockFace cases; do one class at a time)

6. Remaining P1/P2 (UVR, light path, collision boxes, jitter)

---

## 3. Working rules

- Never invent geometry “that looks ok”. Prefer recovered constants / tables from pdf, CFG or rodata.
- After each class is fixed: host visual check + collision/ray test if applicable.
- Keep Material layout and LoadMaterials unchanged unless a proven bug is found.
- Document recovered addresses / table offsets in this file or FUNCTION_MAP.md.
- When a batch is done: full project zip under `for_github/` (entire ExplorationLite_RE/).

---

## 4. Quick reference — original function addresses (x86)

| Function | Approx. addr | Notes |
|----------|--------------|-------|
| Region::AddMesh | 0x9b090 area / Ghidra 71176 | Dispatch |
| AddBlockFace | 0x877a0 | Main geometry |
| AddCross | 0x8d750 | |
| AddTorch | 0x8db10 | |
| AddLadder | 0x8dc50 | |
| NeedFaceXP family | 0x833d0+ | Jumptables |
| GetCellCollisionBoxArray | (search) | Multi-AABB |
| GetCellSelectionBox | ~0x8f080 | |
| LoadMaterials | 0x987d0 | |
| Material::Init | 0x98640 | |

---

*This file is the single source of truth for BMT recovery work in subsequent turns.*
