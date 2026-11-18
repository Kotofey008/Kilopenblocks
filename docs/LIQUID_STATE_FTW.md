# Liquid world model + FTW/FTL (Ghidra)

## World storage (not material type 5)

| Source | type | state |
|--------|------|-------|
| AddGrassAndWater (lakes) | **0** | **0x2000** |
| AddLiquidSource(LiquidType=0) | **0** | **0xE000** (= 0xC000\|0x2000) |
| AddLiquidSource(LiquidType≠0) | **0** | **0xC3C0** (= 0xC000\|0x03C0) |
| Place material type 5 | **0** | **0xC3C0** (lava!) |

- Water fluid bit: `0x2000`
- Level (water): `(state & 0x1C00) >> 10` → 0..7
- Any liquid presence: `state & 0x3FC0`
- Lava profile: `0x03C0` set, `0x2000` clear
- Material **id 5** in blocks.txt: S-Index=-1, UV of water, **hidden** from palette; not written by natural generation

## FTW / FTL (blocks.txt → Material +0x1b / +0x1c)

Loaded in `LoadMaterials` into `raw[0x1b]=FTL`, `raw[0x1c]=FTW`.

`IsFlowAllowed*(cell, FlowType)` reads:

```text
perm = materials[type].raw[0x1b + FlowType]
```

`FlowType`: XP=0, XN=1, YP=2, YN=3, ZP=4, ZN=5.

Only two columns exist in the text table, so:

- **FTL** → `raw[0x1b]` — used for FlowType 0 (and as the primary horizontal/default perm byte)
- **FTW** → `raw[0x1c]` — used for FlowType 1

Further FlowType indices (2..5) read `raw[0x1d..0x20]` which stay **0** unless other load paths fill them; binary still indexes `0x1b+FlowType` per direction.

### Bit meaning of a perm byte (from IsFlowAllowed* decompile)

| Bits | Meaning |
|------|---------|
| **bit0 = 1** | Flow allowed → return `0x0F` (unless bit3 special) |
| **bit0 = 0, bit1 = 1** | Flow blocked → return 0 |
| **bit0 = 0, bit1 = 0, bit2 = 1** | Conditional on cell `state&3==3` |
| **bit3 = 1** (with bit0) | Class-dependent specials (`prop01` 0/6/7/10 + state bits) |

### Values seen in blocks.txt

| Value | Typical use |
|-------|-------------|
| **0** | Full solid (stone, dirt, wood…) — flow denied |
| **1** | Passable / fluid-friendly (air, water row, leaves, glass, flowers…) — flow allowed |
| **9** | Partial geometry (slabs, stairs, doors, beds, plates) — allow + special class path |
| **2,3,4** | Rare actives (torch/lamp/TV/furnace) — intermediate / special |

**FTL vs FTW** on one row: usually equal. Differences (e.g. door FTL=1 FTW=9) give **direction-dependent** flow permission when FlowType selects +0x1b vs +0x1c.

Water material row (id 5): FTL=1, FTW=1 — if that type were used as a solid cell, flow through it would be allowed; world water does not use this type.

## Project mapping (2026-11-18)

- Terrain / AddLiquidSource → type 0 + water/lava states above
- UpdateWater / mesher IsWaterCell → `state & 0x2000`
- Type 5 left in materials table for icons/UV only
