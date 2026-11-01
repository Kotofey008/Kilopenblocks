# Function / Symbol Map (x86 libExplorationLite.so)

## Entry
| Address   | Symbol                                      | Size  | Notes |
|-----------|---------------------------------------------|-------|-------|
| 0x43470   | android_main                                | 115   | constructs GLApplication, calls AndroidMain |
| 0xfd790   | ANativeActivity_onCreate                    | 501   | |
| 0x44400   | ApplicationBase::AndroidMain(android_app*)  | 442   | event loop + EGL swap |
| 0x71d10   | GLApplication::GLApplication()              | 521   | |
| 0x704c0   | GLApplication::OnCreateGLContext()          | 169   | |

## Grid / World
| Address   | Symbol                                      | Size  | Notes |
|-----------|---------------------------------------------|-------|-------|
| 0x77d80   | GridData::SetCell(int,int,int,u8,u16)       | 5910  | core write |
| 0x7c050   | GridData::TracePoint(...)                   | 528   | |
| 0x8ee70   | GridData::GetCellSelectionBox               | 3491  | |
| 0xa1a60   | GridData::ActivateCell                      | 2005  | |
| 0x7b350   | GridData::IsUnderwater                      | 740   | |
| 0x7b090   | GridData::OnLadder                          | 704   | |

## Terrain
| Address   | Symbol                                      | Notes |
|-----------|---------------------------------------------|-------|
| (see dynstr) | TerrainGeneratorEarth::Generate / GenerateTerrainShape / AddTree / AddFlowers / AddGrassAndWater / Slice | |
|           | TerrainGeneratorFlat::Generate              | |
|           | TerrainGeneratorTest::Generate              | |

## Game / UI
| Address   | Symbol                          | Size  | Notes |
|-----------|---------------------------------|-------|-------|
| 0x6c720   | Game::Play(int)                 | 2679  | |
| 0x63680   | Game::Update()                  | 106   | |
| 0x6b500   | Game::InsertGame                | 770   | |
| 0x62d10   | Game::UpdateSkyBox              | 826   | |

## Free functions
- LoadSceneObjects(char const*, vector<SceneObject*>&, Properties&, Scene*)
- DrawCursor(GridData&, int, int, int, unsigned int)

## Third-party (kept as-is)
ENet full API, LodePNG, LZ4, zlib/deflate, Adler32

## GridData layout notes (partial)
- +0x90 : MeshBuffer
- +0xec : LeavesQuality (int, default 2)
- +0xe0.. : related counters / pointers
- Region::Init stores three ints at +8,+0xc,+0x10; +0/+4 = 0x7fffffff
- Chunk / Region system present (CopyChunkToRegions etc.)
- Stride 0xa4 seen in InitRegions loops

## TerrainGeneratorEarth (confirmed)
- Region size = 16 (add 0xf / sar 4 / shl 4 alignment in Generate)
- LCG mul = 0x343FD
- Generate @ 0xec750 → GenerateTerrainShape @ 0xeb2c0 → AddGrassAndWater @ 0xeac30
- AddTree @ 0xec030 calls CheckTreePosition

## Noise system
| Symbol | Addr | Size | Notes |
|--------|------|------|-------|
| Noise2D | 0xbf1d0 | 781 | free fn, uses floorf + table |
| Noise3D | 0xbf4e0 | 1557 | |
| Noise::Noise | 0xbfb00 | 380 | |
| Noise::Init(Random&,int,float,float,float) | 0xbfc80 | 681 | |
| Noise::GetValue(float,float) | 0xbff30 | 243 | |
| Noise::GetValue(float,float,float) | 0xc0030 | 287 | |
| Noise::GetValue(int,int) | 0xc0150 | 251 | |
| Noise::GetValue(int,int,int) | 0xc0250 | 295 | |
| Noise::s_DataPattern | 0x12ef40 | 256 | runtime |
| PeriodicNoise::s_Perm | 0x130520 | 512 | runtime |

## Save / Stream
| Symbol | Addr | Size |
|--------|------|------|
| FileStream::Open | 0xe94b0 | 232 |
| FileStream::Serialize | 0xe9140 | 257 |
| Stream::ByteOrderSerialize | 0xe9770 | 98 |
| Game::GetSaveName | 0x65590 | 279 |
| Game::SaveGameHeader | 0x6c030 | 600 |
| Game::LoadGameHeader | 0x6bb50 | 1246 |
| GridData::EncodeWorldPos | 0xa2300 | 47 |
| GridData::DecodeWorldPos | 0xa2330 | — |
| GridData::SaveChunk | 0xa25d0 | 192 |

## Trees
| Symbol | Addr | Size |
|--------|------|------|
| CheckTreePosition | 0xea290 | 895 |
| BuildTree | 0xea610 | 708 |
| TerrainGeneratorEarth::AddTree | 0xec030 | 341 |
| LCG | mul=0x343FD add=0x269EC3 | confirmed in AddTree |

## Entity / Player / Input / Camera
| Symbol | Addr | Size |
|--------|------|------|
| Entity::Move | 0x5a810 | 649 |
| Entity::CheckGround | 0x5a750 | 191 |
| EntityPlayer::FixedUpdate | 0x5b1d0 | 573 |
| EntityPlayer::UpdateCamera | 0x5b580 | 1262 |
| InputMgr::TouchPressEvent | 0xb1920 | 328 |
| Camera::SetPerspective | 0x506a0 | 45 |

## Updated 2026-10-31 (deeper audit)

### Free functions / game façade
| Address  | Symbol | Size | Notes |
|----------|--------|------|-------|
| 0x69c40  | GameInit | 36 | thin |
| 0x679a0  | GameUpdate | 36 | thin |
| 0x679d0  | GameRender | 9 | thin |
| 0x68990  | GameFree | 36 | |
| 0xc4ab0  | LoadSceneObjects(...) | 5062 | text factory — major missing |
| 0xc3e60  | NewSceneObject(int) | 75 | |
| 0xc3eb0  | NewSceneObject(char const*) | 93 | |
| 0xb7190  | GetRotationVectors(Vector&,Vector&,Vector&,float,float) | 501 | used by camera |

### SceneObject (selected)
| Address  | Symbol | Size | Notes |
|----------|--------|------|-------|
| 0xd5e10  | SceneObject::SceneObject | 188 | |
| 0xd55a0  | SceneObject::HandleMessage | 29 | |
| 0xd55c0–0xd56c0 | many virtuals | 9 | base stubs |
| 0xd5af0  | SceneObject::UpdateTouchState | 633 | |
| 0x675d0  | Game::HandleSceneMessage | 905 | |

### Entity / player (selected)
| Address  | Symbol | Size | Notes |
|----------|--------|------|-------|
| 0x5b1d0  | EntityPlayer::FixedUpdate | 573 | |
| 0x5b4a0  | EntityPlayer::UpdateRotation | 214 | |
| 0x5b580  | EntityPlayer::UpdateCameraTransform | 1262 | critical |
| 0x5cfb0  | EntityPlayer::AddOrDeleteBlock | (prior) | |
| 0x5e260  | EntityPlayerSet::HandleTouch | 585 | |
| 0x5dbd0  | EntityPlayerSet::HandleMessage | 275 | |

### GridData (selected additional)
| Address  | Symbol | Size | Notes |
|----------|--------|------|-------|
| 0x81fb0  | GridData::Resize | 2583 | missing |
| 0xa3770  | GridData::Store | 358 | |
| 0xa4d50  | GridData::Restore | 1452 | |
| 0xa3620  | GridData::Sync | 321 | |
| 0x7c4b0  | GridData::CreateEntity | 163 | |

### Input / sensitivity
| Address  | Symbol | Size | Notes |
|----------|--------|------|-------|
| 0x43a80  | ApplicationBase::GetDefaultTouchSensitivity | 49 | table by ScreenSizeEnum |
| 0xd61f0  | Screen::GetTouchSensitivity | 22 | |
| 0xd6210  | Screen::SetTouchSensitivity | 24 | |

### Other large / uninvestigated
- AnimationSet::* (many)
- ENet full API (~70 symbols)
- HierarchyNode, MeshInstance, Model
- Game::UpdatePaletteUI 1186 B, UpdateGameListUI 2055 B
- DrawPrimitive PCT1 / PsCTb1 variants

**Note:** Base SceneObject methods of size 9 are immediate returns; behaviour lives in overrides that current code does not implement.

## SetCell recovery notes (2026-10-31)
- 0x77d80 size 5910, complexity 73, 122 BBs, 24 loops
- Calls only SetState (0x77380, 617 B) + PIC thunk (many times)
- Fields used: +0x44,+0x48,+0x50 dims; +0x74 types; +0x78 states; +0x64,+0x68,+0x80 region; +0xf0 flag; +0x230,+0x258,+0x260,+0x268,+0x270,+0x278,+0x280,+0x288 aux maps
- State bits: 0x2000, 0x3c0, 0x4000, mask 0xbfff
- Region stride 0xa4 confirmed in region address calc
- Early-out when type unchanged (jz 0x78ae8)
- Full neighbour/propagation logic still open

## Trace / interaction path (2026-10-31)
| Address  | Symbol | Size | Notes |
|----------|--------|------|-------|
| 0x7c050  | GridData::TracePoint(Vector,Vector,GridDataTraceResult&,Entity*) | 528 | dual path |
| 0x79dd0  | GridData::FindFirstUsedCell(Vector,Vector,float&,int&) | 2541 | main DDA |
| 0xa3340  | GridData::IsHidden(int,int) | 432 | leaves filter |
| 0x99240  | GridData::GetAddingPosition(...) | 743 | face + material |
| 0x99530  | GridData::IsPossibleToAdd(...) | 496 | |
| 0x997f0  | GridData::ConfigureState(...) | 765 | place state |
| 0x99af0  | GridData::ConfigureAirState(...) | 99 | break |
| 0x5cfb0  | EntityPlayer::AddOrDeleteBlock | 722 | orchestrator |
| 0x5cf90  | EntityPlayer::TracePoint (thin) | — | calls GridData::TracePoint |

TraceResult layout: +0x00 t, +0x04 hitZ, +0x08 hitY, +0x0c hitX, +0x10 cellIndex, +0x14 face, +0x18/1c/20 cellXYZ, +0x24 0

## FindFirstUsedCell / ConfigureState notes (2026-10-31)
- FindFirstUsedCell: AABB from int bounds → BoxLineSegIntersect(0xb3520, 2269B) → floor entry → clamp → DDA with region*0xa4+0x1e flag + types[idx]!=0
- IsHidden: region metadata comparisons, 0xff sentinel; semantics open
- ConfigureState: material stride 34 at +0xe8; orient state 0-3; calls GetDirectionMarker, SetLiquidState, FullHeightBlockVariant
- GetDirectionMarker @ 0x99060 (466B), SetLiquidState @ 0x99720 (202B), FullHeightBlockVariant @ 0x98f30 (294B)

## SetState / SetCell aux maps (2026-10-31)
| Offset | Role (recovered) |
|--------|------------------|
| +0x74  | types (byte*) |
| +0x78  | states (word*) |
| +0x7c  | related array (seen in SetCell epilogue) |
| +0x80  | regions base (stride 0xa4); dirty at +0x1f |
| +0x230 | word aux (combined state mirror) |
| +0x258 | word aux (full newState mirror) |
| +0x260/+0x268/+0x270/+0x278 | hierarchical byte counts (>>16/12/8/4) for 0x4000 |
| +0x280/+0x288/+0x290/+0x298 | additional hierarchy family |
| +0x66d/+0x74d/+0x82d | per-axis dirty markers |

State bits: 0x2000, 0x03C0, 0x4000; mask 0xBFFF.
SetState calls from SetCell: 14 sites (neighbours under fluid/light conditions — exact predicates open).
Related: AddLight 0x95b90, AddLightUpdate 0x94b60, SetAddLights 0x92cb0, UpdateWaterState 0xa1040, MarkModifiedRegions 0x7aad0.

## Mesher / Lighting / Water (2026-11-01)
| Addr | Symbol | Size |
|------|--------|------|
| 0x9b090 | Region::AddMesh | 3164 |
| 0x833d0–0x84c40 | NeedFaceXP/YP/ZP/XN/YN/ZN | 1081–1369 |
| 0x877a0 | AddBlockFace | 14400 |
| 0x8cc70 | AddWaterFace | 2771 |
| 0x8d750 | AddCross | 949 |
| 0x8db10 | AddTorch | 305 |
| 0x8dc50 | AddLadder | 1872 |
| 0x94480 | GetLight | 860 |
| 0x947e0 | GetLightColor | 318 |
| 0x94920 | UpdateLightMap | 566 |
| 0x92cb0 | SetAddLights | 1837 |
| 0x94b60 | AddLightUpdate | 4132 |
| 0x95b90 | AddLight | 2977 |
| 0x96740 | AddLights | 1963 |
| 0xa1040 | UpdateWaterState | 2513 |
| 0x851a0–0x86370 | IsFlowAllowed* | 180–281 |
