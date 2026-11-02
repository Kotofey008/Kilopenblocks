#include "Assets.h"
#include "Scene.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <algorithm>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

static float parseFloat(const std::string& v) {
    return std::strtof(v.c_str(), nullptr);
}
static int parseInt(const std::string& v) {
    return (int)std::strtol(v.c_str(), nullptr, 10);
}
static bool parseBool(const std::string& v) {
    return v == "1" || v == "true" || v == "True";
}
static void parseVec3(const std::string& v, Vector& out) {
    std::istringstream iss(v);
    iss >> out.x >> out.y >> out.z;
}
static void parseColor255(const std::string& v, float& r, float& g, float& b, float& a) {
    // start.scene Color = "A R G B" with A often 255
    int c0 = 255, c1 = 255, c2 = 255, c3 = 255;
    std::istringstream iss(v);
    iss >> c0 >> c1 >> c2 >> c3;
    r = c1 / 255.f;
    g = c2 / 255.f;
    b = c3 / 255.f;
    a = c0 / 255.f;
}

static bool parseParamSetKey(const std::string& key, int& index, std::string& field) {
    if (key.compare(0, 10, "ParamSets[") != 0) return false;
    size_t rb = key.find(']');
    if (rb == std::string::npos) return false;
    index = parseInt(key.substr(10, rb - 10));
    if (index < 0 || index >= 4) return false;
    if (rb + 1 >= key.size() || key[rb + 1] != '.') return false;
    field = key.substr(rb + 2);
    return true;
}

bool Scene::Load(const std::string& path) {
    std::string fileText;
    std::istream* pin = nullptr;
    std::istringstream assetStream;
    std::ifstream fileStream;
    if (Assets::ReadText(path.c_str(), fileText)) {
        assetStream.str(fileText);
        pin = &assetStream;
    } else {
        fileStream.open(path);
        if (!fileStream) {
            std::cerr << "Scene::Load failed to open " << path << std::endl;
            return false;
        }
        pin = &fileStream;
    }
    std::istream& f = *pin;
    std::string line, section;
    UISceneObject* currentUI = nullptr;
    int uiCount = 0;

    globals = SceneGlobalsConfig{};
    camera = SceneCameraConfig{};
    playerSet = SceneEntityPlayerSetConfig{};
    // Defaults aligned with binary ParamSet ctor + start.scene non-zero sets
    playerSet.paramSets[0] = PlayerParamSet{};
    playerSet.paramSets[1] = PlayerParamSet{2.f, 9.f, 9.f, 0.501f, 0.062625f, -4.9f, -2.f, 3.f};
    playerSet.paramSets[2] = PlayerParamSet{2.f, 9.f, 18.f, 0.501f, 0.062625f, 0.f, 0.f, 5.f};
    playerSet.paramSets[3] = PlayerParamSet{6.f, 18.f, 18.f, 0.501f, 0.062625f, -9.81f, -50.f, 5.f};

    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim(line);
        if (line.empty()) continue;

        if (line == "End") {
            if (currentUI) {
                objects.push_back(currentUI);
                currentUI = nullptr;
            }
            section.clear();
            continue;
        }

        if (line == "Globals" || line == "Camera" || line == "GridModel" ||
            line == "SkyBox" || line == "Clouds" || line == "EntityPlayerSet" ||
            line == "UISceneObject" || line.find("SceneObject") == 0) {
            section = line;
            if (section == "UISceneObject") {
                currentUI = new UISceneObject();
                uiCount++;
            } else if (section == "Camera") {
                camera.present = true;
            } else if (section == "EntityPlayerSet") {
                playerSet.present = true;
            }
            continue;
        }

        auto colon = line.find(':');
        auto eq = line.find('=');
        if (colon == std::string::npos || eq == std::string::npos || eq < colon) continue;

        std::string key = trim(line.substr(0, colon));
        std::string val = trim(line.substr(eq + 1));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);

        if (section == "Globals") {
            if (key == "TimeMultiplier") globals.timeMultiplier = parseFloat(val);
            else if (key == "MaxTimestep") globals.maxTimestep = parseInt(val);
            else if (key == "WorldSizeMin") parseVec3(val, globals.worldSizeMin);
            else if (key == "WorldSizeMax") parseVec3(val, globals.worldSizeMax);
            else if (key == "ClearColor") {
                parseColor255(val, globals.clearR, globals.clearG, globals.clearB, globals.clearA);
                globals.hasClearColor = true;
            } else if (key == "TexAtlases[0]") {
                globals.texAtlas0 = val;
            }
        } else if (section == "Camera") {
            camera.present = true;
            if (key == "ID") camera.id = parseInt(val);
            else if (key == "Position") parseVec3(val, camera.position);
            else if (key == "Perspective") camera.perspective = parseBool(val);
            else if (key == "FieldOfView") camera.fieldOfView = parseFloat(val);
            else if (key == "NearPlane") camera.nearPlane = parseFloat(val);
            else if (key == "FarPlane") camera.farPlane = parseFloat(val);
            else if (key == "ViewportX0") camera.viewportX0 = parseFloat(val);
            else if (key == "ViewportY0") camera.viewportY0 = parseFloat(val);
            else if (key == "ViewportX1") camera.viewportX1 = parseFloat(val);
            else if (key == "ViewportY1") camera.viewportY1 = parseFloat(val);
            else if (key == "SizeFromViewport") camera.sizeFromViewport = parseBool(val);
            else if (key == "ClearColor") camera.clearColor = parseBool(val);
            else if (key == "ClearDepth") camera.clearDepth = parseBool(val);
            else if (key == "ColorValue") {
                parseColor255(val, camera.clearR, camera.clearG, camera.clearB, camera.clearA);
            }
        } else if (section == "EntityPlayerSet") {
            playerSet.present = true;
            if (key == "ID") playerSet.id = parseInt(val);
            else if (key == "Camera") playerSet.cameraObjectId = parseInt(val);
            else if (key == "GridModel") playerSet.gridModelObjectId = parseInt(val);
            else if (key == "LocalBBox.Min") parseVec3(val, playerSet.localBBoxMin);
            else if (key == "LocalBBox.Max") parseVec3(val, playerSet.localBBoxMax);
            else if (key == "CollisionBBox.Min") parseVec3(val, playerSet.collisionBBoxMin);
            else if (key == "CollisionBBox.Max") parseVec3(val, playerSet.collisionBBoxMax);
            else if (key == "OperationDistance") playerSet.operationDistance = parseFloat(val);
            else if (key == "OperationTime") playerSet.operationTime = parseInt(val);
            else if (key == "TouchSensitivity") playerSet.touchSensitivity = parseFloat(val);
            else if (key == "FlipXRotation") playerSet.flipXRotation = parseBool(val);
            else if (key == "FlipYRotation") playerSet.flipYRotation = parseBool(val);
            else if (key == "DXThreshold") playerSet.dxThreshold = parseInt(val);
            else if (key == "DYThreshold") playerSet.dyThreshold = parseInt(val);
            else {
                int idx = 0;
                std::string field;
                if (parseParamSetKey(key, idx, field)) {
                    PlayerParamSet& ps = playerSet.paramSets[idx];
                    if (field == "MaximumSpeed") ps.maximumSpeed = parseFloat(val);
                    else if (field == "Acceleration") ps.acceleration = parseFloat(val);
                    else if (field == "Deceleration") ps.deceleration = parseFloat(val);
                    else if (field == "StepSize") ps.stepSize = parseFloat(val);
                    else if (field == "SmallStepSize") ps.smallStepSize = parseFloat(val);
                    else if (field == "Gravity") ps.gravity = parseFloat(val);
                    else if (field == "GravityLimit") ps.gravityLimit = parseFloat(val);
                    else if (field == "Jump") ps.jump = parseFloat(val);
                }
            }
        } else if (section == "UISceneObject" && currentUI) {
            if (key == "ID") currentUI->id = parseInt(val);
            else if (key == "Name") currentUI->name = val;
            else if (key == "FileName") currentUI->fileName = val;
            else if (key == "Visible") currentUI->visible = parseBool(val);
            else if (key == "Position") parseVec3(val, currentUI->position);
            currentUI->props.values[key] = val;
        }
    }

    if (grid) {
        ApplyGlobalsToGrid(*grid, /*mobileClamp=*/false);
    }

    std::cout << "Scene::Load: " << uiCount << " UISceneObjects"
              << ", Camera FOV=" << camera.fieldOfView
              << " near=" << camera.nearPlane << " far=" << camera.farPlane
              << ", EPS reach=" << playerSet.operationDistance
              << " sens=" << playerSet.touchSensitivity
              << " eyeH=" << playerSet.EyeHeightFromBBox()
              << ", world [" << globals.worldSizeMin.x << ".." << globals.worldSizeMax.x << "]"
              << std::endl;
    if (playerSet.present) {
        for (int i = 0; i < 4; ++i) {
            const auto& p = playerSet.paramSets[i];
            std::cout << "  ParamSets[" << i << "] speed=" << p.maximumSpeed
                      << " grav=" << p.gravity << " jump=" << p.jump << std::endl;
        }
    }
    return true;
}

void Scene::ApplyGlobalsToGrid(GridData& g, bool mobileClamp) const {
    Vector minV = globals.worldSizeMin;
    Vector maxV = globals.worldSizeMax;
    if (mobileClamp) {
        float half = kMobileWorldHalfExtent;
        minV.x = std::max(minV.x, -half);
        minV.z = std::max(minV.z, -half);
        maxV.x = std::min(maxV.x, half);
        maxV.z = std::min(maxV.z, half);
        if (maxV.y - minV.y > (kMobileWorldMaxY - kMobileWorldMinY) * 2.f) {
            minV.y = kMobileWorldMinY;
            maxV.y = kMobileWorldMaxY;
        }
    }
    g.worldMin = minV;
    g.worldMax = maxV;
    g.InitDimensionsFromWorldBounds();
}
