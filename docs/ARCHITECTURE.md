# Architecture Comparison: Original vs Current

## Original (from symbols + limited disassembly)

### Core engine
- `android_main` constructs `GLApplication` (derives `ApplicationBase`).
- `ApplicationBase::AndroidMain` owns the native activity event loop, EGL context lifecycle, input polling (`ProcessInput`), and calls into game free-functions / virtuals.
- Thin free-function façade: `GameInit`, `GameUpdate` (36 B), `GameRender` (9 B), `GameFree`, `GameOnForeground`/`OnBackground`/`OnAppCheckpointing`. Real work lives in `Game` methods and SceneObject virtuals.

### Scene graph
- `Scene` owns a collection of `SceneObject*`.
- `LoadSceneObjects(path, vector<SceneObject*>&, Properties&, Scene*)` — 5062 B, text-driven factory.
- `NewSceneObject(int)` / `NewSceneObject(char const*)` factory.
- Base `SceneObject` has many virtuals; most base implementations are 9-byte stubs (immediate return). Real behaviour is in derived classes:
  - Camera (full projection + viewport + render-order API)
  - UISceneObject / UI elements (hit-test, touch state, messages)
  - EntitySet / EntityPlayerSet (entity management, local player, HandleMessage, HitTest, Render)
  - Clouds, TextureView, Model, GridModel, HierarchyNode, MeshInstance, AnimationState, etc.
- HierarchyNode + BFSCollectNodes + local/world transforms.
- Message path: `Game::HandleSceneMessage(SceneObject*, int, int, int, void*)` (905 B) + `EntityPlayerSet::HandleMessage` + element-level dispatch. UI files contain string commands that are parsed and routed.

### World / Grid
- `GridData` is a large class (SetCell alone 5910 B). Owns regions (stride ~0xa4 observed), type/state storage, light maps, mesh buffers, water state, entity creation hooks.
- Region size 16 confirmed by alignment arithmetic.
- Chunk encode/decode for save streaming.
- Multiple generators (Earth/Flat/Test) + Noise + TreeBuilder attached via Game.

### Player / Camera / Input
- EntityPlayer is a SceneObject-managed entity.
- Update path: FixedUpdate → UpdateRotation → UpdateCameraTransform (1262 B, uses GetRotationVectors) → AddOrDeleteBlock.
- InputMgr feeds TouchPress/Move/Release; sensitivity from Screen + ApplicationBase tables; EntityPlayerSet::HandleTouch (585 B).

### Other major systems present in binary
- AnimationSet / AnimationState / Model / MeshInstance (skeletal or keyframe animation).
- ENet (full multiplayer stack, ~70 symbols).
- HTTPClient, FileStream/MemoryStream, BBoxTree collision, Font, SkyBox, Clouds, LodePNG, LZ4, zlib.
- Many Game::* UI/update helpers (UpdatePaletteUI 1186 B, UpdateGameListUI 2055 B, etc.).

## Current implementation

### Structure
- Flat `Game` class that owns GridData, Camera, EntityPlayer, InputMgr, UIDocument, SkyBox, Clouds, settings.
- Minimal `SceneObject` / `UISceneObject` / `Scene` stubs (mostly empty virtuals, no hierarchy, no factory, no real LoadSceneObjects).
- `GLApplication` drives OnCreateGLContext → Assets + blockDefs + game.Play + mesh build; Frame does Update + RenderWorld + RenderHUD.
- No free-function GameInit/Update/Render wrappers matching original call sites.
- No HierarchyNode, MeshInstance, AnimationSet, EntitySet as SceneObjects, ENet, HTTP, BBoxTree.

### Lifecycle
- Direct construction in android_main / OnCreateGLContext.
- No scene-file driven object graph; HUD is loaded separately via UIDocument from hud.txt.
- Message dispatch is a simplified `Game::DispatchMessage` string parser, not the original HandleSceneMessage signature or SceneObject routing.

## Can the simplified architecture reach full functional equivalence?

**Short answer: No, not for full equivalence.**

- Core voxel gameplay loop (generate, walk, look, place/break, basic HUD, save a world) can be approximated and is the current focus. Host tests exercise this subset.
- Full equivalence requires:
  - Exact scene loading and object lifetime (start.scene + UI files produce specific SceneObject graphs).
  - Exact message dispatch and UI state machines (menu flow, palette, settings, game list).
  - EntitySet / local-player ownership and HandleMessage paths.
  - Camera as a SceneObject with the full transform update path (not a free-standing Camera attached by hand each frame).
  - Any multiplayer, animation, or advanced collision that the binary contains.
- The current façade can be extended for more UI messages and tighter player/camera behaviour, but treating it as “the original architecture with stubs” is incorrect. It is a parallel, simplified reimplementation of the playable core.

**Recommendation:** Keep the simplified architecture for the playable core while documenting every missing original path. Only reconstruct SceneObject hierarchy pieces when a concrete missing behaviour cannot be expressed through the façade (e.g. exact menu transitions that depend on object identity or properties round-trips).
