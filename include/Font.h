#pragma once
#include "Mesh.h"
#include "Texture.h"
#include "Vector.h"
#include <string>
#include <vector>
#include <cstdint>

// Font::AddTextToMesh @ 0x611c0 (2233 B)
// Assets: font/font_N.png + font_N.txt metrics, font.csv lists sizes

enum class TextAlign { Left = 0, Center = 1, Right = 2 };

struct Glyph {
    int code = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    float width = 8, height = 8;
    float xoff = 0, yoff = 0, xadv = 8;
};

class Font {
public:
    bool Load(const char* metricsPath, const char* texturePath);
    // Add quads for text into mesh at position, scale, color
    void AddTextToMesh(MeshBuffer& mesh, const Vector& pos, const char* text,
                       float scale, uint32_t color, TextAlign align = TextAlign::Left);
    float MeasureWidth(const char* text, float scale) const;
    Texture& GetTexture() { return texture_; }

private:
    std::vector<Glyph> glyphs_; // indexed by char code 0-255
    Texture texture_;
    float lineHeight_ = 16.f;
};
