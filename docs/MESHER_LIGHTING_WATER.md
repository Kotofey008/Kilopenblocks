# Region Mesher / Lighting / Water — Binary Analysis (2026-11-01)

Binary-derived facts only. Hypotheses marked separately.

---

## 1. Region::AddMesh @ 0x9b090 (3164 B)

### Signature
`GridData::Region::AddMesh(GridData&, int, int, int, int)`

### Control flow (verified)
1. Iterate cells in region (16³ implied by region system)
2. For each non-air cell: look up Material at GridData+0xe8, stride 34
3. Material property byte +0x01 compared to 0x05 (special material class)
4. Face occlusion bits (0x01,0x02,0x04,0x08,0x10,0x20) tested against neighbour state bytes
5. LeavesQuality at +0xec gates leaves path
6. Per-face dispatch to NeedFace* then geometry emitters

### Callees (verified symbols)
| Address | Symbol | Size | Role |
|---------|--------|------|------|
| 0x833d0 | NeedFaceXP | 1365 | visibility +X |
| 0x83930 | NeedFaceYP | 1081 | +Y |
| 0x83d70 | NeedFaceZP | 1351 | +Z |
| 0x842c0 | NeedFaceXN | 1332 | -X |
| 0x84800 | NeedFaceYN | 1084 | -Y |
| 0x84c40 | NeedFaceZN | 1369 | -Z |
| 0x855c0–0x867f0 | WaterNeedFace* | ~860–1140 | liquid face tests |
| 0x877a0 | **AddBlockFace** | **14400** | solid face geometry |
| 0x8cc70 | AddWaterFace | 2771 | water geometry |
| 0x8d750 | AddCross | 949 | plant/cross mesh |
| 0x8db10 | AddTorch | 305 | torch mesh |
| 0x8dc50 | AddLadder | 1872 | ladder mesh |
| 0x851a0–0x86370 | IsFlowAllowed* | ~180–281 | flow direction gates |

### Mismatch vs current RegionMesher
| Aspect | Binary | Current |
|--------|--------|---------|
| Architecture | Specialized emitters per block class | Single IsSolid + EmitFace / greedy |
| Face test | NeedFace* (1000+ B each, material-aware) | Opaque neighbour check |
| Water | Separate WaterNeedFace + AddWaterFace | Treated as non-solid only |
| Plants | AddCross | Skipped as non-solid |
| Torch/Ladder | Dedicated emitters | Skipped / solid |
| Greedy merge | **Not observed** in AddMesh call graph | Implemented (hypothesis, not binary) |
| AddBlockFace | 14 KB geometry with lighting/UV | Simple quad |

**Classification: Partially equivalent (face culling concept only).**

---

## 2. Lighting subsystem

### Functions
| Address | Symbol | Size | Complexity |
|---------|--------|------|------------|
| 0x94480 | GetLight(int,int,int,int) | 860 | — |
| 0x94400 | GetLightInside | 123 | — |
| 0x947e0 | GetLightColor(ushort) | 318 | — |
| 0x94920 | UpdateLightMap | 566 | — |
| 0x92cb0 | SetAddLights(int,int) | 1837 | — |
| 0x94b60 | **AddLightUpdate** | **4132** | 91 |
| 0x95b90 | AddLight(int,int,int,ushort) | 2977 | 62 |
| 0x96740 | AddLights() | 1963 | — |

### Light value packing (verified from GetLightColor)
```
bits [0:3]  = channel A (mask 0x0f)   — often treated as one nibble
bits [4:7]  = channel B (mask 0xf0)   — other nibble
bits [11:13]= mode (mask 0x3800)
  observed mode values: 0x1800, 0x2000, 0x2800
Color = lookup table at GridData+0x214 indexed by (A<<4)|B, then scaled by mode
```

**Hypothesis (not fully verified):** A=sky, B=block matches prior recovery and AddLight masking with 0x0f.
**Fact:** packing is richer than two independent 4-bit levels; mode bits exist.

### AddLight (0x95b90) structure
- Seeds light at (x,y,z) with ushort value
- Masks with 0x0f for one channel
- Uses types@+0x74, materials@+0xe8
- Hierarchical maps at +0x280..+0x2a0 (same family as SetState 0x4000 maps)
- 24 loops → multi-direction propagation

### AddLightUpdate (0x94b60) structure
- Largest lighting function (4132 B)
- Calls IsFlowAllowed* (shared with water?)
- Hierarchical maps +0x280 family
- Flood / BFS-style with opacity tests
- Called from higher-level light invalidation

### Mismatch vs current LightMap
| Aspect | Binary | Current |
|--------|--------|---------|
| Storage | Parallel maps on GridData + hierarchy | Separate LightMap vector |
| Packing | nibble+nibble+mode bits | sky[0:3]+block[4:7] only |
| Color | Table @ +0x214 with mode scaling | Linear RGB from levels |
| Propagation | AddLight + AddLightUpdate (7000+ B combined) | Simple BFS in PropagateBlockLights |
| SetAddLights | 1837 B entry | Not present |
| Opacity | Material-driven (via +0xe8) | Hardcoded IsOpaque list |

**Classification: Partially equivalent (BFS concept + torch emit 14).**

---

## 3. UpdateWaterState @ 0xa1040 (2513 B)

### Related symbols
| Address | Symbol | Size |
|---------|--------|------|
| 0xa1040 | UpdateWaterState(int) | 2513 |
| 0x851a0–0x86370 | IsFlowAllowedXP/YP/ZP/XN/YN/ZN | 180–281 |
| 0x86b70 | Material::MinWaterHeight | 52 |
| 0x86c70 | Material::MaxWaterHeight | 64 |
| 0x86bb0 | WaterThresholdTest | 177 |

### Verified facts
- Takes an int parameter (likely cell index or generation counter)
- Uses `GridData::FlowType` and `GridData::LiquidType` enums
- Material defines Min/Max water height from state
- Flow allowed tests are direction-specific and type-aware
- Called from GridModel::FixedUpdate

### Mismatch vs current UpdateWater
| Aspect | Binary | Current |
|--------|--------|---------|
| Levels | Min/Max height from material + state | Binary water/air only |
| Flow | IsFlowAllowed* per direction + FlowType | Fall down, then horizontal spread |
| Threshold | WaterThresholdTest | None |
| State | Water level encoded in cell state | Type=5 only |

**Classification: Partially equivalent (fall + horizontal spread concept).**

---

## 4. Call relationships (verified)

```
SetCell / world mutation
  └─ (side effects still partial)
       └─ light invalidation → SetAddLights / AddLightUpdate
       └─ water dirty → UpdateWaterState (from FixedUpdate)

Region rebuild (MarkModifiedRegions → dirty regions)
  └─ Region::AddMesh
       ├─ NeedFace* / WaterNeedFace*
       ├─ AddBlockFace / AddWaterFace / AddCross / AddTorch / AddLadder
       └─ MeshBuffer::PushQuads*

GetLight / GetLightColor used by AddBlockFace for vertex colors
```

---

## 5. Implementation status

| Subsystem | Status | Notes |
|-----------|--------|-------|
| RegionMesher | Partially equivalent | Face cull yes; specialized emitters missing; greedy is hypothesis |
| LightMap packing | Partially equivalent | Mode bits missing |
| Light propagation | Partially equivalent | BFS present; hierarchy + exact opacity open |
| UpdateWater | Partially equivalent | No levels/FlowType |
| AddBlockFace | Missing | 14 KB not recovered |
| NeedFace* | Missing as separate functions | Logic folded into IsSolid |
| IsFlowAllowed* | Missing | |

### Still tracked open (from prior work)
- BoxLineSegIntersect full body
- FindFirstUsedCell region+0x1e
- IsHidden tables
- ConfigureState liquid/slab
- Entity tracers
- SetCell neighbour SetState predicates
