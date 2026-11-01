# TODO

## Done this session
- Material table allocation (+0xe0/+0xe8), 34B layout, LoadMaterials←blocks.txt
- Key fields: prop01, prop02, waterBase, uFace/vFace, flowPerm
- GetLightColor mode origin = GetLight face-arg OR (0x2000/0x2800 etc.)
- IsFlowAllowed uses material flowPerm when table loaded

## Open
- Full blocks.txt column → Material byte map (BMT/MBI/SAU/FTL/FTW)
- prop02 exact encoding
- NeedFace jump-table cases for return 2 and 3
- GetLight face→mode wired into LightMap::GetFaceLight
- AddBlockFace UV from Material.uFace/vFace (EmitFace still stand-in)
- Prior: BoxLineSegIntersect, FindFirstUsedCell +0x1e, IsHidden, ConfigureState liquid, entity tracers, SetCell neighbour predicates

## Next
Complete column map from LoadMaterials field stores OR wire Material UVs into mesher
