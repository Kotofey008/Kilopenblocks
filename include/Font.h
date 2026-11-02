#pragma once
#include "Mesh.h"
#include "Texture.h"
#include "Vector.h"
#include <string>
#include <vector>
#include <cstdint>
#include <map>

// Font::Init / FontResource::Load — assets/font/font_N.txt + font_N.png
// Header: #FaceName CharHeight CellHeight NumChars TexName TexWidth TexHeight
// Glyph:  CharCode(hex) A B C U0 V0 U1 V1
//   A ≈ xoff, (U1-U0)=width, (V1-V0)=cell height, B≈advance related
// Game::LoadFont("Sans", size) → font/font_%d.txt (string @ 0x106c7c)

enum class TextAlign { Left = 0, Center = 1, Right = 2 };

struct Glyph {
    int code = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // normalized 0..1
    float width = 0, height = 0;           // pixels
    float xoff = 0, yoff = 0, xadv = 0;
};

class Font {
public:
    // Load face by design name + pixel size, e.g. ("Sans", 16) → font/font_16.txt
    bool LoadFace(const char* faceName, int pixelSize);
    bool Load(const char* metricsPath, const char* texturePath); // direct paths

    void AddTextToMesh(MeshBuffer& mesh, const Vector& pos, const char* text,
                       float scale, uint32_t color, TextAlign align = TextAlign::Left) const;
    float MeasureWidth(const char* text, float scale) const;
    float LineHeight() const { return lineHeight_; }
    Texture& GetTexture() { return texture_; }
    const Texture& GetTexture() const { return texture_; }
    bool IsLoaded() const { return loaded_; }

private:
    Glyph glyphs_[256];
    Texture texture_;
    float lineHeight_ = 16.f;
    float cellHeight_ = 16.f;
    int texW_ = 1, texH_ = 1;
    bool loaded_ = false;
    std::string faceName_;
};

// FontResource — caches Font by "Sans@16" key (FindFont @ 0x61bb0)
class FontResource {
public:
    Font* FindOrLoad(const char* faceName, int pixelSize);
    Font* Find(const char* faceName) const;
private:
    std::map<std::string, Font> fonts_;
};
