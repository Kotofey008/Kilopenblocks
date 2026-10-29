# Exploration Lite 1.3.3 — Reverse-Engineered Reimplementation

Native C++ reimplementation of **Exploration Lite 1.3.3** (`com.explorationbase.ExplorationLite`) for Android, recovered from the original `libExplorationLite.so` with Rizin.

Original version string: `Exploration Lite 1.3.3 (c) Andrzej Chomiak`

## Status

Playable source complete; host-compile clean for non-Android modules.  
Build the APK with NDK (local or GitHub Actions).

## Features recovered / implemented

| System | Notes |
|--------|-------|
| BlockDef | 113 blocks from `assets/scenes/blocks.txt` |
| GridData | type+state cells, region 16, EncodeWorldPos |
| Noise / Terrain / Trees | Earth generator, LCG 0x343FD, oak/birch |
| Mesh | PCT1Vertex stride 24, greedy mesher |
| Texture | blocks.png 256² atlas, per-face UVs |
| Lighting | sky+block nibble pack, torch emit 14, BFS |
| Clouds / SkyBox | scroll plane, sun/moon, time-of-day |
| Font / HUD | selected block + time overlay |
| Entity / Player / Camera / Input | move, look, dig/place |
| Water | fall + horizontal spread tick |
| Save / Load | ELW1 world format |
| Entry | `android_main` → NativeActivity |

## Build

### GitHub Actions
Push to `main` or run the **Build ExplorationLite APK** workflow. Artifact: `ExplorationLite-reimpl-debug.apk`.

### Local
```bash
# requires NDK r21+ and Android SDK platform 19
ndk-build -C jni NDK_PROJECT_PATH=. \
  APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk
# then package — see BUILD.md
```

## Layout

```
include/   public headers
src/       implementation
jni/       Android.mk, Application.mk
assets/    scenes, textures, font, ui (from original APK)
res/       Android resources
docs/      PROGRESS.md, analysis notes
```

## Controls (touch)

| Input | Action |
|-------|--------|
| Left half drag | Move |
| Right half drag | Look |
| Short tap | Break block |
| Long press | Place selected block |
| Palette messages | `Game SelectPaletteItem N` |

## License / credit

Reverse-engineering for interoperability and preservation.  
Original game © Andrzej Chomiak.
