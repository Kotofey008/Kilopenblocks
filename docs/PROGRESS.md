# ExplorationLite 1.3.3 Reverse Engineering Progress Log

**Date**: 2026-10-29 (continued)

## This step: HUD overlay + GitHub Actions workflow

### HUD
- Ortho 2D pass after world draw
- Shows **Selected: &lt;block name&gt;** and **Time: 0.xx**
- Uses `Font::AddTextToMesh` + `font_16` assets

### GitHub Actions
`.github/workflows/android.yml`
- NDK r25c + SDK platform 19
- `ndk-build` → aapt package → zipalign → debug sign
- Uploads `ExplorationLite-reimpl-debug.apk` + `.so` artifacts

### README.md
Handoff doc: features table, controls, build instructions.

### Inventory
```
include/: 20 headers
src/:     20 sources
.github/workflows/android.yml
README.md, BUILD.md, AndroidManifest.xml
```

## Project status
**Source-complete for a first playable APK.**  
Remaining unknowns (non-blocking): exact light LUT bytes, original PageCache/LZ4 chunk format, full water level model, JavaApp thin wrapper (NativeActivity equivalent).
