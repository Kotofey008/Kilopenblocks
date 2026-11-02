#include "Font.h"
#include "Assets.h"
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <algorithm>

bool Font::LoadFace(const char* faceName, int pixelSize) {
    if (!faceName || pixelSize <= 0) return false;
    // Binary: sprintf path "font_%d.txt" under font/
    char metrics[64], texPath[64];
    std::snprintf(metrics, sizeof(metrics), "font/font_%d.txt", pixelSize);
    // TexName is inside the metrics file; default guess:
    std::snprintf(texPath, sizeof(texPath), "font/font_%d.png", pixelSize);
    faceName_ = faceName;
    return Load(metrics, texPath);
}

bool Font::Load(const char* metricsPath, const char* texturePath) {
    loaded_ = false;
    for (int i = 0; i < 256; ++i) glyphs_[i] = Glyph{};

    std::string text;
    if (!Assets::ReadText(metricsPath, text)) return false;

    std::istringstream in(text);
    std::string line;
    bool headerDone = false;
    std::string texName;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (!line.empty() && (unsigned char)line[0] == 0xEF && line.size() >= 3)
            line = line.substr(3);
        // skip pure comment headers
        if (line[0] == '#' && line.find("FaceName") != std::string::npos) continue;
        if (line[0] == '#' && line.find("CharCode") != std::string::npos) continue;

        if (!headerDone) {
            // Sans 16 19 120 font_16.png 256 128
            char face[64] = {}, tex[64] = {};
            int chH = 0, cellH = 0, num = 0, tw = 0, th = 0;
            if (std::sscanf(line.c_str(), "%63s %d %d %d %63s %d %d",
                            face, &chH, &cellH, &num, tex, &tw, &th) >= 5) {
                lineHeight_ = (float)chH;
                cellHeight_ = (float)(cellH > 0 ? cellH : chH);
                texW_ = tw > 0 ? tw : 1;
                texH_ = th > 0 ? th : 1;
                texName = tex;
                headerDone = true;
            }
            continue;
        }

        // Glyph: hexCode A B C U0 V0 U1 V1
        unsigned int code = 0;
        int A = 0, B = 0, C = 0, U0 = 0, V0 = 0, U1 = 0, V1 = 0;
        if (std::sscanf(line.c_str(), "%x %d %d %d %d %d %d %d",
                        &code, &A, &B, &C, &U0, &V0, &U1, &V1) < 8)
            continue;
        if (code > 255) continue;
        Glyph& g = glyphs_[code];
        g.code = (int)code;
        g.xoff = (float)A;
        // B appears to be advance-related width; C is side bearing
        g.width = (float)(U1 - U0);
        g.height = (float)(V1 - V0);
        if (g.width < 1.f) g.width = (float)std::max(B, 1);
        if (g.height < 1.f) g.height = cellHeight_;
        // Advance: B is used as advance in practice for this format
        g.xadv = (float)(B > 0 ? B : (int)g.width);
        if (C != 0) {
            // C sometimes negative kerning-ish; include in advance when positive
        }
        g.xadv = g.width + (float)C; // U span + trailing bearing
        if (g.xadv < 1.f) g.xadv = g.width > 0 ? g.width : 4.f;

        // UV: pixel coords in texture; GL V=0 at bottom of uploaded image
        // PNG decoder stores file-top as row 0 → GL bottom; same as blocks atlas
        g.u0 = (float)U0 / (float)texW_;
        g.u1 = (float)U1 / (float)texW_;
        g.v0 = (float)V0 / (float)texH_;
        g.v1 = (float)V1 / (float)texH_;
    }

    // Texture path: prefer name from header under font/
    if (!texName.empty()) {
        std::string p = std::string("font/") + texName;
        if (!texture_.LoadPNGAsset(p.c_str())) {
            if (texturePath) texture_.LoadPNGAsset(texturePath);
        }
    } else if (texturePath) {
        texture_.LoadPNGAsset(texturePath);
    }

    loaded_ = texture_.Width() > 0 || true; // metrics alone still usable
    // Prefer success if we got any glyphs
    for (int i = 0; i < 256; ++i) {
        if (glyphs_[i].width > 0) { loaded_ = true; break; }
    }
    return loaded_;
}

float Font::MeasureWidth(const char* text, float scale) const {
    float w = 0;
    if (!text) return 0;
    for (const char* p = text; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        float adv = glyphs_[c].xadv;
        if (adv <= 0.f) adv = 8.f;
        w += adv * scale;
    }
    return w;
}

void Font::AddTextToMesh(MeshBuffer& mesh, const Vector& pos, const char* text,
                         float scale, uint32_t color, TextAlign align) const {
    if (!text || !loaded_) return;
    float x = pos.x, y = pos.y, z = pos.z;
    if (align != TextAlign::Left) {
        float tw = MeasureWidth(text, scale);
        if (align == TextAlign::Center) x -= tw * 0.5f;
        else if (align == TextAlign::Right) x -= tw;
    }
    for (const char* p = text; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        const Glyph& g = glyphs_[c];
        float gw = g.width > 0 ? g.width : 0.f;
        float gh = g.height > 0 ? g.height : cellHeight_;
        if (gw < 1.f) { x += (g.xadv > 0 ? g.xadv : 4.f) * scale; continue; }
        float x0 = x + g.xoff * scale;
        float y0 = y + g.yoff * scale;
        float x1 = x0 + gw * scale;
        float y1 = y0 + gh * scale;
        // Screen space Y grows down; UV: v0 top-of-glyph in file → low V after upload
        PCT1Vertex v[4] = {
            {x0, y0, z, color, g.u0, g.v0},
            {x0, y1, z, color, g.u0, g.v1},
            {x1, y1, z, color, g.u1, g.v1},
            {x1, y0, z, color, g.u1, g.v0},
        };
        mesh.PushQuad(v);
        float adv = g.xadv > 0 ? g.xadv : gw;
        x += adv * scale;
    }
}

Font* FontResource::FindOrLoad(const char* faceName, int pixelSize) {
    char key[64];
    std::snprintf(key, sizeof(key), "%s@%d", faceName ? faceName : "Sans", pixelSize);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return &it->second;
    Font& f = fonts_[key];
    f.LoadFace(faceName ? faceName : "Sans", pixelSize);
    return &f;
}

Font* FontResource::Find(const char* faceName) const {
    // Prefer any size for face
    if (!faceName) return nullptr;
    for (auto& kv : fonts_) {
        if (kv.first.compare(0, std::strlen(faceName), faceName) == 0)
            return const_cast<Font*>(&kv.second);
    }
    return nullptr;
}
