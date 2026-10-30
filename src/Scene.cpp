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

// Minimal but expanding parser for the observed text scene format.
// Handles Globals (world bounds, time), and records Camera / GridModel / UISceneObject sections.
// Full property typing, Quaternion, message wiring, and LoadSceneObjects call still incomplete.
bool Scene::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Scene::Load failed to open " << path << std::endl;
        return false;
    }
    std::string line, section;
    UISceneObject* currentUI = nullptr;
    int objectCount = 0;

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
            line == "UISceneObject" || line.find("SceneObject") == 0) {
            section = line;
            if (section == "UISceneObject") {
                currentUI = new UISceneObject();
                objectCount++;
            }
            continue;
        }

        // key:Type = value   or   key:Type = value (with spaces)
        auto colon = line.find(':');
        auto eq = line.find('=');
        if (colon == std::string::npos || eq == std::string::npos || eq < colon) continue;

        std::string key = trim(line.substr(0, colon));
        std::string val = trim(line.substr(eq + 1));
        // strip quotes if present
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);

        if (section == "Globals") {
            if (key == "TimeMultiplier") {
                timeMultiplier = std::strtof(val.c_str(), nullptr);
            } else if (key == "WorldSizeMin") {
                // "x y z"
                std::istringstream iss(val);
                iss >> worldSizeMin.x >> worldSizeMin.y >> worldSizeMin.z;
            } else if (key == "WorldSizeMax") {
                std::istringstream iss(val);
                iss >> worldSizeMax.x >> worldSizeMax.y >> worldSizeMax.z;
            }
            // ClearColor, TexAtlases etc. can be added similarly
        } else if (section == "Camera") {
            // Camera properties can be stored on a Camera object later
        } else if (section == "GridModel") {
            // GridModel owns GridData; bounds already set from Globals
        } else if (section == "UISceneObject" && currentUI) {
            if (key == "ID") {
                currentUI->id = (int)std::strtol(val.c_str(), nullptr, 10);
            } else if (key == "Name") {
                currentUI->name = val;
            } else if (key == "FileName") {
                currentUI->fileName = val;
            } else if (key == "Visible") {
                currentUI->visible = (val == "1" || val == "true");
            } else if (key == "Position") {
                std::istringstream iss(val);
                iss >> currentUI->position.x >> currentUI->position.y >> currentUI->position.z;
            }
            // ElementClickedMessage etc. left for message system
        }
    }

    // Apply bounds to grid if present
    if (grid) {
        grid->worldMin = worldSizeMin;
        grid->worldMax = worldSizeMax;
        grid->InitDimensionsFromWorldBounds();
    }

    std::cout << "Scene::Load: " << objectCount << " UISceneObjects, "
              << "world [" << worldSizeMin.x << "," << worldSizeMin.y << "," << worldSizeMin.z
              << "] .. [" << worldSizeMax.x << "," << worldSizeMax.y << "," << worldSizeMax.z << "]"
              << std::endl;
    return true;
}
