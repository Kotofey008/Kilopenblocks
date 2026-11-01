# NeedFace / AddBlockFace / Light modes / Water flow — recovery notes (2026-11-01)

## 1. NeedFaceXP @ 0x833d0 (1365 B) — template for all NeedFace*

### Algorithm (verified)
```
selfType = types[cellIndex]
mat = materials[selfType]          // +0xe8, stride 34
neighIndex = cellIndex + faceOffset (with dim wrap via +0x44)
neighType = types[neighIndex]
neighMat = materials[neighType]
switch (mat.prop01)  // jump table values 0..0x0a
  cases inspect: mat.prop02 opacity, state&0x03/0x07/0x0f, neighMat props
return 0 (cull) | 1 (emit) | 2 (special/partial)
```

### Material layout (partial)
| Offset | Role |
|--------|------|
| +0x01 | class (0 solid, 6/7 special, 0x0a liquid-like) |
| +0x02 | opacity / flag (0 → deeper checks) |
| +0x03 | water base height |
| +0x1b + FlowType | flow permission bits |

### LeavesQuality
Gates leaves path in **AddMesh**, not inside NeedFace itself.

### Other NeedFace*
Same structure; only neighbour offset axis differs (XP/XN/YP/YN/ZP/ZN).

### Implementation status
`RegionMesher::NeedFace` implements opaque/transparent occlusion rules.
**Missing:** full material jump table (needs binary material table load), return-value 2 path.

### Greedy merging
**Not present** in NeedFace or AddMesh call graph. Each face decided independently then passed to AddBlockFace. Current greedy path is a hypothesis and should not be treated as original behavior.

---

## 2. AddBlockFace @ 0x877a0 (14400 B)

### Signature
`AddBlockFace(int, int, int, Material const&, int face, unsigned char)`

### MeshBuffer::PushQuads @ 0x99e60
```
PushQuads(int, int, int, int, short const* uvsOrOffsets, int, int,
          char const*, unsigned short light, unsigned int color, int)
```
- Vertex layout confirmed PCT1: pos(12) + color(4) + uv(8) = 24 bytes (DrawPrimitive stride 0x18)
- UV from material / short array
- Light as ushort, color as uint
- **No face merging** inside PushQuads — writes one quad

### Status
Geometry body not instruction-matched. Current EmitFace approximates single-quad emission.

---

## 3. Lighting mode bits

### GetLightColor @ 0x947e0
```
A = v & 0x0f; B = (v & 0xf0) >> 4; mode = v & 0x3800
color = table[+0x214][(A<<4)|B]
scale by mode:
  0x0000 / 0x1800 → ×0xA4/256 ≈ 0.641
  0x2800          → ×0xD2/256 ≈ 0.820
  0x1000 / 0x2000 → other branches
```

### SetAddLights @ 0x92cb0
- Args: two ints (region coords)
- Works in 16-aligned blocks
- Flag at +0xf0 gates path
- Seeds lights into region cells

### Mode assignment
**Not yet recovered** which code path writes mode bits into the light value. `LightPack::Make` now accepts optional mode; default 0 until assignment sites found.

### Hypothesis (labeled)
A=sky, B=block remains hypothesis consistent with AddLight 0x0f mask.

---

## 4. Water flow helpers

### IsFlowAllowedXP @ 0x851a0 (281 B)
```
mat = materials[types[cell]]
perm = mat[+0x1b + FlowType]
if (!(perm & 1)) {
  if (perm & 2) return 0
  if (perm & 4) return (state&3)==3 ? 0x0f : 0
}
if (perm & 8) { /* special prop01 0/6/7/0x0a paths */ }
return 0x0f or computed
```

### MinWaterHeight @ 0x86b70
```
if prop03==0: return 0
if prop01==0x0a: return (state&0x0c)<1 ? 2 : 0
if state&0x04: return 0
return prop03
```

### MaxWaterHeight @ 0x86c70
```
if prop03==0: return 0
if prop01==0x0a: return (state&0x0c)<1 ? 0x10 : 0x0e
if !(state&0x04): return 0
return 0x10 - prop03
```

### Implemented
`GridData::MinWaterHeight` / `MaxWaterHeight` match binary arithmetic.
IsFlowAllowed* and UpdateWaterState level integration still open.

---

## Mismatch summary

| Item | Binary | Current | Status |
|------|--------|---------|--------|
| NeedFace return 0/1/2 | jump table | 0/1 via opacity rules | Partial |
| Material table | +0xe8 stride 34 | blocks.txt BMT only | Partial |
| Greedy mesh | **absent** | optional code exists | Incorrect assumption |
| AddBlockFace | 14KB | EmitFace single quad | Partial |
| Light mode | 0x3800 family | optional in Make() | Documented; assignment open |
| Water heights | Min/Max recovered | helpers implemented | Recovered arithmetic |
| IsFlowAllowed | 6 dirs | missing | Missing |
| UpdateWaterState levels | uses heights | binary water only | Partial |
