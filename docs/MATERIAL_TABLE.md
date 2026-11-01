# Material table recovery (2026-11-01)

## Allocation (verified)
| Offset | Role |
|--------|------|
| GridData+0xe0 | capacity (InitMaterials sets **0x100** = 256) |
| GridData+0xe8 | `Material*` array |
| size | **0x22** (34) bytes × 256 = **0x2200** allocation |

## Source file (verified)
`GridData::LoadMaterials(char const*)` @ 0x987d0:
- Called from `GridModel::LoadResources`
- Opens path via FileStream, reads entire file, parses **line-by-line** (`GetLine`)
- Skips lines starting with `#`
- Source asset: **`assets/scenes/blocks.txt`** (text, not binary)

## Material::Init @ 0x98640 defaults
```
+0x00=0x10  +0x01=0  +0x02=0  +0x03=0  +0x04=0x07  +0x06=0x01
+0x0c..+0x11 = arg0×6   +0x12..+0x17 = arg1×6
+0x1b..+0x1c=0  +0x1e=arg2  +0x20=0xffff
```

## Verified field map
| Off | Field | Consumers |
|-----|-------|-----------|
| +0x01 | prop01 (class 0..0x0a) | NeedFace jump table |
| +0x02 | prop02 | NeedFace opacity checks |
| +0x03 | waterBase | Min/MaxWaterHeight |
| +0x0c..+0x11 | uFace[6] | AddBlockFace UV |
| +0x12..+0x17 | vFace[6] | AddBlockFace UV |
| +0x18/+0x19 | occlusion flags | AddMesh |
| +0x1b+FlowType | flow permission | IsFlowAllowed* |

## blocks.txt columns → fields
**Partial.** Mapped:
- Uxp/Vxp…Uzn/Vzn → uFace/vFace
- BH → waterBase (hypothesis)
- S-Index / BMT / MBI → prop01 (hypothesis: first valid 0..10)
- Col → flow default + prop02 (hypothesis)

**Unresolved:** exact BMT/MBI/SAU/FTL/FTW → remaining bytes; prop02 encoding.

## NeedFace jump returns (expanded)
| Value | Observed meaning |
|-------|------------------|
| 0 | cull |
| 1 | emit |
| 2 | special/partial (prop02==0 paths) |
| 3 | state&7 related case |

Full jump-table decode still open (requires matching every case to prop01).

## GetLightColor mode origin (verified)
- **Only caller family:** `AddBlockFace` (many sites)
- Pattern: `GetLight(x,y,z,face)` → ushort → `GetLightColor(ushort)`
- **Mode applied in GetLight** by face (4th arg):
  - face 5: `or ah, 0x28` → mode **0x2800**
  - face 4: returns **0x200e** (mode 0x2000)
  - other faces: similar constants (0x0f, 0x1800 family)
- Mode is **not** stored per-cell in the light map; it is OR'd at read time for shading.

## Implementation status
| Item | Status |
|------|--------|
| Material 34B layout | Recovered structure |
| InitMaterials / LoadMaterials | Implemented (partial column map) |
| IsFlowAllowed + material perm | Uses table when loaded |
| NeedFace prop01 jump 2/3 | Still open |
| Full column map | Partial |
| GetLight face→mode | Documented; LightMap GetFaceLight not yet OR'ing modes |
