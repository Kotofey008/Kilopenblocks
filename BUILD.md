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

## GitHub Actions

Workflow: `.github/workflows/android.yml`

Signing uses **repository secrets** (stable signature across builds).  
If secrets are missing (e.g. PR from a fork), an ephemeral debug keystore is generated instead.

### Required secrets

| Secret | Description |
|--------|-------------|
| `ANDROID_KEYSTORE_BASE64` | Base64 of the keystore file (JKS or PKCS12) |
| `ANDROID_KEYSTORE_PASSWORD` | Keystore password |
| `ANDROID_KEY_ALIAS` | Key alias inside the keystore |
| `ANDROID_KEY_PASSWORD` | Key password (often same as store password) |

### How to prepare secrets from an existing keystore

**If you already have a `.jks` / `.keystore` / `.p12`:**

```bash
# Linux / macOS (no newlines — important)
base64 -w0 my.keystore > keystore.b64
# or:  base64 my.keystore | tr -d '\n' > keystore.b64

# Windows (PowerShell)
[Convert]::ToBase64String([IO.File]::ReadAllBytes("my.keystore")) | Set-Content -NoNewline keystore.b64
```

Then in GitHub: **Repo → Settings → Secrets and variables → Actions → New repository secret**

- Name: `ANDROID_KEYSTORE_BASE64` → paste **entire contents** of `keystore.b64`
- Name: `ANDROID_KEYSTORE_PASSWORD` → store password
- Name: `ANDROID_KEY_ALIAS` → alias (check with `keytool -list -keystore my.keystore`)
- Name: `ANDROID_KEY_PASSWORD` → key password

**If you only have `.pk8` + `.x509.pem` (AOSP-style):**

```bash
# 1. Build PKCS12
openssl pkcs12 -export \
  -in certificate.x509.pem \
  -inkey key.pk8 \
  -name myalias \
  -out keystore.p12 \
  -password pass:YOUR_PASSWORD

# 2. (optional) Convert to JKS
keytool -importkeystore \
  -srckeystore keystore.p12 -srcstoretype PKCS12 \
  -destkeystore keystore.jks -deststoretype JKS \
  -srcstorepass YOUR_PASSWORD -deststorepass YOUR_PASSWORD

# 3. Base64 as above
base64 -w0 keystore.p12 > keystore.b64   # or keystore.jks
```

`apksigner` accepts both JKS and PKCS12.

### Check alias / passwords

```bash
keytool -list -v -keystore my.keystore
```

## Package ID
`com.explorationbase.ExplorationLite` — same as original.

## Entry point
`android_main` in `libExplorationLite.so` via `NativeActivity` + `android.app.lib_name`.
