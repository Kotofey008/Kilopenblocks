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
// Char codes include Unicode (arrows 0x2190.., 0x21E1, bullet 0x2023) — map not [256].
// Game::LoadFont("Sans", size) → font/font_%d.txt (string @ 0x106c7c)
// Font::GetCharIndex(unsigned short) @ 0x60f30

enum class TextAlign { Left = 0, Center = 1, Right = 2 };

struct Glyph {
    int code = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // normalized 0..1
    float width = 0, height = 0;           // pixels
    float xoff = 0, yoff = 0, xadv = 0;
};

class Font {
public:
    bool LoadFace(const char* faceName, int pixelSize);
    bool Load(const char* metricsPath, const char* texturePath);

    void AddTextToMesh(MeshBuffer& mesh, const Vector& pos, const char* text,
                       float scale, uint32_t color, TextAlign align = TextAlign::Left) const;
    float MeasureWidth(const char* text, float scale) const;
    float LineHeight() const { return lineHeight_; }
    Texture& GetTexture() { return texture_; }
    const Texture& GetTexture() const { return texture_; }
    bool IsLoaded() const { return loaded_; }
    const Glyph* FindGlyph(unsigned code) const;

private:
    std::map<int, Glyph> glyphs_;
    Texture texture_;
    float lineHeight_ = 16.f;
    float cellHeight_ = 16.f;
    int texW_ = 1, texH_ = 1;
    bool loaded_ = false;
    std::string faceName_;
};

class FontResource {
public:
    Font* FindOrLoad(const char* faceName, int pixelSize);
    Font* Find(const char* faceName) const;
    // After EGL context recreate: drop GL ids and re-upload from CPU pixels.
    void InvalidateGL();
    void EnsureAllUploaded();
private:
    std::map<std::string, Font> fonts_;
};
