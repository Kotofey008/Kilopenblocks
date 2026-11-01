# Reverse-Engineering Audit (deeper pass) — Exploration Lite 1.3.3

**Date:** 2026-10-31 (second pass)  
**Constraint:** No implementation changes in this pass; evidence and classification only.

## 1. Method

- Full symbol inventory of x86 `libExplorationLite.so` via Rizin (`rz-bin -s`).
- Targeted disassembly of entry points, SetCell, UpdateCameraTransform, GetRotationVectors, GetDefaultTouchSensitivity, LoadSceneObjects size, Game free-functions.
- Source-tree inventory of every .h/.cpp under ExplorationLite_RE.
- Comparison of binary method sizes vs current implementation sizes (SetCell 5910 B vs ~10 lines is the clearest example).
- Asset inspection (start.scene, blocks.txt, ui/, textures/).

Host tests and “compiles + looks plausible” are **not** treated as evidence of equivalence.

## 2. Architecture conclusion

See `ARCHITECTURE.md`.

The original is a SceneObject-centric engine with a text-driven factory (`LoadSceneObjects` 5062 B), thin free-function game façade, and large derived-class methods. The current tree is a simplified, flat `Game` + standalone `Camera`/`EntityPlayer` reimplementation of the playable core.

**Can the simplified architecture support full functional equivalence?**  
No. It can support an approximation of the core voxel loop. Full equivalence (exact UI/menu state machines, object identity, properties round-trips, any multiplayer/animation paths, exact camera attachment) requires either reconstructing substantial parts of the SceneObject hierarchy or proving every original call path can be expressed through the façade — which has not been shown.

## 3. Subsystem status (see COMPATIBILITY.md for full table)

**Reassessment of previously “solid” items:**

- **GridData / SetCell** — Partially equivalent at best. Original SetCell is 5910 bytes; current is a thin write. Side-effects (region mark, light, water, mesh dirty, neighbours) are missing or incomplete. Host tests only exercise the thin path. **Not solid.**
- **Mesher** — Implemented but unverified. Face counts from host tests do not prove winding, UV scale, light vertex packing, or greedy merge rules match the binary.
- **Lighting / water** — Implemented but unverified. BFS host passes do not equal binary propagation order or emission constants.
- **Noise / LCG / trees** — Constants (LCG, region 16) verified; full algorithms only partially equivalent / unverified.
- **Save/load** — Implemented but unverified. Round-trip tests prove internal consistency, not format identity with original SaveGameHeader / chunk layout.
- **BlockDef / UV** — Partially equivalent. Text parse matches observed columns; atlas sampling orientation assumed, not proven.
- **Camera / movement / input** — Camera path is **incorrect** relative to the 1262 B UpdateCameraTransform. Movement and input are partial.

**Missing or not investigated (non-exhaustive):**
- Full SceneObject factory and lifecycle
- HierarchyNode / MeshInstance / Model / AnimationSet
- ENet (~70 symbols)
- HTTPClient, BBoxTree
- Many GridData methods (Resize, Store, Restore, Sync, CreateEntity, GetCellSelectionBox 3491 B, ActivateCell, …)
- Game UI helpers (UpdatePaletteUI 1186 B, UpdateGameListUI 2055 B, …)
- Exact sensitivity tables and Screen size enums
- Continuous dig / hold behaviour
- Multiplayer if present in original

## 4. Discrepancy map (expanded)

| Area | Original | Current | Classification |
|------|----------|---------|----------------|
| Entry / loop | android_main → GLApp → AndroidMain + Game* free functions | android_main → GLApp → direct Game | Partial |
| Scene load | LoadSceneObjects 5062 B text factory | Minimal Globals parse + separate HUD | Missing / Partial |
| Object model | Rich SceneObject + Hierarchy + EntitySet | Minimal stubs | Missing |
| Message system | HandleSceneMessage + virtuals | Simplified DispatchMessage | Partial |
| Grid write | SetCell 5910 B + side-effects | Thin write | Partial / Incorrect side-effects |
| Region / chunk | 16³, EncodeWorldPos, mesh per region | 16 used, external mesher | Partial (constants OK) |
| Player camera | UpdateCameraTransform 1262 B | Simple yaw/pitch matrix | Incorrect |
| Input sensitivity | Screen + ApplicationBase tables | Hard-coded / approximate | Missing |
| Meshing / lighting / water | Large original methods | Host-tested approximations | Implemented but unverified |
| Terrain / noise / trees | Full generators + Noise class | Recovered signatures + constants | Partial |
| Save format | Stream + header + chunks | ELW1-like + round-trip | Implemented but unverified |
| UI / menus | Large Game::* UI updates + scene objects | HUD hit-test + some messages | Partial / Missing |
| Networking / anim / HTTP | Present in binary | Absent | Not investigated / Missing |

## 5. Evidence examples

- SetCell size and call density (many XREFs) show it is a central, heavy function; current implementation cannot be equivalent.
- UpdateCameraTransform size + call to GetRotationVectors + multiple entity field offsets show a non-trivial attachment path; current frame-by-frame SetPosition/SetYawPitch is a different design.
- LoadSceneObjects size (5062 B) and start.scene structure (Camera + many objects with full property blocks) show the original is scene-graph driven; current is not.
- Many SceneObject base methods are 9-byte stubs → real logic lives in overrides that current code does not implement.

## 6. What this means for future work

1. Treat every “host test PASS” as internal consistency only until binary algorithms are matched.
2. Prefer recovering one original method completely (disassembly → constants → edge cases → side-effects) over broad approximate coverage.
3. Architecture decision: continue façade for playable core, or begin selective SceneObject reconstruction when a concrete missing behaviour cannot be expressed otherwise. Document the choice explicitly.
4. Do not tune camera, textures, or movement until the underlying transform and input paths are classified and either matched or deliberately accepted as approximations.

## Related documents
- ARCHITECTURE.md — original vs current structure
- COMPATIBILITY.md — full classification table
- FUNCTION_MAP.md — address/size map (to be expanded)
- TODO.md — ordered investigation and verification tasks


## SetCell recovery (2026-10-31)
Partial reimplementation performed. Verified: index, type change path, state bit masks, region dirty marking, meshesDirty. Unresolved: neighbour fluid/light loops, exact auxiliary map semantics. See COMPATIBILITY.md and TODO.md. Classification remains Partially equivalent.


## TracePoint / Place / Break recovery (2026-10-31)
Structural recovery of the full interaction path completed. TracePoint has dual paths (entity tracers + FindFirstUsedCell DDA). FindFirstUsedCell is 2541 B and only approximated by current Amanatides–Woo DDA. IsHidden, entity-tracer list, and ConfigureState orientation bits remain unresolved. Host tests cover the recovered subset. Classification: Partially equivalent. SetCell neighbour/lighting/fluid propagation still unresolved.

## FindFirstUsedCell / ConfigureState (2026-10-31)
FindFirstUsedCell structure recovered and implemented (slab AABB + DDA + type check). Full BoxLineSegIntersect and region+0x1e not matched. ConfigureState orientation 0-3 implemented; liquid/slab paths open. IsHidden returns false pending table mapping. Entity tracer list empty. SetCell neighbour/light still unresolved.

## SetCell/SetState deeper recovery (2026-10-31)
SetState algorithm recovered: combined state write, 0x4000 hierarchical occupancy at four shift levels, dual aux maps. SetCell: type-change path, bit flags, region+0x1f dirty, per-axis markers, ~14 neighbour SetState calls. Implemented hierarchy + neighbour region dirty. Exact neighbour predicates and light/fluid inline rules still open. Classification remains Partially equivalent.


## Mesher / Lighting / Water mapping (2026-11-01)
Full call-graph and size inventory of Region::AddMesh, NeedFace*, AddBlockFace (14KB), AddWaterFace, AddCross, AddTorch, AddLadder, AddLight, AddLightUpdate, SetAddLights, GetLight/GetLightColor, UpdateWaterState, IsFlowAllowed*, water height helpers. Documented in docs/MESHER_LIGHTING_WATER.md. Current implementations remain approximate; no large rewrite performed. Light packing mode bits and specialized mesh emitters are the largest gaps.

## NeedFace / light modes / water heights (2026-11-01)
NeedFaceXP recovered as template; RegionMesher::NeedFace implements opacity-based 0/1. Greedy merging confirmed absent from binary. Min/MaxWaterHeight arithmetic implemented exactly. Light mode mask 0x3800 and scale factors documented; assignment not recovered. See NEEDFACE_LIGHT_WATER.md.
