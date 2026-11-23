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

// Binary MeshBuffer has 7 layers (GetTotalIndices loops i=0..6).
// GridData::Draw → DrawMeshes(layer):
//   0: opaque (MBI=0) — no alpha test, no blend
//   1: cutout  (MBI=1) — glAlphaFunc(GEQUAL, 0.5), no blend
//   2: translucent (MBI=2) — depth-only then SRC_ALPHA/ONE_MINUS_SRC_ALPHA
//   3: water still  (MBI=3) — fluid texture, animated
//   4: water flow   (MBI=4)
//   5: lava still   (MBI=5)
//   6: lava flow    (MBI=6)
// PushQuads(this+0x90, MBI, ...) tags faces with MBI as layer index.
struct RegionMesh {
    int ox = 0, oy = 0, oz = 0;
    // layers[MBI] — binary 7-slot MeshBuffer (MBI 0..6)
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
