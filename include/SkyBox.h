#pragma once
#include "Mesh.h"
#include "Texture.h"
#include "Vector.h"
#include <cmath>
#include <string>
#include <vector>
#include <cstdint>

struct SceneSkyBoxConfig;

// SkyBox from Ghidra: SetTimeOfDay, UpdateSkyMesh, CreateStarsMesh, FindStarIndex, Render3D.
class SkyBox {
public:
    void LoadResources();
    void ApplyConfig(const SceneSkyBoxConfig& cfg);

    void SetTimeOfDay(float t);
    float GetTimeOfDay() const { return timeOfDay_; }
    float GetDayFactor() const { return dayFactor_; }

    Vector GetSunDirection() const;
    Vector GetMoonDirection() const;
    float GetSunIntensity() const;
    float GetAmbientScale() const;

    void GetSkyUpperColor(float& r, float& g, float& b) const;
    void GetSkyLowerColor(float& r, float& g, float& b) const;
    void GetSkyColor(float& r, float& g, float& b) const;

    void BuildSunMoonMeshes(const Vector& eye, const Vector& right, const Vector& up,
                            float dist, MeshBuffer& sunMesh, MeshBuffer& moonMesh);

    void EnsureStarsMesh();
    void RebuildStarsSubset();
    const MeshBuffer& StarsMesh() const { return starsDrawMesh_; }
    bool StarsVisible() const { return dayFactor_ < 0.98f && numStars_ > 0; }

    void UpdateSkyMesh();
    const MeshBuffer& HorizonMesh() const { return horizonMesh_; }
    bool HorizonEnabled() const { return horizon_; }

    Texture& SunTexture() { return sunTex_; }
    Texture& MoonTexture() { return moonTex_; }
    float SunScale() const { return sunScale_; }
    float MoonScale() const { return moonScale_; }
    float SkyObjectScale() const { return skyObjectScale_; }

private:
    float timeOfDay_ = 0.5f;
    float dayFactor_ = 1.f;
    float cachedDayFactor_ = -1.f;

    float sunriseBegin_ = 0.208333f;
    float sunriseEnd_   = 0.270833f;
    float sunsetBegin_  = 0.729167f;
    float sunsetEnd_    = 0.791667f;

    // ctor AABBGGRR: day lower 167,207,255 ; upper 115,168,255 ; night 9,12,19 / 4,6,10
    float dayLowerR_ = 167.f / 255.f, dayLowerG_ = 207.f / 255.f, dayLowerB_ = 1.f;
    float dayUpperR_ = 115.f / 255.f, dayUpperG_ = 168.f / 255.f, dayUpperB_ = 1.f;
    float nightLowerR_ = 9.f / 255.f, nightLowerG_ = 12.f / 255.f, nightLowerB_ = 19.f / 255.f;
    float nightUpperR_ = 4.f / 255.f, nightUpperG_ = 6.f / 255.f, nightUpperB_ = 10.f / 255.f;

    bool horizon_ = true;
    std::string sunTexPath_ = "textures/sun.png";
    std::string moonTexPath_ = "textures/moon.png";
    float sunScale_ = 3.f;
    float moonScale_ = 3.f;
    float skyObjectScale_ = 16.f;
    int numStars_ = 600;
    int starsSeed_ = 1;

    Texture sunTex_, moonTex_;
    MeshBuffer horizonMesh_;
    MeshBuffer starsFullMesh_;
    MeshBuffer starsDrawMesh_;
    std::vector<uint16_t> starSortKeys_;
    std::vector<int> starOrder_;
    bool starsBuilt_ = false;

    void RecomputeDayFactor();
    static uint32_t PackColor(float r, float g, float b, float a = 1.f);
};
