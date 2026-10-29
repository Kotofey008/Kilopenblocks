#pragma once
#include "Vector.h"
#include "GridData.h"
#include <string>
#include <vector>
#include <map>

// Observed: LoadSceneObjects(char const*, vector<SceneObject*>&, Properties&, Scene*)
// Scene objects have ID, Name, Position, Rotation, Scale, Visible, UpdateOrder, RenderOrder, etc.
// Specializations: UISceneObject (loads .txt UI defs), GridModel, Camera, SimplePlayer, ...

struct Properties {
    // key-value store used by SetProperties/GetProperties; exact type TBD
    std::map<std::string, std::string> values;
};

class SceneObject {
public:
    int id = 0;
    std::string name;
    Vector position;
    // Quaternion rotation; simplified for now
    Vector scale{1,1,1};
    bool visible = true;
    int updateOrder = 0;
    int renderOrder = 0;
    virtual ~SceneObject() = default;
    virtual void LoadResources() {}
    virtual void FreeResources() {}
    virtual void Update() {}
    virtual void Render3D() {}
    virtual void RenderUI() {}
    virtual void SetProperties(Properties&) {}
    virtual void GetProperties(Properties&) {}
};

class UISceneObject : public SceneObject {
public:
    std::string fileName; // e.g. "Screen/ui/menu.txt"
    // ElementClickedMessage etc. as string commands that call into Game
};

class Scene {
public:
    Vector worldSizeMin{-576, -64, -576};
    Vector worldSizeMax{576, 192, 576};
    float timeMultiplier = 1.f;
    // camera, grid, objects
    std::vector<SceneObject*> objects;
    GridData* grid = nullptr; // owned by GridModel typically

    bool Load(const std::string& path); // loads start.scene style
};
