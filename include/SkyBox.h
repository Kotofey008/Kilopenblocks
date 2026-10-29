#pragma once
#include "Mesh.h"
#include "Texture.h"
#include "Vector.h"
#include <cmath>

// SkyBox::SetTimeOfDay / GetTimeOfDay; Game::UpdateTimeOfDay / UpdateSkyBox
// Assets: sun.png, moon.png; strings SunriseBegin/End, SunsetBegin/End

class SkyBox {
public:
    void LoadResources();
    void SetTimeOfDay(float t); // 0..1, 0=midnight, 0.25=sunrise, 0.5=noon, 0.75=sunset
    float GetTimeOfDay() const { return timeOfDay_; }

    // Sky clear color from time
    void GetSkyColor(float& r, float& g, float& b) const;
    // Sun/moon direction on unit sphere (for lighting + billboard)
    Vector GetSunDirection() const;
    Vector GetMoonDirection() const;
    float GetSunIntensity() const; // 0 at night
    float GetAmbientScale() const;

    Texture& SunTexture() { return sunTex_; }
    Texture& MoonTexture() { return moonTex_; }
    // Billboard quads in world space given camera eye
    void BuildSunMoonMeshes(const Vector& eye, float dist, MeshBuffer& sunMesh, MeshBuffer& moonMesh);

private:
    float timeOfDay_ = 0.5f;
    Texture sunTex_, moonTex_;
    // recovered string constants as defaults
    float sunriseBegin_ = 0.22f, sunriseEnd_ = 0.28f;
    float sunsetBegin_  = 0.72f, sunsetEnd_  = 0.78f;
};
