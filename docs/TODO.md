# TODO

## Recovered this session
- NeedFaceXP template + NeedFace() implementation (opacity rules)
- MinWaterHeight / MaxWaterHeight exact arithmetic
- Light mode bits documented; LightPack::Mode + optional mode in Make
- Confirmed: no greedy merging in binary mesher path

## Still open
- NeedFace material jump table (return 2 path)
- AddBlockFace full geometry
- Light mode assignment sites in AddLight/SetAddLights
- IsFlowAllowed* all 6 directions
- UpdateWaterState level integration
- BoxLineSegIntersect, FindFirstUsedCell +0x1e, IsHidden, ConfigureState liquid, entity tracers, SetCell neighbour predicates

## Next
IsFlowAllowed full recovery → wire heights into UpdateWater → AddLight mode assignment → AddBlockFace UV/light vertex path
