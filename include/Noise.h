#pragma once
#include <cstdint>
#include <cmath>

// Observed:
// Noise2D(unsigned char const*, float, float)
// Noise3D(unsigned char const*, float, float, float)
// class Noise { Init(Random&, int, float, float, float); GetValue overloads }
// class PeriodicNoise { s_Perm[512], Init, Noise, GetValue }
// s_DataPattern[256]

class Random {
public:
    explicit Random(uint32_t seed = 1) : state_(seed) {}
    // LCG mul observed elsewhere as 0x343FD; exact Random class constants TBD
    uint32_t Next() {
        state_ = state_ * 0x343FDu + 0x269EC3u;
        return (state_ >> 16) & 0x7FFF;
    }
    float NextFloat() { return Next() / 32768.f; }
    int NextInt(int max) { return max > 0 ? (int)(Next() % (uint32_t)max) : 0; }
private:
    uint32_t state_;
};

class Noise {
public:
    Noise();
    // Init(Random&, octaves?, freq?, amp?, persistence?)
    void Init(Random& rng, int octaves, float frequency, float amplitude, float persistence);
    float GetValue(float x, float y) const;
    float GetValue(float x, float y, float z) const;
    float GetValue(int x, int y) const;
    float GetValue(int x, int y, int z) const;

    static uint8_t s_DataPattern[256]; // observed size 256
private:
    int octaves_ = 4;
    float frequency_ = 0.01f;
    float amplitude_ = 1.f;
    float persistence_ = 0.5f;
    uint8_t perm_[512]; // working table (filled in Init)
    float Fade(float t) const { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
    float Grad(int hash, float x, float y, float z) const;
    float Noise3(float x, float y, float z) const;
};

// Free functions observed
float Noise2D(const unsigned char* table, float x, float y);
float Noise3D(const unsigned char* table, float x, float y, float z);
