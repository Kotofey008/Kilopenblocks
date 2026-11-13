# Material table recovery (updated 2026-11-13)

## Allocation (verified)
| Offset | Role |
|--------|------|
| GridData+0xe0 | capacity (InitMaterials sets **0x100** = 256) |
| GridData+0xe4 | max S-Index+1 (updated while loading) |
| GridData+0xe8 | `Material*` array |
| size | **0x22** (34) bytes × 256 = **0x2200** allocation |

## Source file (verified)
`GridData::LoadMaterials(char const*)` @ 0x987d0:
- Called from `GridModel::LoadResources` with path from `MtlDescFileName`
- Opens path via FileStream, reads entire file, parses **line-by-line** (`GetLine`)
- Skips lines starting with `#`
- `ParseString` must return **0x1d (29)** fields or logs error
- Source asset: **`assets/scenes/blocks.txt`** (text, UTF-8 BOM optional)

## Material::Init @ 0x98640 defaults
```
+0x00=0x10  +0x01=0  +0x02=0  +0x03=0  +0x04=0x07  +0x06=0x01
+0x0c..+0x11 = arg0×6   +0x12..+0x17 = arg1×6
+0x1b..+0x1c=0  +0x1e=arg2  +0x20=0xffff
```

## blocks.txt columns → Material (exact from decomp)

| # | Column | Material field |
|---|--------|----------------|
| 1 | Index | table slot |
| 2 | S-Index | +0x20 int16 (invalid → 0xffff) |
| 3–14 | Uxp Vxp … Uzn Vzn | +0x0c..+0x11 U (XP YP ZP XN YN ZN), +0x12..+0x17 V |
| 15 | BH | gated by BMT → +0x00 |
| 16 | BMT | +0x01 class 0..0x0a |
| 17 | MBI | +0x02 (clamp 0..6) |
| 18 | SAU | +0x05 (!=0) |
| 19 | Col | +0x06 (!=0) |
| 20 | All | +0x07 (!=0) |
| 21 | UVR | +0x08 |
| 22–24 | XT YT ZT | +0x09 +0x0a +0x0b |
| 25–26 | W H | +0x18 +0x19 (clamp 0..16) |
| 27 | L | +0x1a |
| 28–29 | FTL FTW | +0x1b +0x1c |

### BMT / BH gating
- BMT==0: clamp BH to [1,16] into +0x00 (else 0x10)
- BMT in {1,4} or {2,3,5,6,7,8,9,10}: +0x00 = 0
- other BMT: force BMT=0, then clamp BH

### Post-process by class (+0x01)
- class 0 and height < 0x0e: +0x03=height, +0x04=((0x10-height)>>1)-1
- class 6: +0x03=8, +0x04=3
- class 10: +0x03=2, +0x04=6

## Verified field map / consumers
| Off | Field | Consumers |
|-----|-------|-----------|
| +0x00 | height (BH-derived) | NeedFace partial, slab |
| +0x01 | prop01 / BMT class | NeedFace jump, IsFlowAllowed specials |
| +0x02 | prop02 / MBI | NeedFace |
| +0x03 | waterBase | Min/MaxWaterHeight |
| +0x04 | companion height | derived |
| +0x06 | Col | collision / occlusion |
| +0x07 | All | leaves-style alpha |
| +0x0c..+0x17 | uFace / vFace | AddBlockFace UV |
| +0x18/+0x19 | W/H | occlusion / size |
| +0x1b+FlowType | flow permission | IsFlowAllowed* |
| +0x20 | S-Index | MtlSelIndexToCell / CellToMtlSelIndex |

## Sample rows (after load)
| Type | Comment | class | height | col | FTL | notes |
|------|---------|-------|--------|-----|-----|-------|
| 0 | Air | 0 | 0x10 | 0 | 1 | non-solid |
| 1 | Stone | 0 | 0x10 | 1 | 0 | full occluder |
| 5 | Water | 0 | 0x10 | 0 | 1 | fluid (MBI=3) |
| 10 | Leaves | 0 | 0x10 | 1 | 1 | All=1 |
| 11 | Glass | 0 | 0x10 | 1 | 1 | transparent |
| 12 | Stone Slab | 0 | 8 | 1 | 9 | waterBase=8 |
| 13 | Flower | 1 | 0 | 0 | 1 | cross |
| 72 | Oak Stairs | 6 | 0 | 1 | 9 | class 6 post → wb 8 |

## Implementation status
| Item | Status |
|------|--------|
| Material 34B layout | Done |
| InitMaterials / LoadMaterials | **Exact column map + BMT/BH + post-process** |
| IsFlowAllowed + material perm | Uses +0x1b+FlowType from FTL/FTW |
| NeedFace material occlusion | Uses Col/All/FTL/height/class |
| NeedFace full jump table 0..0xa | Still open (ghidra too many branches) |
| GetLight face→mode | Documented separately |
