#pragma once
#include "Mesh.h"
#include "Texture.h"
#include "Vector.h"
#include <cmath>
#include <string>

struct SceneSkyBoxConfig;

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

    void GetSkyColor(float& r, float& g, float& b) const;
    void GetSkyUpperColor(float& r, float& g, float& b) const;
    void GetSkyLowerColor(float& r, float& g, float& b) const;

    void BuildSunMoonMeshes(const Vector& eye, float dist,
                            MeshBuffer& sunMesh, MeshBuffer& moonMesh);
    void EnsureStarsMesh();
    const MeshBuffer& StarsMesh() const { return starsMesh_; }
    bool StarsVisible() const { return dayFactor_ < 0.35f && numStars_ > 0; }

    Texture& SunTexture() { return sunTex_; }
    Texture& MoonTexture() { return moonTex_; }
    float SunScale() const { return sunScale_; }
    float MoonScale() const { return moonScale_; }

private:
    float timeOfDay_ = 0.5f;
    float dayFactor_ = 1.f;

    float sunriseBegin_ = 0.208333f;
    float sunriseEnd_   = 0.270833f;
    float sunsetBegin_  = 0.729167f;
    float sunsetEnd_    = 0.791667f;

    float dayLowerR_ = 1.f, dayLowerG_ = 207.f/255.f, dayLowerB_ = 167.f/255.f;
    float dayUpperR_ = 1.f, dayUpperG_ = 168.f/255.f, dayUpperB_ = 115.f/255.f;
    float nightLowerR_ = 19.f/255.f, nightLowerG_ = 12.f/255.f, nightLowerB_ = 9.f/255.f;
    float nightUpperR_ = 10.f/255.f, nightUpperG_ = 6.f/255.f, nightUpperB_ = 4.f/255.f;

    bool horizon_ = true;
    std::string sunTexPath_ = "textures/sun.png";
    std::string moonTexPath_ = "textures/moon.png";
    float sunScale_ = 3.f;
    float moonScale_ = 3.f;
    int numStars_ = 600;
    int starsSeed_ = 1;

    Texture sunTex_, moonTex_;
    MeshBuffer starsMesh_;
    bool starsBuilt_ = false;

    void RecomputeDayFactor();
};
