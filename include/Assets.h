#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct android_app;

// Android AAssetManager wrapper — APK assets are NOT real files
namespace Assets {
    void Init(android_app* app);
    bool Ready();
    // path relative to assets/ e.g. "textures/blocks.png" or "scenes/blocks.txt"
    bool ReadAll(const char* path, std::vector<uint8_t>& out);
    bool ReadText(const char* path, std::string& out);
}
