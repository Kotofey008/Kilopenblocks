# IsFlowAllowed / Light mode / NeedFace returns (2026-11-01)

## IsFlowAllowed* (all six)

| Symbol | Addr | Size |
|--------|------|------|
| IsFlowAllowedXP | 0x851a0 | 281 |
| IsFlowAllowedYP | 0x852c0 | 180 |
| IsFlowAllowedZP | 0x85380 | 273 |
| IsFlowAllowedXN | 0x854a0 | 281 |
| IsFlowAllowedYN | 0x85cb0 | 182 |
| IsFlowAllowedZN | 0x86370 | 281 |

### Core algorithm (verified, same for all axes)
```
perm = materials[types[cell]][+0x1b + FlowType]
if (perm & 1) {
  if (perm & 8) special(prop01 in {0,6,7,0x0a}, state bits) → 0 or 0x0f
  else return 0x0f
} else {
  if (perm & 2) return 0          // Y axes invert via sbb/not
  if (perm & 4) {
    XP/XN/ZP/ZN: (state&3)==3 ? 0x0f : 0
    YP/YN: alternate sbb path
  }
  return 0
}
```

### Implementation
`GridData::IsFlowAllowed(cellIndex, FlowType)` — type-heuristic until material table loaded.
`UpdateWater` now gates fall/spread on IsFlowAllowed.

### FlowType mapping
`FlowXP=0, FlowXN=1, FlowYP=2, FlowYN=3, FlowZP=4, FlowZN=5` (indexes mat+0x1b).

---

## Light mode bits

### Storage (AddLight / AddLightUpdate)
- Channels: `value & 0x0f`, `value & 0xf0`
- Hierarchy write: `value | 0x8000` at maps `+0x280..`
- **No `or` of 0x1000/0x1800/0x2000/0x2800 found in AddLight body**

### Consumption (GetLightColor)
- `mode = value & 0x3800`
- Scales RGB from table[+0x214]

### Hypothesis (labeled)
Mode may be applied at lookup from external context (time of day), not stored per-cell.
Alternatively assignment is in an unexamined path of SetAddLights / AddLights.
**Status: assignment site still open.** LightPack keeps optional mode for when found.

---

## NeedFace return values (expanded)
| Value | Meaning |
|-------|---------|
| 0 | cull |
| 1 | emit full |
| 2 | special/partial (prop+0x02==0, type mismatch paths) |
| 3 | observed jump-table case (state&7 related) |

Material jump table (prop+0x01 0..0x0a) still required for 2/3.

## Greedy meshing
Isolated under `#if 0` in Mesh.cpp — confirmed non-original.
