#include "Noise.h"
#include <cstring>
#include <algorithm>

// Standard Perlin permutation seed (will be overwritten by Init from Random)
// Original s_DataPattern / s_Perm are runtime-initialized; using classic Ken Perlin table
// as a functional equivalent until exact bytes recovered from runtime or Init disassembly.
static const uint8_t kDefaultPerm[256] = {
  151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
  8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
  35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
  134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
  55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
  18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
  250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
  189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
  172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
  228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
  107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
  138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

uint8_t Noise::s_DataPattern[256];

Noise::Noise() {
    std::memcpy(perm_, kDefaultPerm, 256);
    std::memcpy(perm_ + 256, kDefaultPerm, 256);
    std::memcpy(s_DataPattern, kDefaultPerm, 256);
}

void Noise::Init(Random& rng, int octaves, float frequency, float amplitude, float persistence) {
    octaves_ = std::max(1, octaves);
    frequency_ = frequency;
    amplitude_ = amplitude;
    persistence_ = persistence;
    // Shuffle permutation with Random (matching Init taking Random&)
    for (int i = 0; i < 256; ++i) perm_[i] = (uint8_t)i;
    for (int i = 255; i > 0; --i) {
        int j = rng.NextInt(i + 1);
        std::swap(perm_[i], perm_[j]);
    }
    std::memcpy(perm_ + 256, perm_, 256);
    std::memcpy(s_DataPattern, perm_, 256);
}

float Noise::Grad(int hash, float x, float y, float z) const {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float Noise::Noise3(float x, float y, float z) const {
    int X = (int)std::floor(x) & 255;
    int Y = (int)std::floor(y) & 255;
    int Z = (int)std::floor(z) & 255;
    x -= std::floor(x); y -= std::floor(y); z -= std::floor(z);
    float u = Fade(x), v = Fade(y), w = Fade(z);
    int A  = perm_[X] + Y, AA = perm_[A] + Z, AB = perm_[A + 1] + Z;
    int B  = perm_[X + 1] + Y, BA = perm_[B] + Z, BB = perm_[B + 1] + Z;
    float res = 
        (1-w)*((1-v)*((1-u)*Grad(perm_[AA],x,y,z) + u*Grad(perm_[BA],x-1,y,z))
             + v*((1-u)*Grad(perm_[AB],x,y-1,z) + u*Grad(perm_[BB],x-1,y-1,z)))
      + w*((1-v)*((1-u)*Grad(perm_[AA+1],x,y,z-1) + u*Grad(perm_[BA+1],x-1,y,z-1))
             + v*((1-u)*Grad(perm_[AB+1],x,y-1,z-1) + u*Grad(perm_[BB+1],x-1,y-1,z-1)));
    return res;
}

float Noise::GetValue(float x, float y, float z) const {
    float sum = 0, amp = amplitude_, freq = frequency_;
    for (int i = 0; i < octaves_; ++i) {
        sum += Noise3(x * freq, y * freq, z * freq) * amp;
        freq *= 2.f;
        amp *= persistence_;
    }
    return sum;
}

float Noise::GetValue(float x, float y) const {
    return GetValue(x, y, 0.f);
}

float Noise::GetValue(int x, int y) const {
    return GetValue((float)x, (float)y);
}

float Noise::GetValue(int x, int y, int z) const {
    return GetValue((float)x, (float)y, (float)z);
}

float Noise2D(const unsigned char* table, float x, float y) {
    // Simplified; original uses the supplied table
    Noise n;
    if (table) std::memcpy(n.s_DataPattern, table, 256);
    return n.GetValue(x, y);
}

float Noise3D(const unsigned char* table, float x, float y, float z) {
    Noise n;
    if (table) std::memcpy(n.s_DataPattern, table, 256);
    return n.GetValue(x, y, z);
}
