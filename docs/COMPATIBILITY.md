# Compatibility Classification

Strict categories (binary evidence required for “Verified equivalent”):

- **Verified equivalent** — algorithm, constants, side-effects and edge cases matched to binary by disassembly + observable behaviour.
- **Partially equivalent** — core idea present; missing side-effects, edge cases, or exact constants; host tests may pass for the happy path.
- **Implemented but unverified** — code exists and may produce plausible output; no binary-level comparison of the algorithm performed.
- **Incorrect** — known divergence from binary (wrong constants, missing required side-effects, wrong data layout).
- **Missing** — no corresponding implementation.
- **Not investigated** — symbols or assets exist; no analysis yet.

## Classification by subsystem

### Engine / lifecycle
| Item | Status | Notes |
|------|--------|-------|
| android_main / GLApplication construction | Partially equivalent | Matches entry pattern; does not use original free-function GameInit etc. |
| ApplicationBase event loop / EGL | Implemented but unverified | Functional EGL; not compared instruction-for-instruction. |
| GameInit / GameUpdate / GameRender free functions | Missing | Original thin wrappers exist; current folds logic into Game/GLApplication. |
| OnForeground / OnBackground / checkpointing | Partially equivalent | Hooks present; body not verified. |

### Scene graph & messaging
| Item | Status | Notes |
|------|--------|-------|
| SceneObject base + virtual table | Partially equivalent | Minimal base; most original virtuals are stubs or missing. |
| LoadSceneObjects (5062 B) | Missing | start.scene is partially parsed for Globals only. |
| NewSceneObject factory | Missing | |
| HierarchyNode / MeshInstance / Model | Missing | |
| Game::HandleSceneMessage (905 B) | Partially equivalent | Simplified string DispatchMessage exists; signature and full routing differ. |
| EntityPlayerSet / EntitySet as SceneObjects | Missing | Flat EntityPlayer + EntityPlayerSet stub. |
| UISceneObject full behaviour | Partially equivalent | UIDocument + hit-test; no full properties / touch-state / element lifecycle. |
| AnimationSet / AnimationState | Not investigated | Large symbol set present. |

### World / GridData
| Item | Status | Notes |
|------|--------|-------|
| SetCell / SetState (5910 B original) | Partially equivalent / Incorrect for side-effects | Current is ~10 lines (write type/state). Original almost certainly marks regions, updates lights, water, meshes, neighbours. Host tests only exercise thin path. |
| Region size 16, EncodeWorldPos | Verified equivalent (constants) | Alignment and key formula confirmed in prior work. |
| type[] + state[] storage | Partially equivalent | Plausible layout; exact packing / region embedding not verified against binary. |
| TracePoint / Place / Break | Partially equivalent | Dual-path structure recovered; DDA approximates FindFirstUsedCell; entity tracers + IsHidden + ConfigureState orientation unresolved. Host test covers subset. |
| UpdateWater | Implemented but unverified | Host test claimed; algorithm vs binary not compared. |
| Lighting (sky+block pack, Propagate, AddTorch) | Implemented but unverified | Host BFS pass; exact emission values / propagation order not proven. |
| LeavesQuality, mesh dirty, Invalidate | Partially equivalent | Fields present; full interaction with SetCell missing. |
| Resize / Store / Restore / Sync / CreateEntity | Missing or stub | Large original methods exist. |

### Terrain / Noise / Trees
| Item | Status | Notes |
|------|--------|-------|
| Noise2D/3D, Noise::Init/GetValue, s_DataPattern | Implemented but unverified | Recovered signatures + Perlin-style; table init / exact permutation not proven. |
| LCG mul 0x343FD add 0x269EC3 | Verified equivalent (constants) | Confirmed in prior tree/terrain work. |
| TerrainGeneratorEarth/Flat/Test | Partially equivalent | Structure and high-level steps present; full Generate body vs binary not audited. |
| TreeBuilder height, wood/leaf IDs | Partially equivalent | Constants match prior notes; placement algorithm not fully verified. |

### Rendering / Mesh / Texture
| Item | Status | Notes |
|------|--------|-------|
| PCT1Vertex / DrawPrimitive | Implemented but unverified | Stride and call present; original has both PCT1 and PsCTb1 variants. |
| RegionMesher (face-cull + greedy) | Implemented but unverified | Host 8³→384/6 faces claimed; winding, light vertex colours, exact UV scale not binary-matched. |
| BlockDef parse + GetFaceUV | Partially equivalent | Text format parsed; V-origin / atlas layout assumed from common GLES practice. |
| blocks.png atlas | Implemented but unverified | Loaded; sampling correctness not proven against original frames. |
| SkyBox / Clouds / Font | Partially equivalent | Present; full billboard / scroll / glyph metrics vs binary not audited. |
| GL state (texture enable, depth, cull) | Implemented but unverified | Functional. |

### Player / Camera / Input
| Item | Status | Notes |
|------|--------|-------|
| EntityPlayer FixedUpdate / Move / CheckGround | Partially equivalent | Axis-separated AABB + gravity; original Move 649 B, more modes/edge cases likely. |
| UpdateCameraTransform (1262 B) | Incorrect / Missing | Current uses simple yaw/pitch matrix each frame. Original is large, uses GetRotationVectors + entity offsets + mode checks. |
| GetRotationVectors | Not used (current reimplements) | Binary function recovered; current code does not call equivalent. |
| AddOrDeleteBlock / reach 6 | Partially equivalent | Ray + place/break present; continuous dig, exact face rules, entity argument unverified. |
| InputMgr touch + HUD zones | Partially equivalent | Hit-test + look deltas; sensitivity tables from Screen/ApplicationBase not wired. |
| Screen::GetTouchSensitivity / GetDefaultTouchSensitivity | Missing (not wired) | Constants exist in binary. |

### Persistence
| Item | Status | Notes |
|------|--------|-------|
| FileStream / Stream / ByteOrderSerialize | Partially equivalent | Basic open/serialize present. |
| EncodeWorldPos / SaveChunk | Partially equivalent | Formula recovered; full chunk format vs binary not fully verified. |
| ELW1 / GameHeader / GameSettings | Implemented but unverified | Round-trip host test claimed; field layout vs original SaveGameHeader not proven instruction-level. |
| Activation data save/load | Missing | ApplicationBase methods exist. |

### Other
| Item | Status | Notes |
|------|--------|-------|
| ENet multiplayer | Not investigated | ~70 symbols. |
| HTTPClient | Not investigated | |
| BBoxTree | Not investigated | |
| Full menu / game-list / settings UI flow | Missing / Partially equivalent | Some message strings handled; UpdateGameListUI etc. not present. |
| Inventory / palette full behaviour | Partially equivalent | SelectPaletteItem/Page present; full UI update paths incomplete. |

## Summary counts (approximate)
- Verified equivalent: few (region size, LCG constants, EncodeWorldPos formula, some symbol signatures).
- Partially equivalent: majority of the “working” core.
- Implemented but unverified: most meshing, lighting, noise, save round-trip, rendering path.
- Incorrect: camera transform path (relative to original UpdateCameraTransform).
- Missing: SceneObject factory/lifecycle, full message routing, animation, networking, many GridData methods, sensitivity tables.
- Not investigated: ENet, Animation, HTTP, BBox, large parts of UI state machines, many Game::* helpers.

**Honest position:** The project has a playable, recognisable core that was reverse-engineered from symbols and limited disassembly. It is not yet close to full functional equivalence. Host tests prove the simplified implementations behave consistently with themselves; they do not prove equivalence with the original binary.


### SetCell/SetState update 2026-10-31
Partially equivalent: bit masks, hierarchy, region dirty recovered. Neighbour SetState conditions and full aux layout unresolved.


### Mesher / Lighting / Water (2026-11-01)
| Subsystem | Class | Notes |
|-----------|-------|-------|
| Region::AddMesh | Partially equivalent | Face cull yes; NeedFace*/AddBlockFace/AddCross/Torch/Ladder missing; greedy is hypothesis |
| AddBlockFace | Missing | 14400 B not recovered |
| Light packing | Partially equivalent | Mode bits 0x3800 missing; table@+0x214 not used |
| AddLight / AddLightUpdate | Partially equivalent | BFS concept; hierarchy + exact opacity open |
| UpdateWaterState | Partially equivalent | No levels/FlowType/IsFlowAllowed |
See docs/MESHER_LIGHTING_WATER.md for full mismatch tables.

### NeedFace / water height / light mode (2026-11-01)
| Item | Class | Notes |
|------|-------|-------|
| NeedFace* | Partially equivalent | Opacity rules implemented; material jump table open |
| Greedy meshing | Incorrect | Not in binary; per-face only |
| Min/MaxWaterHeight | Verified equivalent (arithmetic) | Exact match to binary |
| Light mode bits | Implemented but unverified | Make accepts mode; assignment sites open |
| IsFlowAllowed* | Missing | |

### IsFlowAllowed / UpdateWater / mode (2026-11-01)
| Item | Class | Notes |
|------|-------|-------|
| IsFlowAllowed* | Partially equivalent | Core bit logic recovered; material+0x1b table open |
| UpdateWater + heights | Partially equivalent | Gates on IsFlowAllowed; level encoding not full |
| Light mode assignment | Not investigated fully | Not found in AddLight; only GetLightColor consume |
| Greedy meshing | Incorrect (isolated) | #if 0; not in binary |
| NeedFace return 2/3 | Missing | Requires material jump table |

### Material table / GetLight mode (2026-11-01)
| Item | Class | Notes |
|------|-------|-------|
| Material 34B layout | Partially equivalent | Structure + key fields; full column map open |
| LoadMaterials from blocks.txt | Partially equivalent | Parses text; prop01/UV/flow partial |
| IsFlowAllowed + mat perm | Partially equivalent | Uses table when loaded |
| GetLight face→mode | Verified (documented) | Mode OR at read by face; not per-cell storage |
| NeedFace return 2/3 | Missing | Jump table cases open |
| AddBlockFace | Partially equivalent | EmitFace stand-in; UV can use Material.uFace |
