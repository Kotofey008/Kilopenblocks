# Full Implementation vs Binary Audit — 2026-11-02

**Source chat:** thorough comparison of every major implemented function against pdg / pdf / CFG recovery.  
**Purpose:** durable record so future sessions do **not** re-run the full analysis.  
**Current user focus (do not divert):** finish original-style HUD rendering + make inventory/menu functional. Core world rewrites (SetCell neighbours, NeedFace, AddBlockFace, camera) come **later**.

---

## Classification legend

| Tag | Meaning |
|-----|---------|
| **Verified** | Algorithm, constants, side-effects, edge cases match binary |
| **Partial** | Core idea present; missing side-effects / gates / exact constants / jump cases |
| **Self-made** | Invented to “work”; not derived from binary |
| **Incorrect** | Known divergence from binary |
| **Stub / Missing** | Empty or absent |

---

## 1. GridData

| Item | Status | Notes |
|------|--------|-------|
| SetCell (0x77d80, ~5910 B) | **Partial → Incorrect side-effects** | Writes type + SetState + region dirty + meshesDirty. **Missing:** 16 neighbour SetState `|= 0xC000`, per-axis dirty maps (+0x66d/+0x74d/+0x82d), gate `this+0xf0`. Thin write, not original. |
| SetState + hierarchy 0x4000 | **Partial (good)** | Bit merge + hierarchical occupancy recovered; aux layout simplified |
| FillCells (0x79ac0) | **Verified** | Pure bulk fill, exclusive bounds, no side-effects |
| EncodeWorldPos / region 16 / LCG 0x343FD | **Verified** | |
| IsFlowAllowed* (0x851a0) | **Partial** | Control flow from pdg OK. Special tables prop01 6/7/10 missing → forced 0x0f. Type-id fallback **Self-made** when materials unloaded |
| Min/MaxWaterHeight | **Verified** | Exact arithmetic from pdg |
| UpdateWater / UpdateWaterState | **Partial / Self-made parts** | Uses IsFlowAllowed; level encoding / FlowType SM incomplete |
| FindFirstUsedCell / TracePoint | **Partial** | DDA structure OK; BoxLineSegIntersect → slab approx; region +0x1e always active |
| IsUnderwater | **Incorrect** | `type==5` vs original state mask 0x3fc0 |
| OnLadder / GetCellSelectionBox / ActivateCell | **Stub** | |
| Material 34B + LoadMaterials | **Partial** | Key fields OK; full blocks.txt column map open |

## 2. Mesher / lighting / GL

| Item | Status | Notes |
|------|--------|-------|
| NeedFace* | **Partial** | Type-id occlusion heuristic. Return 2/3 **Missing** (material jump table) |
| AddBlockFace (0x877a0 ~14 KB) | **Missing / Self-made stand-in** | `EmitFace` is invented; no Material UV path, torch/cross/ladder, state geometry |
| Greedy meshing | **Incorrect** | Not in binary; correctly `#if 0` |
| Region::AddMesh | **Partial** | Face cull yes; missing cross/torch/ladder + LeavesQuality gate as original |
| Light pack / GetLight / GetLightColor | **Partial** | Sky+block nibbles plausible; mode bits 0x2000/0x2800 documented, assignment site open |
| PropagateBlockLights | **Partial / unverified** | Classic BFS, torch emit 14 |
| PCT1Vertex stride 24 | **Partial** | Layout OK |

## 3. Camera / Entity / Input

| Item | Status | Notes |
|------|--------|-------|
| UpdateCameraTransform (0x5b580, 1262 B) | **Incorrect** | Current = standard FPS yaw/pitch. Original uses GetRotationVectors + entity offsets + modes |
| Entity Move / CheckGround / FixedUpdate | **Partial** | AABB+gravity; original more modes (ladder/swim/step) |
| AddOrDeleteBlock / reach 6 | **Partial** | Ray+reach present; continuous dig / exact face rules open |
| InputMgr / sensitivity tables | **Partial / Missing** | HUD hit-test OK; Screen/ApplicationBase sensitivity not wired |

## 4. Terrain / Noise / Trees

| Item | Status | Notes |
|------|--------|-------|
| Noise Perlin-style | **Self-made details** | Signatures recovered; perm/fade not proven identical |
| LCG constants | **Verified** | |
| TerrainGenerator* | **Partial** | Structure + Noise/Tree calls; full Generate body unaudited |
| Tree height/IDs | **Verified** | [5,15], Oak=4 Birch=16 Leaves1=10 Leaves2=20 |
| Tree canopy shape | **Self-made** | Spherical/cross layers invented |

## 5. Game / Scene / UI / lifecycle

| Item | Status | Notes |
|------|--------|-------|
| android_main → GLApp loop | **Partial** | Entry pattern OK; free-function Game façade folded in |
| LoadSceneObjects (5062 B) | **Missing / Partial** | Globals + some UISceneObject only |
| HandleSceneMessage / DSL | **Partial** | String dispatcher exists; full SceneObject virtuals missing |
| HUD / palette / UIDocument | **Partial** | Hit-test hud_*; SelectPalette*; full menu SM missing |
| Save/Load ELW1 | **Unverified** | Host round-trip ≠ format identity with original |
| SkyBox / Clouds / Font / Texture atlas | **Partial** | Plausible; not binary-matched |

---

## Root causes of “incorrect game feel”

1. SetCell does not OR 0xC000 onto neighbours → light/water/mesh cascades lag  
2. NeedFace without material jump-table → wrong culling (liquid/leaves/glass)  
3. AddBlockFace not recovered → UV / partial faces / special meshes are stand-ins  
4. Camera path is FPS replacement, not UpdateCameraTransform  
5. IsFlowAllowed special tables missing  
6. No full SceneObject / LoadSceneObjects → UI and menus incomplete  

---

## Priority order (impact)

**Now (user focus — do not skip):**
1. Original-style HUD rendering (UIDocument, hud_*, Font, ortho overlay)
2. Inventory / palette menu functional (SelectPaletteItem/Page, UpdatePaletteUI-like paths, message routing for inventory)

**Later (core fidelity):**
3. SetCell neighbour SetState `|=0xC000` + this+0xf0 gate  
4. NeedFace return 2/3 via selective pdf/CFG  
5. AddBlockFace at least Material UV + light mode  
6. UpdateCameraTransform / GetRotationVectors  
7. IsFlowAllowed special tables  
8. Tree canopy, exact Noise, save format identity  

---

## Evidence locations

- `docs/AUDIT.md`, `docs/COMPATIBILITY.md` (earlier passes)  
- `docs/PDG_AUDIT_2026-11-01.md`  
- `pseudocode/RECOVERED_FUNCTIONS.md`  
- `pseudocode/recovered/0x77d80_SetCell_COMPLETE.md`  
- `pseudocode/recovered/0x5b580_UpdateCameraTransform.md`  
- `pseudocode/pdg_key/` (IsFlowAllowed, water heights, etc.)  
- Source comments in `src/GridData.cpp`, `src/Mesh.cpp`, `src/Lighting.cpp` already mark gaps honestly  

---

## Handoff rule for next sessions

- Do **not** re-audit from scratch. Use this file + COMPATIBILITY.md.  
- User is working on **HUD + inventory/menu**. Support that first.  
- Do not start SetCell/NeedFace/Camera rewrites unless user explicitly asks.  
- When rewriting self-made parts later, prefer binary recovery over “make it playable”.  
