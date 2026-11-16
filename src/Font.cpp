#include "Font.h"
#include "Assets.h"
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <algorithm>

// UTF-8 → codepoint. Returns number of bytes consumed (1..4), 0 on error.
static int utf8Next(const char* s, unsigned& outCp) {
    if (!s || !*s) { outCp = 0; return 0; }
    const unsigned char* p = (const unsigned char*)s;
    unsigned c0 = p[0];
    if (c0 < 0x80) { outCp = c0; return 1; }
    if ((c0 & 0xE0) == 0xC0 && p[1]) {
        outCp = ((c0 & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    if ((c0 & 0xF0) == 0xE0 && p[1] && p[2]) {
        outCp = ((c0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }
    if ((c0 & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
        outCp = ((c0 & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    outCp = c0; // invalid → emit byte as-is
    return 1;
}

bool Font::LoadFace(const char* faceName, int pixelSize) {
    if (!faceName || pixelSize <= 0) return false;
    char metrics[64], texPath[64];
    std::snprintf(metrics, sizeof(metrics), "font/font_%d.txt", pixelSize);
    std::snprintf(texPath, sizeof(texPath), "font/font_%d.png", pixelSize);
    faceName_ = faceName;
    return Load(metrics, texPath);
}

bool Font::Load(const char* metricsPath, const char* texturePath) {
    glyphs_.clear();
    loaded_ = false;
    if (!metricsPath) return false;

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
        if (line[0] == '#' && line.find("FaceName") != std::string::npos) continue;
        if (line[0] == '#' && line.find("CharCode") != std::string::npos) continue;

        if (!headerDone) {
            char face[64] = {}, tex[64] = {};
            int chH = 0, cellH = 0, num = 0, tw = 0, th = 0;
            if (std::sscanf(line.c_str(), "%63s %d %d %d %63s %d %d",
                            face, &chH, &cellH, &num, tex, &tw, &th) >= 5) {
                cellHeight_ = (float)(cellH > 0 ? cellH : chH);
                // AlignRect / DrawText extent uses full cell so glyphs are not clipped
                lineHeight_ = cellHeight_ > (float)chH ? cellHeight_ : (float)chH;
                texW_ = tw > 0 ? tw : 1;
                texH_ = th > 0 ? th : 1;
                texName = tex;
                headerDone = true;
            }
            continue;
        }

        // Glyph: hexCode A B C U0 V0 U1 V1  (codes up to 0x21E1)
        unsigned int code = 0;
        int A = 0, B = 0, C = 0, U0 = 0, V0 = 0, U1 = 0, V1 = 0;
        if (std::sscanf(line.c_str(), "%x %d %d %d %d %d %d %d",
                        &code, &A, &B, &C, &U0, &V0, &U1, &V1) < 8)
            continue;
        Glyph g;
        g.code = (int)code;
        g.xoff = (float)A;
        g.width = (float)(U1 - U0);
        g.height = (float)(V1 - V0);
        if (g.width < 1.f) g.width = (float)std::max(B, 1);
        if (g.height < 1.f) g.height = cellHeight_;
        g.xadv = g.width + (float)C;
        if (g.xadv < 1.f) g.xadv = g.width > 0 ? g.width : 4.f;
        g.u0 = (float)U0 / (float)texW_;
        g.u1 = (float)U1 / (float)texW_;
        g.v0 = (float)V0 / (float)texH_;
        g.v1 = (float)V1 / (float)texH_;
        glyphs_[g.code] = g;
    }

    if (!texName.empty()) {
        std::string p = std::string("font/") + texName;
        if (!texture_.LoadPNGAsset(p.c_str())) {
            if (texturePath) texture_.LoadPNGAsset(texturePath);
        }
    } else if (texturePath) {
        texture_.LoadPNGAsset(texturePath);
    }
    // font_N.png has no tRNS — black cells are opaque unless converted (matches
    // binary alpha-blend text path with gray→alpha atlas).
    if (texture_.Valid())
        texture_.ConvertGrayscaleToAlphaMask();

    loaded_ = !glyphs_.empty();
    return loaded_;
}

const Glyph* Font::FindGlyph(unsigned code) const {
    auto it = glyphs_.find((int)code);
    if (it != glyphs_.end()) return &it->second;
    return nullptr;
}

float Font::MeasureWidth(const char* text, float scale) const {
    float w = 0;
    if (!text) return 0;
    const char* p = text;
    while (*p) {
        unsigned cp = 0;
        int n = utf8Next(p, cp);
        if (n <= 0) break;
        p += n;
        if (cp == '\n' || cp == '\r') continue;
        const Glyph* g = FindGlyph(cp);
        float adv = g ? g->xadv : 8.f;
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
    const char* p = text;
    while (*p) {
        unsigned cp = 0;
        int n = utf8Next(p, cp);
        if (n <= 0) break;
        p += n;
        if (cp == '\n' || cp == '\r') continue;
        const Glyph* g = FindGlyph(cp);
        if (!g || g->width < 1.f) {
            x += 4.f * scale;
            continue;
        }
        float gw = g->width;
        float gh = g->height > 0 ? g->height : cellHeight_;
        float x0 = x + g->xoff * scale;
        float y0 = y + g->yoff * scale;
        float x1 = x0 + gw * scale;
        float y1 = y0 + gh * scale;
        PCT1Vertex v[4] = {
            {x0, y0, z, color, g->u0, g->v0},
            {x0, y1, z, color, g->u0, g->v1},
            {x1, y1, z, color, g->u1, g->v1},
            {x1, y0, z, color, g->u1, g->v0},
        };
        mesh.PushQuad(v);
        float adv = g->xadv > 0 ? g->xadv : gw;
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
    if (!faceName) return nullptr;
    for (auto& kv : fonts_) {
        if (kv.first.compare(0, std::strlen(faceName), faceName) == 0)
            return const_cast<Font*>(&kv.second);
    }
    return nullptr;
}

void FontResource::InvalidateGL() {
    for (auto& kv : fonts_)
        kv.second.GetTexture().InvalidateGL();
}

void FontResource::EnsureAllUploaded() {
    for (auto& kv : fonts_)
        kv.second.GetTexture().EnsureUploaded();
}
