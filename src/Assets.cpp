#include "Assets.h"
#include <cstring>
#include <cstdio>
#include <android/log.h>

#if defined(__ANDROID__)
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#endif

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "ExplorationLite", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "ExplorationLite", __VA_ARGS__))

#if defined(__ANDROID__)
static AAssetManager* g_mgr = nullptr;
#endif

void Assets::Init(android_app* app) {
#if defined(__ANDROID__)
    g_mgr = (app && app->activity) ? app->activity->assetManager : nullptr;
    LOGI("Assets::Init mgr=%p", (void*)g_mgr);
#else
    (void)app;
#endif
}

bool Assets::Ready() {
#if defined(__ANDROID__)
    return g_mgr != nullptr;
#else
    return true;
#endif
}

bool Assets::ReadAll(const char* path, std::vector<uint8_t>& out) {
    out.clear();
    if (!path) return false;
#if defined(__ANDROID__)
    if (!g_mgr) return false;
    // strip leading "assets/" if present
    const char* p = path;
    if (strncmp(p, "assets/", 7) == 0) p += 7;
    AAsset* asset = AAssetManager_open(g_mgr, p, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Asset open failed: %s", p);
        return false;
    }
    off_t len = AAsset_getLength(asset);
    if (len < 0) { AAsset_close(asset); return false; }
    out.resize((size_t)len);
    int rd = AAsset_read(asset, out.data(), (size_t)len);
    AAsset_close(asset);
    if (rd != (int)len) { out.clear(); return false; }
    LOGI("Asset loaded %s (%d bytes)", p, (int)len);
    return true;
#else
    // host: try path as-is and under assets/
    FILE* f = fopen(path, "rb");
    if (!f) {
        std::string alt = std::string("assets/") + path;
        f = fopen(alt.c_str(), "rb");
    }
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    fread(out.data(), 1, (size_t)n, f);
    fclose(f);
    return true;
#endif
}

bool Assets::ReadText(const char* path, std::string& out) {
    std::vector<uint8_t> buf;
    if (!ReadAll(path, buf)) return false;
    out.assign(reinterpret_cast<char*>(buf.data()), buf.size());
    return true;
}
