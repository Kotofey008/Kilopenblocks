#include "Font.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

bool Font::Load(const char* metricsPath, const char* texturePath) {
    glyphs_.assign(256, Glyph());
    // Default monospace fallback
    for (int i = 0; i < 256; ++i) {
        glyphs_[i].code = i;
        glyphs_[i].width = glyphs_[i].height = 8;
        glyphs_[i].xadv = 8;
        // 16x16 atlas assumption for simple fonts
        int col = i % 16, row = i / 16;
        float t = 1.f / 16.f;
        glyphs_[i].u0 = col * t; glyphs_[i].v0 = row * t;
        glyphs_[i].u1 = glyphs_[i].u0 + t; glyphs_[i].v1 = glyphs_[i].v0 + t;
    }
    // Optional metrics file (font_N.txt) — lines: code x y w h xoff yoff xadv
    if (metricsPath) {
        std::ifstream f(metricsPath);
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            int code = 0, x = 0, y = 0, w = 8, h = 8;
            float xoff = 0, yoff = 0, xadv = 8;
            if (std::sscanf(line.c_str(), "%d %d %d %d %d %f %f %f",
                            &code, &x, &y, &w, &h, &xoff, &yoff, &xadv) >= 5) {
                if (code < 0 || code > 255) continue;
                Glyph& g = glyphs_[code];
                g.code = code; g.width = (float)w; g.height = (float)h;
                g.xoff = xoff; g.yoff = yoff; g.xadv = xadv > 0 ? xadv : (float)w;
                // pixel coords → UV if texture known later
            }
        }
    }
    if (texturePath)
        texture_.LoadPNG(texturePath);
    return true;
}

float Font::MeasureWidth(const char* text, float scale) const {
    float w = 0;
    for (const char* p = text; p && *p; ++p) {
        unsigned char c = (unsigned char)*p;
        w += glyphs_[c].xadv * scale;
    }
    return w;
}

void Font::AddTextToMesh(MeshBuffer& mesh, const Vector& pos, const char* text,
                         float scale, uint32_t color, TextAlign align) {
    if (!text) return;
    float x = pos.x, y = pos.y, z = pos.z;
    if (align != TextAlign::Left) {
        float tw = MeasureWidth(text, scale);
        if (align == TextAlign::Center) x -= tw * 0.5f;
        else if (align == TextAlign::Right) x -= tw;
    }
    for (const char* p = text; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        const Glyph& g = glyphs_[c];
        float x0 = x + g.xoff * scale;
        float y0 = y + g.yoff * scale;
        float x1 = x0 + g.width * scale;
        float y1 = y0 + g.height * scale;
        PCT1Vertex v[4] = {
            {x0, y0, z, color, g.u0, g.v1},
            {x0, y1, z, color, g.u0, g.v0},
            {x1, y1, z, color, g.u1, g.v0},
            {x1, y0, z, color, g.u1, g.v1},
        };
        mesh.PushQuad(v);
        x += g.xadv * scale;
    }
}
