#include "Assets.h"
#include "Texture.h"
#include <cmath>
#include "BlockDef.h"
#include <cstdio>
#include <cstring>
#include <zlib.h>
#include <vector>
#include <fstream>

#if defined(__ANDROID__)
#include <GLES/gl.h>
#endif

Texture::~Texture() {
#if defined(__ANDROID__)
    if (glId_) { glDeleteTextures(1, &glId_); glId_ = 0; }
#endif
}

void Texture::TileUV(int tileU, int tileV, float& u0, float& v0, float& u1, float& v1) {
    const float inv = 1.f / (float)kAtlasTiles;
    // blocks.png: row 0 at top of image; GL V=0 at bottom → flip V
    u0 = tileU * inv;
    u1 = u0 + inv;
    v1 = 1.f - tileV * inv;       // top of tile in GL space
    v0 = v1 - inv;                // bottom of tile
}

void GetFaceUV(const BlockDef& def, int face, float& u0, float& v0, float& u1, float& v1) {
    // BlockDef stores atlas tile coords as Uxp/Vxp etc. (integer tile indices)
    int tu = 0, tv = 0;
    switch (face) {
        case 0: tu = (int)def.uxp; tv = (int)def.vxp; break; // +X
        case 1: tu = (int)def.uxn; tv = (int)def.vxn; break; // -X
        case 2: tu = (int)def.uyp; tv = (int)def.vyp; break; // +Y
        case 3: tu = (int)def.uyn; tv = (int)def.vyn; break; // -Y
        case 4: tu = (int)def.uzp; tv = (int)def.vzp; break; // +Z
        case 5: tu = (int)def.uzn; tv = (int)def.vzn; break; // -Z
        default: break;
    }
    Texture::TileUV(tu, tv, u0, v0, u1, v1);
}

// Minimal PNG decoder (RGBA8, no interlace) — equivalent role to embedded lodepng
static bool DecodePNG(const std::vector<uint8_t>& file, std::vector<uint8_t>& out,
                      unsigned& width, unsigned& height) {
    if (file.size() < 33) return false;
    if (std::memcmp(file.data(), "\x89PNG\r\n\x1a\n", 8) != 0) return false;
    size_t pos = 8;
    std::vector<uint8_t> idat;
    int bitDepth = 8, colorType = 6;
    while (pos + 12 <= file.size()) {
        uint32_t len = (file[pos]<<24)|(file[pos+1]<<16)|(file[pos+2]<<8)|file[pos+3];
        pos += 4;
        char type[5] = {(char)file[pos],(char)file[pos+1],(char)file[pos+2],(char)file[pos+3],0};
        pos += 4;
        if (pos + len + 4 > file.size()) return false;
        const uint8_t* chunk = file.data() + pos;
        if (std::strcmp(type, "IHDR") == 0 && len >= 13) {
            width  = (chunk[0]<<24)|(chunk[1]<<16)|(chunk[2]<<8)|chunk[3];
            height = (chunk[4]<<24)|(chunk[5]<<16)|(chunk[6]<<8)|chunk[7];
            bitDepth = chunk[8]; colorType = chunk[9];
            if (chunk[10] != 0 || chunk[11] != 0 || chunk[12] != 0) return false; // compression/filter/interlace
        } else if (std::strcmp(type, "IDAT") == 0) {
            idat.insert(idat.end(), chunk, chunk + len);
        } else if (std::strcmp(type, "IEND") == 0) {
            break;
        }
        pos += len + 4; // data + CRC
    }
    if (!width || !height || idat.empty()) return false;

    uLongf destLen = (uLongf)((width * 4 + 1) * height * 2); // upper bound
    std::vector<uint8_t> raw(destLen);
    if (uncompress(raw.data(), &destLen, idat.data(), (uLong)idat.size()) != Z_OK) {
        // try larger buffer
        destLen = (uLongf)(width * height * 8 + height);
        raw.resize(destLen);
        if (uncompress(raw.data(), &destLen, idat.data(), (uLong)idat.size()) != Z_OK)
            return false;
    }
    raw.resize(destLen);

    // Apply PNG filters → RGBA
    int bpp = (colorType == 6) ? 4 : (colorType == 2) ? 3 : 0;
    if (bpp == 0 || bitDepth != 8) return false;
    size_t stride = width * bpp;
    out.resize(width * height * 4);
    const uint8_t* src = raw.data();
    std::vector<uint8_t> prev(stride, 0);
    std::vector<uint8_t> cur(stride, 0);

    for (unsigned y = 0; y < height; ++y) {
        if (src >= raw.data() + raw.size()) return false;
        uint8_t filter = *src++;
        if (src + stride > raw.data() + raw.size()) return false;
        std::memcpy(cur.data(), src, stride);
        src += stride;
        auto paeth = [](int a, int b, int c) {
            int p = a + b - c;
            int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
            if (pa <= pb && pa <= pc) return a;
            if (pb <= pc) return b;
            return c;
        };
        for (size_t i = 0; i < stride; ++i) {
            uint8_t left = (i >= (size_t)bpp) ? cur[i - bpp] : 0;
            uint8_t up = prev[i];
            uint8_t upleft = (i >= (size_t)bpp) ? prev[i - bpp] : 0;
            switch (filter) {
                case 0: break;
                case 1: cur[i] = (uint8_t)(cur[i] + left); break;
                case 2: cur[i] = (uint8_t)(cur[i] + up); break;
                case 3: cur[i] = (uint8_t)(cur[i] + ((left + up) >> 1)); break;
                case 4: cur[i] = (uint8_t)(cur[i] + paeth(left, up, upleft)); break;
                default: return false;
            }
        }
        for (unsigned x = 0; x < width; ++x) {
            size_t di = ((size_t)y * width + x) * 4;
            size_t si = x * bpp;
            out[di] = cur[si];
            out[di+1] = cur[si+1];
            out[di+2] = cur[si+2];
            out[di+3] = (bpp == 4) ? cur[si+3] : 255;
        }
        prev.swap(cur);
    }
    return true;
}

bool Texture::LoadPNG(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), {});
    return LoadPNGFromMemory(file.data(), file.size());
}
bool Texture::LoadPNGFromMemory(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;
    std::vector<uint8_t> file(data, data + size);
    if (!DecodePNG(file, pixels_, width_, height_)) return false;
    return UploadGL();
}
bool Texture::LoadPNGAsset(const char* assetPath) {
    std::vector<uint8_t> buf;
    if (!Assets::ReadAll(assetPath, buf)) return false;
    return LoadPNGFromMemory(buf.data(), buf.size());
}

bool Texture::UploadGL() {
#if defined(__ANDROID__)
    if (pixels_.empty()) return false;
    if (!glId_) glGenTextures(1, &glId_);
    glBindTexture(GL_TEXTURE_2D, glId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width_, (GLsizei)height_,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    return true;
#else
    return !pixels_.empty(); // host: decode-only OK
#endif
}

void Texture::Bind() const {
#if defined(__ANDROID__)
    if (glId_) glBindTexture(GL_TEXTURE_2D, glId_);
#endif
}

void Texture::Unbind() const {
#if defined(__ANDROID__)
    glBindTexture(GL_TEXTURE_2D, 0);
#endif
}
