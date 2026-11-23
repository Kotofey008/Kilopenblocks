#pragma once
#include "ApplicationBase.h"
#include "Game.h"
#include "Mesh.h"
#include "BlockDef.h"
#include "Texture.h"
#include "Lighting.h"
#include "Clouds.h"
#include "SkyBox.h"
#include "Font.h"
#include <vector>
#include <memory>

// GLApplication extends ApplicationBase with game loop + world mesh rendering.
// Draw path uses recovered DrawPrimitive (PCT1Vertex stride 24).

// Binary MeshBuffer: 7 layers; PushQuads(..., MBI, ...) → layer[MBI].
// DrawMeshes(layer): 0 opaque, 1 cutout GEQUAL 0.5, 2 translucent blend,
// 3–4 water still/flow, 5–6 lava still/flow.
struct RegionMesh {
    int ox = 0, oy = 0, oz = 0;
    MeshBuffer layers[7];
    bool valid = false;
};

class GLApplication : public ApplicationBase {
public:
    GLApplication(int, int);
    ~GLApplication() override;

    void OnCreateGLContext() override;
    void OnDeleteGLContext() override;

    // Called each frame from AndroidMain loop
    void Frame();
    void RenderWorld();
    void RenderHUD(int screenW, int screenH);
    void DrawMenuMaterialSlots(int screenW, int screenH);
    void EnsureRegionMeshes();

    Game& GetGame() { return game_; }

private:
    Game game_;
    std::vector<BlockDef> blockDefs_;
    std::vector<RegionMesh> regionMeshes_;
    bool worldReady_ = false;
    bool gameContentInited_ = false; // true after first full OnCreate (not cleared on TERM)
    Texture blocksAtlas_;
    Texture waterTex_;   // GridModel WaterTextureFileName
    Texture lavaTex_;    // GridModel LavaTextureFileName
    Texture waterPal_;   // GridModel WaterPaletteFileName
    Texture lavaPal_;    // GridModel LavaPaletteFileName
    LightMap lightMap_;
    Clouds clouds_;
    SkyBox skyBox_;
    Font hudFont_;
    MeshBuffer hudMesh_;
};
