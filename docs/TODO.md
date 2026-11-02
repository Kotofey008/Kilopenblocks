# TODO

## Current focus (user 2026-11-02)
- [x] UpdateHUDUI + selection frame (State[1], frame2, pressed from InputMgr) — 2026-11-02
- [ ] Original-style HUD rendering (UIDocument, hud_*, Font, ortho, hit-test) — partial (styles/selection done)
- [ ] Inventory / palette menu functional (SelectPaletteItem/Page, message routing) — partial (slot/page selected visual)

## Deferred (after HUD + inventory)
- SetCell neighbour SetState |= 0xC000 + this+0xf0 gate
- NeedFace return 2/3 (material jump table via pdf/CFG)
- AddBlockFace (Material UV + light mode; replace EmitFace stand-in)
- UpdateCameraTransform / GetRotationVectors (replace FPS camera)
- IsFlowAllowed special tables (prop01 6/7/10)
- Tree canopy shape, exact Noise, save format identity

## Done (reference)
- Material table allocation (+0xe0/+0xe8), 34B layout, LoadMaterials←blocks.txt
- Key fields: prop01, prop02, waterBase, uFace/vFace, flowPerm
- GetLightColor mode origin = GetLight face-arg OR (0x2000/0x2800 etc.)
- IsFlowAllowed uses material flowPerm when table loaded
- Min/MaxWaterHeight arithmetic Verified from pdg
- Full audit recorded: docs/AUDIT_FULL_2026-11-02.md
- Handoff prompt: docs/HANDOFF_PROMPT.md

## Open (from earlier, not current focus)
- Full blocks.txt column → Material byte map (BMT/MBI/SAU/FTL/FTW)
- prop02 exact encoding
- GetLight face→mode wired into LightMap::GetFaceLight
- Prior: BoxLineSegIntersect, FindFirstUsedCell +0x1e, IsHidden, ConfigureState liquid, entity tracers
