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
