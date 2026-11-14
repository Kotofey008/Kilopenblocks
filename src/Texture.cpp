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
    // glTexImage2D places the first memory row at V=0 (bottom).
    // Our PNG decoder stores file-top as memory row 0, so atlas tileV=0 (top of
    // blocks.png) lives at the BOTTOM of the GL texture → low V, not high V.
    // Previous "flip V" mapped tileV=0 to V≈1 (top of GL = bottom of PNG = white).
    u0 = (float)tileU * inv;
    u1 = u0 + inv;
    v0 = (float)tileV * inv;
    v1 = v0 + inv;
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
    std::vector<uint8_t> plte; // RGB triples
    std::vector<uint8_t> trns; // alpha per palette index (optional)
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
            if (chunk[10] != 0 || chunk[11] == 0 /*filter*/ ) { /* ok */ }
            if (chunk[12] != 0) return false; // interlace not supported
        } else if (std::strcmp(type, "PLTE") == 0) {
            plte.assign(chunk, chunk + len);
        } else if (std::strcmp(type, "tRNS") == 0) {
            trns.assign(chunk, chunk + len);
        } else if (std::strcmp(type, "IDAT") == 0) {
            idat.insert(idat.end(), chunk, chunk + len);
        } else if (std::strcmp(type, "IEND") == 0) {
            break;
        }
        pos += len + 4;
    }
    if (!width || !height || idat.empty()) return false;

    // Samples per pixel / bits
    // colorType: 0=gray, 2=RGB, 3=indexed, 4=grayA, 6=RGBA
    int channels = 0;
    if (colorType == 0) channels = 1;
    else if (colorType == 2) channels = 3;
    else if (colorType == 3) channels = 1;
    else if (colorType == 4) channels = 2;
    else if (colorType == 6) channels = 4;
    else return false;
    if (bitDepth != 8 && !(colorType == 3 && (bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8)))
        return false;
    if (colorType == 3 && plte.size() < 3) return false;

    const size_t rowBits = (size_t)width * (size_t)bitDepth * (size_t)channels;
    const size_t rowBytes = (rowBits + 7) / 8;
    const int bppBytes = (int)((bitDepth * channels + 7) / 8); // bytes per pixel for filter (at least 1)

    uLongf destLen = (uLongf)((rowBytes + 1) * height + 64);
    std::vector<uint8_t> raw(destLen);
    int zst = uncompress(raw.data(), &destLen, idat.data(), (uLong)idat.size());
    if (zst != Z_OK) {
        destLen = (uLongf)((rowBytes + 1) * height * 4 + 256);
        raw.resize(destLen);
        if (uncompress(raw.data(), &destLen, idat.data(), (uLong)idat.size()) != Z_OK)
            return false;
    }
    raw.resize(destLen);

    // Unfilter
    std::vector<uint8_t> prev(rowBytes, 0);
    std::vector<uint8_t> cur(rowBytes, 0);
    std::vector<uint8_t> unfiltered((size_t)height * rowBytes);
    const uint8_t* src = raw.data();
    for (unsigned y = 0; y < height; ++y) {
        if (src >= raw.data() + raw.size()) return false;
        uint8_t filter = *src++;
        if (src + rowBytes > raw.data() + raw.size()) return false;
        std::memcpy(cur.data(), src, rowBytes);
        src += rowBytes;
        auto paeth = [](int a, int b, int c) {
            int p = a + b - c;
            int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
            if (pa <= pb && pa <= pc) return a;
            if (pb <= pc) return b;
            return c;
        };
        for (size_t i = 0; i < rowBytes; ++i) {
            uint8_t left = (i >= (size_t)bppBytes) ? cur[i - bppBytes] : 0;
            uint8_t up = prev[i];
            uint8_t upleft = (i >= (size_t)bppBytes) ? prev[i - bppBytes] : 0;
            switch (filter) {
                case 0: break;
                case 1: cur[i] = (uint8_t)(cur[i] + left); break;
                case 2: cur[i] = (uint8_t)(cur[i] + up); break;
                case 3: cur[i] = (uint8_t)(cur[i] + ((left + up) >> 1)); break;
                case 4: cur[i] = (uint8_t)(cur[i] + paeth(left, up, upleft)); break;
                default: return false;
            }
        }
        std::memcpy(unfiltered.data() + (size_t)y * rowBytes, cur.data(), rowBytes);
        prev.swap(cur);
    }

    out.resize((size_t)width * height * 4);
    auto writeRGBA = [&](unsigned x, unsigned y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        size_t di = ((size_t)y * width + x) * 4;
        out[di] = r; out[di+1] = g; out[di+2] = b; out[di+3] = a;
    };

    if (colorType == 3) {
        // Indexed: extract bitDepth-bit indices, map through PLTE + tRNS
        for (unsigned y = 0; y < height; ++y) {
            const uint8_t* row = unfiltered.data() + (size_t)y * rowBytes;
            for (unsigned x = 0; x < width; ++x) {
                unsigned idx = 0;
                if (bitDepth == 8) {
                    idx = row[x];
                } else if (bitDepth == 4) {
                    uint8_t b = row[x / 2];
                    idx = (x & 1) ? (b & 0x0F) : (b >> 4);
                } else if (bitDepth == 2) {
                    uint8_t b = row[x / 4];
                    int shift = (3 - (int)(x % 4)) * 2;
                    idx = (b >> shift) & 0x3;
                } else if (bitDepth == 1) {
                    uint8_t b = row[x / 8];
                    int shift = 7 - (int)(x % 8);
                    idx = (b >> shift) & 0x1;
                }
                size_t pi = (size_t)idx * 3;
                uint8_t r = 0, g = 0, b = 0, a = 255;
                if (pi + 2 < plte.size()) {
                    r = plte[pi]; g = plte[pi+1]; b = plte[pi+2];
                }
                if (idx < trns.size()) a = trns[idx];
                writeRGBA(x, y, r, g, b, a);
            }
        }
        return true;
    }

    // 8-bit gray / RGB / grayA / RGBA
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t* row = unfiltered.data() + (size_t)y * rowBytes;
        for (unsigned x = 0; x < width; ++x) {
            const uint8_t* p = row + (size_t)x * channels;
            uint8_t r, g, b, a = 255;
            if (colorType == 0) { r = g = b = p[0]; }
            else if (colorType == 2) { r = p[0]; g = p[1]; b = p[2]; }
            else if (colorType == 4) { r = g = b = p[0]; a = p[1]; }
            else { r = p[0]; g = p[1]; b = p[2]; a = p[3]; }
            writeRGBA(x, y, r, g, b, a);
        }
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
