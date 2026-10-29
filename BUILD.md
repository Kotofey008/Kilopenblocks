# Building ExplorationLite reimplementation

## Prerequisites
- Android NDK r16+ (r21–r25 recommended)
- Android SDK build-tools + platform android-19 (or higher)
- `ndk-build`, `aapt`, `zipalign`, `apksigner` (or `jarsigner`)

## Quick native library build
```bash
cd ExplorationLite_RE
ndk-build -C jni NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk
# Output: libs/armeabi-v7a/libExplorationLite.so  libs/x86/libExplorationLite.so
```

## Package APK (example)
```bash
# 1. Build native libs (above)
# 2. Stage
mkdir -p build/apk/{lib/armeabi-v7a,lib/x86,assets,res}
cp -r assets/* build/apk/assets/
cp -r res/* build/apk/res/
cp libs/armeabi-v7a/libExplorationLite.so build/apk/lib/armeabi-v7a/
cp libs/x86/libExplorationLite.so build/apk/lib/x86/ 2>/dev/null || true

# 3. Resources + manifest
aapt package -f -M AndroidManifest.xml -S res -A assets -I $ANDROID_HOME/platforms/android-19/android.jar -F build/unsigned.apk
# add native libs into the apk
cd build/apk && zip -u ../unsigned.apk lib/armeabi-v7a/libExplorationLite.so lib/x86/libExplorationLite.so
cd ../..

# 4. Align + sign
zipalign -f 4 build/unsigned.apk build/aligned.apk
apksigner sign --ks your.keystore --out ExplorationLite-reimpl.apk build/aligned.apk
```

## GitHub Actions sketch
```yaml
- uses: nttld/setup-ndk@v1
  with: { ndk-version: r25c }
- run: ndk-build -C ExplorationLite_RE/jni NDK_PROJECT_PATH=ExplorationLite_RE ...
- run: # aapt package + sign as above
```

## Package ID
`com.explorationbase.ExplorationLite` — same as original.

## Entry point
`android_main` in `libExplorationLite.so` via `NativeActivity` + `android.app.lib_name`.
