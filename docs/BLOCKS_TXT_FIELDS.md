# blocks.txt field reference (original binary + asset)

Source of truth:
- Asset: `assets/scenes/blocks.txt` via `start.scene` → `GridModel.MtlDescFileName`
- Loader: `GridData::LoadMaterials` @ `0x987d0` (29 fields / `ParseString == 0x1d`)
- Table: `Material[256] × 0x22` at `GridData+0xe8`
- Init defaults: `Material::Init` @ `0x98640`

This document merges:
1. In-game / asset observations (author notes)
2. Ghidra / binary recovery (`IsFlowAllowed*`, `LoadMaterials`, `GetCellCollisionBoxArray`, `ConfigureState`, liquid paths)

Confidence tags: **CONFIRMED** (decomp + consistent asset), **STRONG** (asset pattern + partial decomp), **HYPOTHESIS** (observation only / open in binary).

---

## Field map (column → Material offset)

| # | Name | Material | Type after load |
|---|------|----------|-----------------|
| 1 | Index | table slot | 0..255 (asset uses 0..112) |
| 2 | S-Index | `+0x20` int16 | −1 → stored as invalid/`0xFFFF` path |
| 3–14 | Uxp Vxp … Uzn Vzn | `+0x0c..+0x17` | u8 UV tile coords |
| 15 | BH | gated → `+0x00` | u8 height units (1/16) |
| 16 | BMT | `+0x01` | u8 class 0..0x0A |
| 17 | MBI | `+0x02` | u8 clamp 0..6 |
| 18 | SAU | `+0x05` | u8 bool |
| 19 | Col | `+0x06` | u8 bool |
| 20 | All | `+0x07` | u8 bool |
| 21 | UVR | `+0x08` | u8 |
| 22–24 | XT YT ZT | `+0x09..+0x0b` | u8 |
| 25–26 | W H | `+0x18..+0x19` | u8 clamp 0..16 |
| 27 | L | `+0x1a` | u8 (signed values as two’s complement byte) |
| 28–29 | FTL FTW | `+0x1b..+0x1c` | u8 flow-perm bytes |

---

## Index

- **Meaning:** Block type id written into `cellTypes[]`.
- **Range (asset):** 0..112. **Engine:** 256 slots.
- **CONFIRMED.**

## S-Index

- **Meaning:** Inventory / palette selection id (not the same as world type).
- **−1:** hidden from palette (variants, parents, water material row, etc.).
- **≥0:** order / id in block picker.
- Loader stores int16 at `+0x20`; invalid → `0xFFFF`.
- Helpers: `MtlSelIndexToCell` / `CellToMtlSelIndex` (symbols present).
- **CONFIRMED.**

## Uxp Vxp … Uzn Vzn

- **Meaning:** Atlas tile column (U) and row (V) for faces  
  order in file/load: **XP, YP, ZP, XN, YN, ZN** (U then V pairs).
- **Asset range:** U 0..14, V 0..8 on 16×16 grid over `textures/blocks.png`.
- Consumed by `AddBlockFace` / specialized emitters for UV.
- **CONFIRMED.**

## BH (Block Height)

- **Meaning:** Block height in 1/16 cell units for **BMT == 0** solids (slabs, snow layer, bed half, etc.).
- **Asset values:** 0, 1, 8, 9 (also theoretically 1..16).
- **Loader gating (CONFIRMED from decomp):**
  - BMT==0: if BH∈[1,16] → `+0x00=BH`, else `+0x00=0x10` (full)
  - BMT∈{1..10}: `+0x00=0` (height ignored for class models)
- **Post-process:** class 0 with height &lt; 0x0e fills waterBase/companion (`+0x03/+0x04`).
- Author examples (1 snow/wood plate, 8 slab, 9 bed half) match asset + gating.
- **CONFIRMED.**

## BMT (Block Model Type) — `+0x01`

| Value | Model (author + mesher symbols) | Notes |
|------|----------------------------------|--------|
| 0 | Full / height-solid | Uses BH; default cubes & slabs |
| 1 | Cross | Flowers, mushrooms, tall grass (`AddCross`) |
| 2 | Torch | `AddTorch`; orientation in cell state |
| 3 | Ladder | `AddLadder` |
| 4 | Cross-like (sugar cane) | Author: cross; binary still class 4 (height forced 0) |
| 5 | Fence | Multi-AABB collision branch |
| 6 | Stairs | Post-process waterBase; multi-AABB |
| 7 | Door | State open/facing |
| 8 | Small lamp | W/H hitbox in asset |
| 9 | Fence gate | Collision special (`GetCellCollisionBoxArray` cmp 9) |
| 10 | Trapdoor | Class 0x0A specials in flow/height helpers |

- Invalid BMT at load → forced to 0 + BH clamp.
- Drives `Region::AddMesh` dispatch and `NeedFace*` jump family.
- **CONFIRMED** for 0..10 as class id; mesh names **STRONG** from symbols + asset labels.

## MBI (Material Blend Index) — `+0x02`

Loader **clamp 0..6**.

| Value | Author observation | Binary notes |
|------|--------------------|--------------|
| 0 | Fully opaque, alpha off | Default solids |
| 1 | Cutout: alpha → 0 or 255 | Leaves, flowers, grass (All/LeavesQuality interact) |
| 2 | True translucent alpha | Ice (asset) |
| 3 | Override faces w/ water **still** anim | Water material row (id 5) |
| 4 | Override w/ water **flowing** anim | Not in shipping rows; allowed by clamp |
| 5 | Override w/ lava still | Allowed by clamp |
| 6 | Override w/ lava flowing | Allowed by clamp |

- Inspected in **NeedFace** paths as `prop02` (opacity / return 2 paths) — **CONFIRMED** consumer.
- Full GL blend path for 3–6: **STRONG** (author + clamp + water row MBI=3); not every branch named in recovery.
- **Not** the world liquid storage (world liquid is type 0 + state bits).

## SAU (Sand Action Unit) — `+0x05`

- **Author:** gravity / falling sand-like blocks (yes/no).
- **Asset:** only **Sand (24)** and **Gravel (25)** set to 1.
- Loader: `!=0 → 1`.
- Dedicated “falling block” tick symbol not fully recovered; flag storage and exclusive use on sand/gravel make **STRONG** support for gravity-sand semantics.
- **Status:** **STRONG** (semantics); exact update function still open in RE.

## Col (Collision) — `+0x06`

- **Author:** player collision on/off.
- **CONFIRMED:** solids vs non-solids (flowers Col=0, stone Col=1). Used with class in collision / occlusion heuristics; air/liquid type 0 separate.

## All (All-faces) — `+0x07`

- **Author:** whether faces against **same type** depend on **LeavesQuality**.
- **Asset:** leaves and similar All=1.
- **CONFIRMED** interaction: LeavesQuality (`GridData+0xec`) gates leaves path in **AddMesh**, not inside NeedFace itself (prior RE).

## UVR (UV Rotation) — `+0x08`

- **Author:** face/UV orientation depends on player look at place time; concrete enum unknown.
- **Asset values:** 0, 1, 3, 7  
  - **1:** furnace, chest, crafting table, TV, box (horizontal facing)  
  - **3:** bed  
  - **7:** lying logs / hay (axis = place orientation)
- `ConfigureState` @ `0x997f0` compares material-related `+0x08` / value `0x08` while building state from look vector — **STRONG** link UVR ↔ place-time state orientation.
- Exact bit decoding of 1/3/7: **HYPOTHESIS** (matches axis/mode groups, not fully enumerated in decomp notes).

## XT YT ZT — `+0x09..+0x0b`

- Present in format and loader.
- **All zeros** in shipping `blocks.txt`.
- No recovered consumer in NeedFace / IsFlowAllowed / LoadMaterials post-process.
- **Status:** **UNKNOWN** (reserved / unused in this build, or rare path not yet xref’d). Do not invent meaning.

## W / H (Width / Height of selection-collision) — `+0x18..+0x19`

- **Author:** hitbox width+depth (W) and height (H); relevant when BH=0 or BMT∈{1,4,8}.
- Loader: **clamp 0..16** (1/16 units).
- **Asset:** cross plants W/H 6×7..10×12; sugar cane 16×16; small lamp 4×2.
- `GetCellSelectionBox` @ `0x8ee70` / `GetCellCollisionBoxArray` @ `0x8fc20` branch on **BMT** then geometry; full-box fallback vs class-specific boxes.
- **STRONG:** W/H are the reduced AABB for non-full models; full BMT0 cubes ignore and use full cell / BH height.

## L (Light) — `+0x1a`

- **Author:**  
  - **≤ −1:** attenuate skylight through block (relevant when MBI≠0)  
  - **0:** neutral  
  - **1..15:** emit block-light level  
- **Asset:**  
  - −2: water row, ice  
  - −1: leaves  
  - 13–15: active lamp / TV / furnace / small lamp  
  - 14: active torch  
- Stored as u8 (e.g. −1 → 0xFF, −2 → 0xFE).
- Emission path ties to `AddLight` / torch-style updates (**STRONG** for positive L).
- Negative L as sky attenuation for alpha blocks: **STRONG** from asset + lighting model; exact instruction site still open.

## FTL / FTW — `+0x1b` / `+0x1c` (and FlowType index)

**Important naming correction (CONFIRMED from binary):**

These are **not** “Flow Type Lava / Flow Type Water” as separate liquid systems.  
They are **flow-permission bytes** for `GridData::IsFlowAllowed*(cell, FlowType)`:

```text
perm = materials[type][+0x1b + FlowType]
FlowType: XP=0, XN=1, YP=2, YN=3, ZP=4, ZN=5
```

File only has two columns:

| Column | Offset | Typical role |
|--------|--------|----------------|
| FTL | `+0x1b` | FlowType 0 (XP) / primary |
| FTW | `+0x1c` | FlowType 1 (XN) |

Further directions read `+0x1d..` if filled; shipping load leaves them 0 unless expanded by a loader path.

### Permission bit meaning (IsFlowAllowed* decomp, CONFIRMED)

| Bits | Meaning |
|------|---------|
| bit0 = 1 | Allow flow → return `0x0F` (unless bit3 special) |
| bit0 = 0, bit1 = 1 | Block flow → 0 |
| bit0 = 0, bit1 = 0, bit2 = 1 | Conditional on `state&3==3` |
| bit3 = 1 (+bit0) | Special by BMT/`prop01` ∈ {0,6,7,0x0A} + state |

### Values in asset

| Value | Use |
|------|-----|
| 0 | Solid — no liquid through |
| 1 | Passable (air, glass, leaves, water row, plants) |
| 9 | Partial geometry (slabs/stairs/doors/beds) — allow + special |
| 2–4 | Rare actives (torch/lamp/TV…) |

Used by water/lava **spread/mark** gates and liquid meshing mutual cull — **not** “lava field vs water field” labels.

---

## World cell vs material row (reminder)

| Concept | Storage |
|---------|---------|
| Block type | `cellTypes[i] = Index` |
| Orientation / door / torch / liquid | `cellStates[i]` bits |
| Liquid volume | **type 0 + state** (`0x2000` water, `0x03C0` lava, level `0x1C00`, schedule `0x4000`) |
| Material id 5 “Water” row | UV/MBI/FTL only; **not** written as world water by gen |

---

## Loader defaults (`Material::Init`)

| Offset | Default |
|--------|---------|
| `+0x00` | `0x10` |
| `+0x04` | `0x07` |
| `+0x06` Col | 1 |
| `+0x20` S-Index | `0xFFFF` |
| FTL/FTW | 0 |

---

## Open RE items

1. **XT YT ZT** — no consumer found; remain documented as unknown.
2. **SAU** — exact falling-sand tick function.
3. **UVR** enum 1/3/7 — full state bit layout in `ConfigureState` / `AddBlockFace`.
4. **MBI 3–6** — full sampler/animation bind points.
5. **L &lt; 0** — precise sky attenuation instruction in `UpdateSunRays*`.
6. **NeedFace** complete class jump for returns 2/3.

---

## References (binary)

| Addr | Symbol |
|------|--------|
| 0x987d0 | `GridData::LoadMaterials` |
| 0x98640 | `Material::Init` |
| 0x851a0–0x86370 | `IsFlowAllowed*` |
| 0x8ee70 | `GetCellSelectionBox` |
| 0x8fc20 | `GetCellCollisionBoxArray` |
| 0x997f0 | `ConfigureState` |
| 0x99720 | `SetLiquidState` |
| 0x833d0+ | `NeedFace*` |
| 0x877a0 | `AddBlockFace` |
| 0x9b090 | `Region::AddMesh` |

---

## Deep dive: XT / YT / ZT (2026-11-23)

### Load path (CONFIRMED — ghidra C `LoadMaterials`)

After `ParseString` returns 29 fields, material bytes are written:

```c
pbVar6[8]  = /* UVR */;
pbVar6[9]  = /* XT */;   // local from parse
pbVar6[10] = /* YT */;
pbVar6[11] = /* ZT */;
```

Offsets **+0x09 / +0x0a / +0x0b** are first-class columns in the 34-byte `Material` record.

### Consumer search (x86 `libExplorationLite.so`)

Searched:

1. Ghidra decompiled C (`libExplorationLite_x86.so.c`, ~4MB) for any read of material base `*0x22 + *(this+0xe8)` followed by `[9]`, `[10]`, `[11]`.
2. Full `by_addr` disasm dumps for `movzx …, byte [reg+0x09]` / `+0x0a` / `+0x0b` where `reg` was recently derived from `+0xe8` (material table).
3. `AddBlockFace`, `NeedFace*`, `ConfigureState`, `GetCellSelectionBox`, `IsFlowAllowed*`, liquid spread — no use of these three bytes.

**Result: zero read sites in this binary.**

### Interpretation

| Claim | Status |
|-------|--------|
| Fields exist in file format and are stored in `Material` | **CONFIRMED** |
| Used at runtime by gameplay/mesh/flow in this `.so` | **Not found — effectively dead storage** |
| Possible editor / future / other-arch / stripped path | **HYPOTHESIS only** |

Shipping `blocks.txt` sets **all XT=YT=ZT=0**, which is consistent with unused fields (no content to drive behavior).

**Practical note for RE/reimplementation:** preserve load/store of the three bytes for format fidelity; do not invent gameplay until a read site appears (other binary build, toolchain, or missed thunk).

---

## Deep dive: FTL / FTW values (2026-11-23)

### Indexing (CONFIRMED)

```c
// IsFlowAllowedXP @ 0x851a0 (same pattern all axes)
perm = materials[type][FlowType + 0x1b];
// FlowType argument: 0=XP … 5=ZN
// File column FTL → +0x1b, FTW → +0x1c
```

Return value is a **permission mask** used by liquid code (commonly tested as allow if low bits imply pass): observed returns **0**, **0x0F**, and special **0x0C** (12).

### Bit algorithm (CONFIRMED — ghidra C `IsFlowAllowedXP`)

```text
perm = mat[0x1b + FlowType]

if (perm & 1) == 0:                    // bit0 clear
    if (perm & 2) != 0:                // bit1 set
        return 0                       // hard block  (actually: bit1 alone → Y uses sbb path; XP: fall through)
    // XP/ZN-style:
    if (perm & 4) != 0:                // bit2 set
        return (state & 3) == 3 ? 0x0F : 0   // conditional on low orientation bits
    return 0

else:                                  // bit0 set → default allow 0x0F
    if (perm & 8) == 0:
        return 0x0F                    // plain allow
    // bit3 set → class-special (mat[+0x01] = BMT):
    switch (BMT):
      0 (solid):
        if height(+0x00) > 7:          // slabs/beds etc.
          return (state&4)==0 ? 0x0C : 0x0F
          // i.e. (-(state&4==0)&~7)+0x0C
        else return 0x0F
      6 (stairs):
        return rodata_table6[state & 7]
      7 (door):
        return rodata_table7[state & 0xF]
      10 (trapdoor):
        if (state & 8): return rodata_table10[state & 3]
        else return 0x0F
      other: return 0x0F
```

Y-axis variants (`IsFlowAllowedYP/YN`) invert some bit1 logic via `sbb`/`not` but the same perm bits apply.

### Numeric values seen in shipping asset → meaning

| Value | Bits | Meaning (CONFIRMED) | Typical blocks |
|------:|------|---------------------|----------------|
| **0** | `0000` | No flow | Stone, dirt, full solids |
| **1** | `0001` | Always allow (no special) | Air, glass, leaves, water row, plants |
| **2** | `0010` | bit1 — treat as **blocked** on the “allow?” path (bit0=0) | Active Lamp FTL=2 |
| **3** | `0011` | bit0+bit1 — **bit0 wins** → allow 0x0F (bit1 ignored once allow) | Torch / small lamp FTL=3, FTW=1 |
| **4** | `0100` | bit2 only — allow **only if** `(state&3)==3` | Active TV / Furnace FTL=4 |
| **9** | `1001` | bit0+bit3 — allow + **BMT special tables** | Slabs, stairs, doors, beds, plates, snow layer |

Values **5–8, 10–255** are not in the shipping table but are valid `u8` bytes; they would be interpreted purely by the bit tests above (e.g. 5=`0101` → allow without special; 8=`1000` → bit3 without bit0 → falls into “bit0 clear” branch with bit3 ignored for the primary allow test → effectively block unless bit2…).

### Asymmetry FTL vs FTW (asset)

Some blocks intentionally differ by direction:

| Example | FTL | FTW | Effect |
|---------|-----|-----|--------|
| Wood/Iron Door 1 | 1 | 9 | XP always open; XN uses door special table |
| Trapdoor | 1 | 9 | same idea |
| Torch | 3 | 1 | both allow; FTL has extra bit1 (no effect once bit0 set) |
| Active TV/Furnace | 4 | 0 | XP conditional on state; XN fully blocked |

Because only two columns exist, **YP/YN/ZP/ZN** read `mat[+0x1d..]` which stay **0** after stock `LoadMaterials` unless another path fills them → vertical flow permission defaults to **block** from material bytes; liquid code also uses separate empty/liquid tests in `SpreadWater`.

### SAU cross-note (from same ConfigureState pass)

`ConfigureState` sets `*param_6 = mat[+0x05]` (SAU). If SAU≠0 **and** type ∈ {0x18,0x19} (Sand/Gravel), it further requires empty support cell below or clears the flag — **CONFIRMED** gravity-place gating for SAU blocks.

---

## Updated open items

1. XT/YT/ZT — **no runtime reader in this .so**; keep as stored format fields.
2. Rodata tables for stairs/door/trapdoor flow (`unaff_EBX+0xa7c88` etc.) — contents not yet dumped.
3. Exact call-site use of return values `0x0C` vs `0x0F` inside `UpdateOneWaterState` / `Spread*`.
