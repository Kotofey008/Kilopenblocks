#include "Game.h"
#include <cmath>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <strings.h> // strcasecmp

// Observed static world-size table (values still approximate; symbol s_WorldSizesHi size 72)
int Game::s_WorldSizesHi[] = { 128, 256, 384, 512, 640, 768, 896, 1024, 0 };
// Binary table @ VA 0x109e60 (GOT @ 0x12ce04), stride 12, levels 0..3
const int Game::s_DrawDistanceSize[4][3] = {
    {96, 128, 96},   // Tiny
    {128, 128, 128}, // Short
    {160, 128, 160}, // Normal
    {192, 128, 192}, // Far
};

Game::Game() {
    scene_.grid = &grid_;
}

Game::~Game() {}

void Game::InitUI() {
    // Original @ 0x66350 — loads UI scene objects + HUD/menu from assets
    // start.scene FileName "Screen/ui/hud.txt" / "Screen/ui/menu.txt" → assets/ui/
    scene_.Load("scenes/start.scene");
    ApplySceneConfigs();
    LoadUIPanelsFromScene();
    // Panels own the UIDocument; GetHud()/GetMenu() return panel docs.
    hudLoaded_ = false;
    menuLoaded_ = false;
    for (auto& p : uiPanels_) {
        if (p.sceneName == "hud_ui" && p.loaded) {
            hudLoaded_ = true;
            p.visible = true; // HUD visible at start
            p.doc.SetAllVisible(true);
        }
        if (p.sceneName == "menu_ui" && p.loaded) {
            menuLoaded_ = true;
            p.visible = false;
            p.doc.SetAllVisible(false);
        }
    }
    // If panel load missed, fall back to direct asset load into legacy members
    if (!hudLoaded_) {
        hudLoaded_ = hud_.LoadAsset("ui/hud.txt");
    }
    if (!menuLoaded_) {
        menuLoaded_ = menu_.LoadAsset("ui/menu.txt");
        if (menuLoaded_) menu_.SetAllVisible(false);
    }
    // Game::LoadFont — Sans sizes used by UI (font/font_N.txt)
    fonts_.FindOrLoad("Sans", 16);
    fonts_.FindOrLoad("Sans", 20);
    fonts_.FindOrLoad("Sans", 24);
    menuOpen_ = false;
}

// ---------------------------------------------------------------------------
// UISceneObject panels + ElementClickedMessage (from start.scene)
// Original: each UISceneObject has FileName + ElementClickedMessage[] strings.
// "menu_* SetVisible 1" matches scene object names (menu_ui), not only element
// name prefixes. Element clicks match the first token of each message.
// ---------------------------------------------------------------------------

void Game::LoadUIPanelsFromScene() {
    // Hard-coded from start.scene UISceneObject blocks (recovered, not invented).
    // Format: sceneName, asset path under assets/, click messages.
    struct Def {
        const char* sceneName;
        const char* asset;
        const char* const* msgs;
        int nmsgs;
    };
    static const char* hudMsgs[] = {
        "hud_menu;ClearInput;hud_* SetVisible 0;menu_* SetVisible 1;Game Call SetUINeedFastResponse",
        "hud_activate;EPS UpdateElement"
    };
    static const char* menuMsgs[] = {
        "frame*;ClearInput;hud_* SetVisible 1;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 0",
        "menu_new_terrain;ClearInput;menu_* SetVisible 0;new_terrain_* SetVisible 1;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 0;Game Call FinishScreenshot",
        "menu_full_version;ClearInput;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 1;select_terrain_* SetVisible 0;options_* SetVisible 0",
        "menu_select_terrain;ClearInput;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 1;options_* SetVisible 0;Game Call FinishScreenshot;Game Call EnterGameList",
        "menu_options;ClearInput;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 1;Game Call SetUINeedFastResponse",
        "menu_mtl_0;ClearInput;Game SelectPaletteItem 0;hud_* SetVisible 1;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 0",
        "menu_mtl_1;ClearInput;Game SelectPaletteItem 1;hud_* SetVisible 1;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 0",
        "menu_mtl_2;ClearInput;Game SelectPaletteItem 2;hud_* SetVisible 1;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 0",
        "menu_mtl_3;ClearInput;Game SelectPaletteItem 3;hud_* SetVisible 1;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 0",
        "menu_mtl_4;ClearInput;Game SelectPaletteItem 4;hud_* SetVisible 1;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 0",
        "menu_mtl_5;ClearInput;Game SelectPaletteItem 5;hud_* SetVisible 1;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 0",
        "menu_mtl_6;ClearInput;Game SelectPaletteItem 6;hud_* SetVisible 1;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 0",
        "menu_mtl_7;ClearInput;Game SelectPaletteItem 7;hud_* SetVisible 1;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 0",
        "menu_mtl_8;ClearInput;Game SelectPaletteItem 8;hud_* SetVisible 1;menu_* SetVisible 0;new_terrain_* SetVisible 0;full_version_* SetVisible 0;select_terrain_* SetVisible 0;options_* SetVisible 0",
        "menu_next_mtl_set;ClearInput;Game SelectPalettePage -2",
        "menu_prev_mtl_set;ClearInput;Game SelectPalettePage -1",
        "menu_mtl_set_0;Game SelectPalettePage 0",
        "menu_mtl_set_1;Game SelectPalettePage 1",
        "menu_mtl_set_2;Game SelectPalettePage 2",
        "menu_mtl_set_3;Game SelectPalettePage 3",
        "menu_mtl_set_4;Game SelectPalettePage 4",
        "menu_mtl_set_5;Game SelectPalettePage 5",
        "menu_mtl_set_6;Game SelectPalettePage 6"
    };
    static const char* newTerrainMsgs[] = {
        "frame*;ClearInput;menu_* SetVisible 1;new_terrain_* SetVisible 0;terrain_options_* SetVisible 0",
        "terrain_options;ClearInput;new_terrain_* SetVisible 0;terrain_options_* SetVisible 1",
        "terrain_create;ClearInput;hud_* SetVisible 1;menu_* SetVisible 0;new_terrain_* SetVisible 0;Game CreateElement"
    };
    static const char* fullVersionMsgs[] = {
        "frame*;ClearInput;menu_* SetVisible 1;full_version_* SetVisible 0"
    };
    static const char* selectTerrainMsgs[] = {
        "frame*;ClearInput;menu_* SetVisible 1;select_terrain_* SetVisible 0;terrain_info_* SetVisible 0;delete_terrain_* SetVisible 0",
        "play;ClearInput;hud_* SetVisible 1;select_terrain_* SetVisible 0;Game Call EnterGameList",
        "file;ClearInput;select_terrain_* SetVisible 0;terrain_info_* SetVisible 1",
        "prev;Game SelectElement 0",
        "next;Game SelectElement 1"
    };
    static const char* terrainInfoMsgs[] = {
        "frame*;ClearInput;select_terrain_* SetVisible 1;delete_terrain_* SetVisible 0;terrain_info_* SetVisible 0",
        "delete;ClearInput;select_terrain_* SetVisible 0;delete_terrain_* SetVisible 1;terrain_info_* SetVisible 0",
        "copy;ClearInput;select_terrain_* SetVisible 0;delete_terrain_* SetVisible 0;terrain_info_* SetVisible 0;Game CopyElement"
    };
    static const char* deleteTerrainMsgs[] = {
        "frame*;ClearInput;select_terrain_* SetVisible 1;delete_terrain_* SetVisible 0",
        "delete;ClearInput;select_terrain_* SetVisible 1;delete_terrain_* SetVisible 0;Game DeleteElement"
    };
    static const char* messageFrameMsgs[] = {
        "frame*;ClearInput;menu_* SetVisible 1;message_frame_* SetVisible 0"
    };
    static const char* optionsMsgs[] = {
        "frame*;ClearInput;menu_* SetVisible 1;game_options_* SetVisible 0;graphics_options_* SetVisible 0;options_* SetVisible 0",
        "controls_options;ClearInput;menu_* SetVisible 0;game_options_* SetVisible 0;graphics_options_* SetVisible 0;controls_options_* SetVisible 1;options_* SetVisible 0;Game Call EnterControlsSettings",
        "graphics_options;ClearInput;menu_* SetVisible 0;game_options_* SetVisible 0;graphics_options_* SetVisible 1;controls_options_* SetVisible 0;options_* SetVisible 0;Game Call EnterGraphicsSettings",
        "game_options;ClearInput;menu_* SetVisible 0;game_options_* SetVisible 1;graphics_options_* SetVisible 0;controls_options_* SetVisible 0;options_* SetVisible 0;Game Call EnterGameSettings",
        "fly_mode_toggle;ClearInput;hud_* SetVisible 1;menu_* SetVisible 0;game_options_* SetVisible 0;graphics_options_* SetVisible 0;controls_options_* SetVisible 0;options_* SetVisible 0;Game Call FlyModeToggle"
    };
    static const char* controlsMsgs[] = {
        "frame*;ClearInput;options_* SetVisible 1;controls_options_* SetVisible 0",
        "screen_size;Game Call UpdateScreenSize",
        "apply;ClearInput;hud_* SetVisible 1;options_* SetVisible 0;controls_options_* SetVisible 0;Game Call ApplyControlsSettings"
    };
    static const char* graphicsMsgs[] = {
        "frame*;ClearInput;options_* SetVisible 1;graphics_options_* SetVisible 0",
        "draw_distance;Game Call UpdateDrawDistance",
        "leaves_quality;Game Call UpdateLeavesQuality",
        "clouds;Game Call UpdateClouds",
        "animated_water;Game Call UpdateAnimatedWater",
        "apply;ClearInput;hud_* SetVisible 1;options_* SetVisible 0;graphics_options_* SetVisible 0;Game Call ApplyGraphicsSettings"
    };
    static const char* gameOptsMsgs[] = {
        "frame*;ClearInput;options_* SetVisible 1;game_options_* SetVisible 0",
        "apply;ClearInput;hud_* SetVisible 1;options_* SetVisible 0;game_options_* SetVisible 0;Game Call ApplyGameSettings"
    };

    static const Def defs[] = {
        { "hud_ui", "ui/hud.txt", hudMsgs, 2 },
        { "menu_ui", "ui/menu.txt", menuMsgs, (int)(sizeof(menuMsgs)/sizeof(menuMsgs[0])) },
        { "new_terrain_ui", "ui/menu_new_terrain.txt", newTerrainMsgs, 3 },
        { "full_version_ui", "ui/menu_full_version.txt", fullVersionMsgs, 1 },
        { "select_terrain_ui", "ui/menu_select_terrain.txt", selectTerrainMsgs, 5 },
        { "terrain_info_ui", "ui/menu_terrain_info.txt", terrainInfoMsgs, 3 },
        { "delete_terrain_ui", "ui/menu_delete_terrain.txt", deleteTerrainMsgs, 2 },
        { "message_frame_ui", "ui/menu_message_frame.txt", messageFrameMsgs, 1 },
        { "options_ui", "ui/menu_options.txt", optionsMsgs, 5 },
        { "controls_options_ui", "ui/menu_controls_options.txt", controlsMsgs, 3 },
        { "graphics_options_ui", "ui/menu_graphics_options.txt", graphicsMsgs, 6 },
        { "game_options_ui", "ui/menu_game_options.txt", gameOptsMsgs, 2 },
    };

    uiPanels_.clear();
    for (const auto& d : defs) {
        UIPanel panel;
        panel.sceneName = d.sceneName;
        panel.loaded = panel.doc.LoadAsset(d.asset);
        panel.visible = false;
        for (int i = 0; i < d.nmsgs; ++i)
            panel.clickMsgs.push_back(d.msgs[i]);
        uiPanels_.push_back(std::move(panel));
    }
}

bool Game::MatchNamePattern(const std::string& name, const std::string& pattern) const {
    // Wildcard: "frame*" matches "frame", "frame_background"; "menu_*" matches "menu_ui"
    if (pattern.empty()) return false;
    if (pattern.back() == '*') {
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        return name.size() >= prefix.size() &&
               name.compare(0, prefix.size(), prefix) == 0;
    }
    return name == pattern;
}

void Game::SetPanelVisiblePattern(const std::string& pattern, bool visible) {
    for (auto& p : uiPanels_) {
        if (MatchNamePattern(p.sceneName, pattern)) {
            p.visible = visible;
            // Element visibility so Draw skips hidden trees
            p.doc.SetAllVisible(visible);
        }
    }
    // Legacy fallback members if panels not used
    if (!uiPanels_.empty()) {
        SyncMenuOpenFromPanels();
        return;
    }
    if (MatchNamePattern("hud_ui", pattern) || pattern == "hud_*") {
        if (hudLoaded_) hud_.SetAllVisible(visible);
    }
    if (MatchNamePattern("menu_ui", pattern) || pattern == "menu_*") {
        if (menuLoaded_) menu_.SetAllVisible(visible);
    }
    SyncMenuOpenFromPanels();
}

void Game::SyncMenuOpenFromPanels() {
    // menuOpen_ when any non-hud panel is visible (blocks look/movement)
    bool any = false;
    for (const auto& p : uiPanels_) {
        if (!p.visible) continue;
        if (p.sceneName == "hud_ui") continue;
        any = true;
        break;
    }
    menuOpen_ = any;
    input_.SetMenuMode(any);
}

void Game::RunElementClickedMessage(const std::string& fullMsg) {
    // "elemPattern;cmd;cmd;..." — skip first token (already matched), run the rest
    size_t start = fullMsg.find(';');
    if (start == std::string::npos) return;
    std::string rest = fullMsg.substr(start + 1);
    size_t pos = 0;
    while (pos < rest.size()) {
        size_t semi = rest.find(';', pos);
        std::string cmd = (semi == std::string::npos) ? rest.substr(pos) : rest.substr(pos, semi - pos);
        // trim
        while (!cmd.empty() && (cmd.front() == ' ' || cmd.front() == '\t')) cmd.erase(cmd.begin());
        while (!cmd.empty() && (cmd.back() == ' ' || cmd.back() == '\t')) cmd.pop_back();
        if (!cmd.empty()) ParseAndRunCommand(cmd);
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
}

Game::UIPanel* Game::FindVisiblePanelHit(float x, float y, UIElement** outHit) {
    *outHit = nullptr;
    // Prefer non-hud panels (they draw on top); then hud
    for (auto& p : uiPanels_) {
        if (!p.visible || !p.loaded) continue;
        if (p.sceneName == "hud_ui") continue;
        p.doc.ResolveAll((float)screenW_, (float)screenH_);
        UIElement* hit = p.doc.HitTest(x, y);
        if (hit) {
            *outHit = hit;
            return &p;
        }
    }
    for (auto& p : uiPanels_) {
        if (!p.visible || !p.loaded) continue;
        if (p.sceneName != "hud_ui") continue;
        p.doc.ResolveAll((float)screenW_, (float)screenH_);
        UIElement* hit = p.doc.HitTest(x, y);
        if (hit) {
            *outHit = hit;
            return &p;
        }
    }
    return nullptr;
}

void Game::OpenMenu() {
    // start.scene hud_menu message: hud_* SetVisible 0; menu_* SetVisible 1
    SetPanelVisiblePattern("hud_*", false);
    SetPanelVisiblePattern("menu_*", true);
    SetPanelVisiblePattern("new_terrain_*", false);
    SetPanelVisiblePattern("full_version_*", false);
    SetPanelVisiblePattern("select_terrain_*", false);
    SetPanelVisiblePattern("options_*", false);
    SetPanelVisiblePattern("controls_options_*", false);
    SetPanelVisiblePattern("graphics_options_*", false);
    SetPanelVisiblePattern("game_options_*", false);
}

void Game::CloseMenu() {
    // start.scene frame* on menu_ui: hud_* 1; hide all menu panels
    SetPanelVisiblePattern("hud_*", true);
    SetPanelVisiblePattern("menu_*", false);
    SetPanelVisiblePattern("new_terrain_*", false);
    SetPanelVisiblePattern("full_version_*", false);
    SetPanelVisiblePattern("select_terrain_*", false);
    SetPanelVisiblePattern("options_*", false);
    SetPanelVisiblePattern("controls_options_*", false);
    SetPanelVisiblePattern("graphics_options_*", false);
    SetPanelVisiblePattern("game_options_*", false);
    SetPanelVisiblePattern("terrain_info_*", false);
    SetPanelVisiblePattern("delete_terrain_*", false);
    SetPanelVisiblePattern("message_frame_*", false);
    SetPanelVisiblePattern("terrain_options_*", false);
}

void Game::HandleTouchPress(int x, int y) {
    // Original: ElementClickedMessage dispatch on the UISceneObject that owns the hit element
    float fx = (float)x, fy = (float)y;
    UIElement* hit = nullptr;
    UIPanel* panel = FindVisiblePanelHit(fx, fy, &hit);

    // When a non-hud panel is open and miss → treat as frame* background close for that panel
    if (!panel || !hit) {
        if (menuOpen_) {
            // Find topmost visible non-hud panel and run its frame* message if any
            for (auto& p : uiPanels_) {
                if (!p.visible || p.sceneName == "hud_ui") continue;
                for (const auto& msg : p.clickMsgs) {
                    if (msg.compare(0, 6, "frame*") == 0) {
                        RunElementClickedMessage(msg);
                        return;
                    }
                }
            }
            CloseMenu();
        }
        return;
    }

    const std::string& n = hit->name;
    // Match ElementClickedMessage first token against element name (wildcard supported)
    for (const auto& msg : panel->clickMsgs) {
        size_t semi = msg.find(';');
        std::string pat = (semi == std::string::npos) ? msg : msg.substr(0, semi);
        if (MatchNamePattern(n, pat)) {
            RunElementClickedMessage(msg);
            return;
        }
    }
}


void Game::ApplySceneConfigs() {
    // EntityPlayerSet → player set + look sensitivity
    if (scene_.playerSet.present) {
        playerSet_.SetConfig(scene_.playerSet);
        input_.SetTouchSensitivity(scene_.playerSet.touchSensitivity);
    }
    // Camera projection (pose still follows player each frame)
    if (scene_.camera.present) {
        const SceneCameraConfig& cc = scene_.camera;
        camera_.SetPerspective(cc.fieldOfView, 1.777f, cc.nearPlane);
        camera_.SetFarPlane(cc.farPlane);
        camera_.SetViewportRect(cc.viewportX0, cc.viewportY0,
                                cc.viewportX1 - cc.viewportX0,
                                cc.viewportY1 - cc.viewportY0);
    }
    // GridModel → materials path, fluid flow speeds (LoadTextures paths applied in GLApplication)
    if (scene_.gridModel.present) {
        const SceneGridModelConfig& gm = scene_.gridModel;
        fluidAnim.SetFlowSpeeds(gm.waterFlowSpeed, gm.lavaFlowSpeed);
        // MtlDescFileName → GridData::LoadMaterials (binary GridModel owns this path)
        if (!gm.mtlDescFileName.empty()) {
            grid_.LoadMaterials(gm.mtlDescFileName.c_str());
        }
        // Plane colors stored for render path; full underwater plane mesh not yet restored
        waterPlaneR_ = gm.waterPlaneR; waterPlaneG_ = gm.waterPlaneG;
        waterPlaneB_ = gm.waterPlaneB; waterPlaneA_ = gm.waterPlaneA;
        lavaPlaneR_ = gm.lavaPlaneR; lavaPlaneG_ = gm.lavaPlaneG;
        lavaPlaneB_ = gm.lavaPlaneB; lavaPlaneA_ = gm.lavaPlaneA;
        useProceduralTextures_ = gm.useProceduralTextures;
    }
    timeMultiplier_ = scene_.globals.timeMultiplier;
    if (timeMultiplier_ < 0.01f) timeMultiplier_ = 0.01f;
}

void Game::InitSceneConnections() {
    // Original @ 0x66800 size 596 — connects UI message handlers to Game methods
}

void Game::Play(int slot) {
    // Original @ 0x6c720 size 2679 — major entry: load/create world, spawn player
    state_ = GameState::Loading;
    // Globals.WorldSize* with mobile XZ clamp (keep scene values in scene_.globals)
    scene_.ApplyGlobalsToGrid(grid_, /*mobileClamp=*/true);
    switch (terrainType_) {
        case TerrainType::Earth:
            genEarth_.Init(grid_, (int64_t)slot * 0x1234ABCD);
            genEarth_.Generate(grid_,
                (int)grid_.worldMin.x, (int)grid_.worldMin.z,
                (int)grid_.worldMax.x, (int)grid_.worldMax.z);
            break;
        case TerrainType::Flat:
            genFlat_.Init(grid_, 0);
            genFlat_.Generate(grid_,
                (int)grid_.worldMin.x, (int)grid_.worldMin.z,
                (int)grid_.worldMax.x, (int)grid_.worldMax.z);
            break;
        default:
            break;
    }
    SpawnPlayer();
    state_ = GameState::Playing;
}

void Game::Update() {
    if (state_ != GameState::Playing) return;
    // Resolve original HUD layout from ui/hud.txt
    if (hudLoaded_) {
        // screen size from input
        // (SetScreenSize already called by GLApplication)
        // We need W/H — store last from input layout
    }
    // Feed input to player — match binary order:
    //   UpdateRotation (apply look deltas + clamp/wrap)
    //   FixedUpdate / movement
    //   UpdateCameraTransform (GetRotationVectors → Camera basis + eye)
    if (player_) {
        if (input_.ConsumeMenuAction()) OpenMenu();
        if (!menuOpen_) {
            float ldx = input_.GetLookDeltaX();
            float ldy = -input_.GetLookDeltaY();
            if (scene_.playerSet.flipXRotation) ldx = -ldx;
            if (scene_.playerSet.flipYRotation) ldy = -ldy;
            player_->UpdateRotation(ldx, ldy);
            input_.ConsumeLookDelta();
            player_->SetMoveInput(input_.GetMoveForward(), input_.GetMoveStrafe(), input_.GetJump());
            if (input_.ConsumeBreakAction()) TryBreakBlock();
            if (input_.ConsumePlaceAction()) TryPlaceBlock(GetSelectedBlockType());
            if (input_.ConsumeActivateAction()) { /* EPS UpdateElement — TBD */ }
            player_->FixedUpdate();
            player_->FixedPostUpdate();
        } else {
            input_.ConsumeLookDelta();
            player_->SetMoveInput(0.f, 0.f, false);
        }
        fluidAnim.Update(1.f / 60.f);
        input_.EndFrame();
        player_->UpdateCameraTransform(camera_);
    }
    UpdateTime();
    UpdateSkyBox();
    UpdateHUDUI();
    UpdateScreenMessages();
}

void Game::SpawnPlayer() {
    int x, y, z;
    if (terrainType_ == TerrainType::Earth)
        genEarth_.GetStartPosition(x, y, z);
    else
        genFlat_.GetStartPosition(x, y, z);

    // Scan upward for feet in air above solid — never spawn inside terrain
    auto isSolid = [&](int bx, int by, int bz) -> bool {
        int idx = grid_.CoordToIndex(bx, by, bz);
        if (idx < 0) return false;
        unsigned char t = grid_.cellTypes[idx];
        return t != 0 && t != 5 && t != 11 && t != 10 && t != 20
            && t != 12 && t != 13 && t != 14 && t != 17 && t != 18;
    };
    int maxY = (int)grid_.worldMax.y - 2;
    int minY = (int)grid_.worldMin.y + 1;
    if (y < minY) y = minY;
    // If feet solid, climb; then ensure head room
    while (y < maxY && isSolid(x, y, z)) ++y;
    while (y > minY && !isSolid(x, y - 1, z)) --y; // drop onto ground
    if (isSolid(x, y, z)) ++y; // one more if still blocked
    // Head clearance
    while (y < maxY && (isSolid(x, y, z) || isSolid(x, y + 1, z))) ++y;

    if (!player_) {
        player_ = new EntityPlayer(&playerSet_);
        playerSet_.SetLocalPlayer(player_);
    }
    player_->SetGrid(&grid_);
    // Push EntityPlayerSet config (ParamSets, BBox, reach, eye height) onto player
    playerSet_.ApplyToLocalPlayer();
    player_->SetPosition(Vector((float)x + 0.5f, (float)y, (float)z + 0.5f));
    player_->Init();
    // Camera projection from start.scene Camera section (not hard-coded 70/0.1/256)
    if (scene_.camera.present) {
        camera_.SetPerspective(scene_.camera.fieldOfView, 1.777f, scene_.camera.nearPlane);
        camera_.SetFarPlane(scene_.camera.farPlane);
    } else {
        camera_.SetPerspective(90.f, 1.777f, 0.025f);
        camera_.SetFarPlane(350.f);
    }
    player_->UpdateCameraTransform(camera_);
}

void Game::OnForeground() {}
void Game::OnBackground() {}

void Game::SelectTerrainType(TerrainType t) {
    terrainType_ = t;
}

void Game::UpdateTerrainType() {
    UpdateParameter("UpdateTerrainType");
}

void Game::UpdateTerrainSize() {
    // Binary 0x65c90 — UI-driven; full body still open
}

// Game::Create @ 0x6a850 — DSL "Game CreateElement"
void Game::Create() {
    activeSaveSlot_ = 0; // this+0x1f0 = 0
    RemoveSaveFiles(activeSaveSlot_);
    Play(activeSaveSlot_);
}

void Game::CreateElement() { Create(); }

void Game::EnterGameList() {
    // Game::EnterGameList @ 0x6a110 (1584 B) — save scan + UpdateGameListUI
    // Full body not ported; keep list index consistent for SelectNext/Prev
    gameListIndex_ = 0;
}

void Game::InsertGame(int, int) {}

// ---------------------------------------------------------------------------
// Graphics / controls settings — recovered from binary (Select* @ 0x636f0..,
// UpdateParameter @ 0x67210, ApplyGraphicsSettings @ 0x641b0).
// Pending fields mirror binary offsets:
//   +0xd30 drawDistance pending, +0x84 applied
//   +0xd34 leavesQuality pending, +0x88 applied
//   +0xd38 clouds pending, +0x8c applied
//   +0xd39 animatedWater pending, +0x8d applied
// ---------------------------------------------------------------------------

static void SetPanelElementText(Game::UIPanel* panel, const char* elemName, const char* text) {
    if (!panel || !panel->loaded || !text) return;
    if (UIElement* e = panel->doc.Find(elemName))
        e->text = text;
}

static Game::UIPanel* FindPanelBySceneName(std::vector<Game::UIPanel>& panels, const char* name) {
    for (auto& p : panels)
        if (p.sceneName == name) return &p;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Settings — dual storage + tables recovered from binary
// Applied: +0x84 draw, +0x88 leaves, +0x8c clouds, +0x8d water
// Pending: +0xd30, +0xd34, +0xd38, +0xd39 (written by Select*)
// Size table @ 0x109e60: per draw-distance {x,y,z} half-extents
// ---------------------------------------------------------------------------

// Game::SelectDrawDistance @ 0x636f0 — writes pending +0xd30, updates UI label
void Game::SelectDrawDistance(DrawDistance d) {
    if ((int)d < 0) d = DrawDistance::Tiny;
    if (d >= DrawDistance::Count) d = DrawDistance::Far;
    drawDistancePending_ = d; // +0xd30
    const char* label = "/DrawDistanceTiny/";
    switch (d) {
        case DrawDistance::Short:  label = "/DrawDistanceShort/"; break;
        case DrawDistance::Normal: label = "/DrawDistanceNormal/"; break;
        case DrawDistance::Far:    label = "/DrawDistanceFar/"; break;
        default: break;
    }
    if (UIPanel* p = FindPanelBySceneName(uiPanels_, "graphics_options_ui"))
        SetPanelElementText(p, "draw_distance", label);
}

// Game::SelectLeavesQuality @ 0x638a0 — pending +0xd34
void Game::SelectLeavesQuality(GridData::LeavesQuality q) {
    leavesQualityPending_ = q;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Leaves %d", (int)q);
    if (UIPanel* p = FindPanelBySceneName(uiPanels_, "graphics_options_ui"))
        SetPanelElementText(p, "leaves_quality", buf);
}

// Game::SelectClouds @ 0x63a20 — pending +0xd38
void Game::SelectClouds(bool on) {
    cloudsPending_ = on;
    if (UIPanel* p = FindPanelBySceneName(uiPanels_, "graphics_options_ui"))
        SetPanelElementText(p, "clouds", on ? "/CloudsOn/" : "/CloudsOff/");
}

// Game::SelectAnimatedWater @ 0x63d50 — pending +0xd39
void Game::SelectAnimatedWater(bool on) {
    animatedWaterPending_ = on;
    if (UIPanel* p = FindPanelBySceneName(uiPanels_, "graphics_options_ui"))
        SetPanelElementText(p, "animated_water", on ? "/AnimatedWaterOn/" : "/AnimatedWaterOff/");
}

// Game::EnableClouds @ 0x63b80 — iterate scene Clouds (IsKindOf 5) → SetEnabled
void Game::EnableClouds(bool on) {
    cloudsEnabled_ = on; // applied +0x8c
    // Binary walks ApplicationBase object list; host path: flag consumed by GLApplication
}

// Game::EnableAnimatedWater @ 0x63eb0 — GridModel::SetAnimatedWater on this+0xe7c
void Game::EnableAnimatedWater(bool on) {
    animatedWater_ = on; // applied +0x8d
    SetAnimatedWater(on);
}

// Game::UpdateParameter @ 0x67210
void Game::UpdateParameter(const char* name) {
    if (!name) return;
    if (::strcasecmp(name, "UpdateDrawDistance") == 0) {
        int cur = (int)drawDistancePending_; // +0xd30
        if (cur == 2)
            SelectDrawDistance(DrawDistance::Short);
        else if (cur == 3)
            SelectDrawDistance(DrawDistance::Normal);
        else if (cur == 1)
            SelectDrawDistance(DrawDistance::Tiny);
        else {
            if (screenW_ <= 0x27f)
                SelectDrawDistance(DrawDistance::Normal);
            else
                SelectDrawDistance(DrawDistance::Far);
        }
        return;
    }
    if (::strcasecmp(name, "UpdateLeavesQuality") == 0) {
        int cur = (int)leavesQualityPending_;
        if (cur == 0)
            SelectLeavesQuality(GridData::LeavesQuality::Medium);
        else if (cur == 1)
            SelectLeavesQuality(GridData::LeavesQuality::High);
        else
            SelectLeavesQuality(GridData::LeavesQuality::Low);
        return;
    }
    if (::strcasecmp(name, "UpdateClouds") == 0) {
        SelectClouds(!cloudsPending_);
        return;
    }
    if (::strcasecmp(name, "UpdateAnimatedWater") == 0) {
        SelectAnimatedWater(!animatedWaterPending_);
        return;
    }
    if (::strcasecmp(name, "UpdateTerrainType") == 0) {
        int t = (int)terrainType_;
        // Binary branches at 0x67249: terrain type at +0x234
        if (t == 1)
            SelectTerrainType(TerrainType::Test);
        else if (t == 2)
            SelectTerrainType(TerrainType::Earth);
        else
            SelectTerrainType(TerrainType::Flat);
        return;
    }
    if (::strcasecmp(name, "UpdateSensitivity") == 0) {
        pendingSensitivity_ = sensitivity_;
        return;
    }
    if (::strcasecmp(name, "UpdateScreenSize") == 0) {
        pendingScreenSize_ = (pendingScreenSize_ + 1) % 4;
        return;
    }
    if (::strcasecmp(name, "UpdateTimeOfDay") == 0) {
        UpdateTimeOfDay();
        return;
    }
}

void Game::UpdateDrawDistance() { UpdateParameter("UpdateDrawDistance"); }
void Game::UpdateLeavesQuality() { UpdateParameter("UpdateLeavesQuality"); }
void Game::UpdateClouds() { UpdateParameter("UpdateClouds"); }
void Game::UpdateAnimatedWater() { UpdateParameter("UpdateAnimatedWater"); }

// Game::ApplyGraphicsSettings @ 0x641b0
void Game::ApplyGraphicsSettings() {
    bool dirty = false;

    // --- Draw distance change: pending +0xd30 vs applied +0x84 ---
    if (drawDistancePending_ != drawDistance_) {
        int level = (int)drawDistancePending_;
        if (level < 0) level = 0;
        if (level > 3) level = 3;
        const int hx = s_DrawDistanceSize[level][0];
        const int hy = s_DrawDistanceSize[level][1];
        const int hz = s_DrawDistanceSize[level][2];
        // Current world half-extents from bounds
        int curHx = (int)((grid_.worldMax.x - grid_.worldMin.x) * 0.5f);
        int curHz = (int)((grid_.worldMax.z - grid_.worldMin.z) * 0.5f);
        // Binary compares table x/z with stored size fields; if mismatch → Store + Resize
        if (hx != curHx || hz != curHz) {
            grid_.Store();
            // Resize args recovered from stack setup around 0x64288:
            // min/max derived from ± table extents (y uses table y / world y)
            int minX = -hx, maxX = hx;
            int minZ = -hz, maxZ = hz;
            int minY = (int)grid_.worldMin.y;
            int maxY = (int)grid_.worldMax.y;
            // Prefer table y if it defines full height band
            if (hy > 0) {
                minY = -hy / 2;
                maxY = hy / 2;
            }
            bool ok = grid_.Resize(minX, minY, minZ, maxX, maxY, maxZ, 0);
            if (ok) {
                drawDistance_ = drawDistancePending_; // +0x84 = +0xd30
                // view distance cells: (table.x - 0x20) / 2  @ grid+0x22c
                int vd = (hx - 0x20) / 2;
                if (vd < 1) vd = 1;
                grid_.SetViewDistanceCells(vd);
                // Restore near local player
                if (EntityPlayer* pl = playerSet_.GetLocalPlayer())
                    grid_.Restore(pl->GetPosition());
                grid_.InvalidateLighting();
            } else {
                // Binary retries with lower pending levels (decrement +0xd30) — 0x642cc
                while (level > 0) {
                    --level;
                    drawDistancePending_ = (DrawDistance)level;
                    const int hx2 = s_DrawDistanceSize[level][0];
                    const int hz2 = s_DrawDistanceSize[level][2];
                    if (grid_.Resize(-hx2, minY, -hz2, hx2, maxY, hz2, 0)) {
                        drawDistance_ = drawDistancePending_;
                        grid_.SetViewDistanceCells((hx2 - 0x20) / 2);
                        if (EntityPlayer* pl = playerSet_.GetLocalPlayer())
                            grid_.Restore(pl->GetPosition());
                        grid_.InvalidateLighting();
                        break;
                    }
                }
            }
        } else {
            drawDistance_ = drawDistancePending_;
        }
        dirty = true;
        // ShowMessage with localized draw-distance name (0x64340 path) — optional
        UpdateSkyBox();
    }

    // --- Leaves: pending +0xd34 vs applied +0x88 ---
    if (leavesQualityPending_ != leavesQualityApplied_) {
        leavesQualityApplied_ = leavesQualityPending_;
        grid_.SetLeavesQuality(leavesQualityApplied_);
        dirty = true;
    }

    // --- Clouds: pending +0xd38 vs applied +0x8c ---
    if (cloudsPending_ != cloudsEnabled_) {
        EnableClouds(cloudsPending_);
        dirty = true;
    }

    // --- Animated water: pending +0xd39 vs applied +0x8d ---
    if (animatedWaterPending_ != animatedWater_) {
        EnableAnimatedWater(animatedWaterPending_);
        dirty = true;
    }

    // Always save settings at end of ApplyGraphics (0x643e2)
    UpdateAndSaveGameSettings(state_);
}

// Game::ApplyGameSettings @ 0x64600
void Game::ApplyGameSettings() {
    UpdateSkyBox();
    UpdateAndSaveGameSettings(state_);
}

// Game::ApplyControlsSettings @ 0x64070
void Game::ApplyControlsSettings() {
    if (screenSizeEnum_ != pendingScreenSize_) {
        screenSizeDirty_ = true;
        screenSizeEnum_ = pendingScreenSize_;
    }
    if (pendingSensitivity_ != sensitivity_) {
        sensitivity_ = pendingSensitivity_;
        float s = sensitivity_;
        if (s > 0.f && s < 0.05f) s = 1.f / s;
        if (s < 0.01f) s = 0.01f;
        if (s > 5.f) s = 5.f;
        input_.SetTouchSensitivity(s);
    }
    UpdateAndSaveGameSettings(state_);
}

void Game::UpdateTimeOfDay() {}
void Game::UpdateSensitivity() { UpdateParameter("UpdateSensitivity"); }
void Game::UpdateSkyBox() {}
void Game::UpdateTime() {
    float step = 0.0001f * timeMultiplier_;
    timeOfDay_ = std::fmod(timeOfDay_ + step, 1.f);
}

void Game::SelectPaletteItem(int index) {
    if (index < 0) index = 0;
    int pageBase = palettePage_ * 9;
    if (index <= 8)
        paletteItem_ = pageBase + index;
    else
        paletteItem_ = index;
}

void Game::SelectPalettePage(int page, int) {
    if (page == -2) palettePage_ += 1;
    else if (page == -1) palettePage_ -= 1;
    else if (page >= 0) palettePage_ = page;
    if (palettePage_ < 0) palettePage_ = 0;
    if (palettePage_ > 20) palettePage_ = 20;
    int slot = paletteItem_ % 9;
    paletteItem_ = palettePage_ * 9 + slot;
}

void Game::SelectNext() {
    int next = gameListIndex_ + 1;
    if (next < gameListCount_)
        gameListIndex_ = next;
}
void Game::SelectPrev() {
    if (gameListIndex_ > 0)
        gameListIndex_ -= 1;
}
void Game::FlyModeToggle() {
    SetFlying(!playerSet_.GetFlying());
}
void Game::SetFlying(bool on) {
    playerSet_.SetFlying(on);
}

void Game::ShowElement(UISceneObject* ui, const char* name, bool visible) {
    if (!ui) return;
    // match wildcard patterns like hud_* 
    if (name && ui->name.find(name) != std::string::npos)
        ui->visible = visible;
}
void Game::SetText(UISceneObject*, const char*, const char*) {}
void Game::SetValue(UISceneObject*, const char*, float) {}
bool Game::GetValue(UISceneObject*, const char*, float& out) { out = 0; return false; }
void Game::ShowMessage(const char* text, MessagePriority) {
    if (text) std::cout << "[MSG] " << text << std::endl;
}
void Game::UpdateScreenMessages() {}
void Game::UpdateHUDUI() {
    // Sync UIElement::pressed / selected from input + palette state.
    // Original: UI state machine drives State[0]/State[1] for DrawBackground.
    // Here we set the flags; Draw uses CurrentStyle() (Default/Pressed/Selected*).

    // --- HUD: pressed from held discrete buttons ---
    UIDocument& hudDoc = GetHud();
    if (hudLoaded_ && !menuOpen_) {
        static const char* kHudBtns[] = {
            "hud_up", "hud_down", "hud_left", "hud_right",
            "hud_jump", "hud_delete", "hud_add", "hud_activate", "hud_menu"
        };
        for (const char* n : kHudBtns) {
            if (UIElement* e = hudDoc.Find(n))
                e->pressed = input_.IsHudButtonHeld(n);
        }
    } else if (hudLoaded_) {
        for (auto& e : hudDoc.All()) e.pressed = false;
    }

    // --- Menu / palette: selected slot + page tab ---
    if (!menuLoaded_) return;

    const int selSlot = paletteItem_ % 9;   // 0..8 within page
    const int selPage = palettePage_;       // menu_mtl_set_N

    UIDocument& menuDoc = GetMenu();
    for (auto& e : menuDoc.All()) {
        e.pressed = false;
        e.selected = false;

        // menu_mtl_0..8 → selected when slot matches current palette item
        if (e.name.compare(0, 9, "menu_mtl_") == 0 && e.name.size() == 10) {
            int slot = e.name[9] - '0';
            if (slot >= 0 && slot <= 8)
                e.selected = (slot == selSlot);
            continue;
        }
        // menu_mtl_set_0..6 → selected page tab (State[1] fill)
        if (e.name.compare(0, 13, "menu_mtl_set_") == 0) {
            int page = std::atoi(e.name.c_str() + 13);
            e.selected = (page == selPage);
            continue;
        }
    }
}
// GLApplication::SetUINeedFastResponse @ 0x6ff50 (16 B) — flag for tight UI frame
void Game::SetUINeedFastResponse() {
    // Original sets a one-shot fast-response flag on GL app; no local state yet
}

// Scene::FinishScreenshot @ 0xd3e20 (16 B) — completes async screenshot capture
void Game::FinishScreenshot() {
    // Original clears screenshot-pending on Scene; no-op until capture path restored
}

void Game::ShowProgressScreen(const char* msg) { ShowMessage(msg, MessagePriority::Normal); }
void Game::HideProgressScreen() {}

// Game::EnterGameSettings @ 0x66e20 — push time-of-day into game_options UI slider
void Game::EnterGameSettings() {
    // Binary: FindObject game_options_ui, GetTimesOfDay, SetValue on slider, UpdateTimeOfDay
    UpdateTimeOfDay();
}

// Game::EnterControlsSettings @ 0x66f00 — push sensitivity into controls UI
void Game::EnterControlsSettings() {
    // Binary: FindObject controls_options_ui, SetValue sensitivity slider
    pendingSensitivity_ = sensitivity_;
}

// Game::EnterGraphicsSettings @ 0x64130
void Game::EnterGraphicsSettings() {
    // 0x64130: Select*(applied fields +0x84/+0x88/+0x8c/+0x8d) → fills pending + UI
    SelectDrawDistance(drawDistance_);
    SelectLeavesQuality(leavesQualityApplied_);
    SelectClouds(cloudsEnabled_);
    SelectAnimatedWater(animatedWater_);
}

// Game::OnScreenClicked @ 0x69ce0 — progress screen dismiss
void Game::ScreenClicked() {
    HideProgressScreen();
}

// Game::Copy @ 0x6a900 / Delete @ 0x6b3c0 — save slot ops (bodies large; structure only)
void Game::Copy() {
    // Binary copies save files for current gameListIndex_
}

void Game::Delete() {
    // Binary deletes save at gameListIndex_, refreshes list
    if (gameListCount_ > 0 && gameListIndex_ >= 0 && gameListIndex_ < gameListCount_) {
        RemoveSaveFiles(gameListIndex_);
        if (gameListIndex_ >= gameListCount_ - 1 && gameListIndex_ > 0)
            gameListIndex_ -= 1;
        if (gameListCount_ > 0) gameListCount_ -= 1;
    }
}

// --- Message dispatcher ---
void Game::ParseAndRunCommand(const std::string& cmd) {
    std::string s = cmd;
    // trim
    auto a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return;
    s = s.substr(a);

    if (s == "ClearInput") return;

    // "Game Call MethodName"
    if (s.compare(0, 10, "Game Call ") == 0) {
        std::string method = s.substr(10);
        if (method == "UpdateDrawDistance") UpdateDrawDistance();
        else if (method == "UpdateLeavesQuality") UpdateLeavesQuality();
        else if (method == "UpdateClouds") UpdateClouds();
        else if (method == "UpdateAnimatedWater") UpdateAnimatedWater();
        else if (method == "ApplyGraphicsSettings") ApplyGraphicsSettings();
        else if (method == "ApplyGameSettings") ApplyGameSettings();
        else if (method == "UpdateTimeOfDay") UpdateTimeOfDay();
        else if (method == "UpdateSensitivity") UpdateSensitivity();
        else if (method == "UpdateTerrainType") UpdateTerrainType();
        else if (method == "UpdateTerrainSize") UpdateTerrainSize();
        else if (method == "SetUINeedFastResponse") SetUINeedFastResponse();
        else if (method == "FinishScreenshot") FinishScreenshot();
        else if (method == "EnterGameList") EnterGameList();
        else if (method == "EnterGameSettings") EnterGameSettings();
        else if (method == "EnterControlsSettings") EnterControlsSettings();
        else if (method == "EnterGraphicsSettings") EnterGraphicsSettings();
        else if (method == "FlyModeToggle") FlyModeToggle();
        else if (method == "ApplyControlsSettings") ApplyControlsSettings();
        else if (method == "UpdateScreenSize") {
            // Game::SelectScreenSize path — cycle pendingScreenSize_
            pendingScreenSize_ = (pendingScreenSize_ + 1) % 4;
        }
        else if (method == "ScreenClicked") ScreenClicked();
        else if (method == "UpdateSkyBox") UpdateSkyBox();
        else {
            // unknown Game Call
        }
        return;
    }

    // "Game SelectPaletteItem N"
    if (s.compare(0, 23, "Game SelectPaletteItem ") == 0) {
        int idx = std::atoi(s.c_str() + 23);
        SelectPaletteItem(idx);
        return;
    }

    // "Game SelectPalettePage N" or "Game SelectPalettePage N M"
    if (s.compare(0, 23, "Game SelectPalettePage ") == 0) {
        int page = 0, arg = 0;
        std::sscanf(s.c_str() + 23, "%d %d", &page, &arg);
        SelectPalettePage(page, arg);
        return;
    }

    // "Game CreateElement"
    if (s == "Game CreateElement") {
        CreateElement();
        return;
    }

    // "Game Call EnterGraphicsSettings" already covered; extra Game Calls from scene
    if (s.compare(0, 10, "Game Call ") == 0) {
        // already handled above
        return;
    }
    if (s == "Game CopyElement") { Copy(); return; }
    if (s == "Game DeleteElement") { Delete(); return; }
    if (s.compare(0, 18, "Game SelectElement ") == 0) {
        // start.scene: prev → SelectElement 0, next → SelectElement 1
        int dir = std::atoi(s.c_str() + 18);
        if (dir == 0) SelectPrev();
        else SelectNext();
        return;
    }

    // Visibility: "hud_* SetVisible 0" / "options_* SetVisible 1"
    // Original targets UISceneObject names (menu_ui, options_ui, …), not only elements.
    auto pos = s.find(" SetVisible ");
    if (pos != std::string::npos) {
        std::string pattern = s.substr(0, pos);
        bool vis = (s.find(" SetVisible 1") != std::string::npos);
        SetPanelVisiblePattern(pattern, vis);
        return;
    }
}

void Game::DispatchMessage(const std::string& msg) {
    // Format from assets: "element_selector;cmd1;cmd2;..."
    // First segment is the UI element name that triggered the message; remaining are commands.
    std::stringstream ss(msg);
    std::string part;
    bool first = true;
    while (std::getline(ss, part, ';')) {
        if (first) { first = false; continue; }
        ParseAndRunCommand(part);
    }
}

void Game::DispatchMessageList(const std::vector<std::string>& msgs) {
    for (auto& m : msgs) DispatchMessage(m);
}

#include "Stream.h"
#include <cstdarg>
#include <cstdio>

void Game::GetSaveName(char* out, const char* fmt, ...) {
    // Observed @ 0x65590 — builds path starting with "/Hom..." (likely app files dir)
    // Uses varargs format. Simplified portable version:
    if (!out) return;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(out, 512, fmt ? fmt : "save_%d.dat", ap);
    va_end(ap);
}

void Game::RemoveSaveFiles(int slot) {
    char name[512];
    GetSaveName(name, "save_%d.dat", slot);
    std::remove(name);
}

void Game::SaveGameHeader(GameHeader& hdr, Stream& stream) {
    // Sequential 4-byte LE fields matching ByteOrderSerialize calls @ 0x6c030
    stream.Ser(hdr.version);
    stream.Ser(hdr.worldSizeX);
    stream.Ser(hdr.worldSizeY);
    stream.Ser(hdr.worldSizeZ);
    stream.Ser(hdr.terrainType);
    stream.Ser(hdr.seed);
    stream.Ser(hdr.flags);
}

void Game::LoadGameHeader(GameHeader& hdr, Stream& stream) {
    stream.Ser(hdr.version);
    stream.Ser(hdr.worldSizeX);
    stream.Ser(hdr.worldSizeY);
    stream.Ser(hdr.worldSizeZ);
    stream.Ser(hdr.terrainType);
    stream.Ser(hdr.seed);
    stream.Ser(hdr.flags);
}

void Game::SaveGameSettings(GameSettings& settings, Stream& stream) {
    stream.Ser(settings.drawDistance);
    stream.Ser(settings.leavesQuality);
    stream.Ser(settings.clouds);
    stream.Ser(settings.animatedWater);
    stream.Ser(settings.sensitivity);
    stream.Ser(settings.timeOfDay);
}

void Game::UpdateAndSaveGameHeader() {
    GameHeader hdr;
    hdr.worldSizeX = (int)(grid_.worldMax.x - grid_.worldMin.x);
    hdr.worldSizeY = (int)(grid_.worldMax.y - grid_.worldMin.y);
    hdr.worldSizeZ = (int)(grid_.worldMax.z - grid_.worldMin.z);
    hdr.terrainType = (int)terrainType_;
    char path[512];
    GetSaveName(path, "header.dat");
    FileStream fs;
    if (fs.Open(path, FileStream::Mode::Write))
        SaveGameHeader(hdr, fs);
}

void Game::UpdateAndSaveGameSettings(GameState) {
    GameSettings s;
    s.drawDistance = (int)drawDistance_;
    s.leavesQuality = 2;
    s.clouds = cloudsEnabled_ ? 1 : 0;
    s.animatedWater = animatedWater_ ? 1 : 0;
    s.sensitivity = sensitivity_;
    s.timeOfDay = timeOfDay_;
    char path[512];
    GetSaveName(path, "settings.dat");
    FileStream fs;
    if (fs.Open(path, FileStream::Mode::Write))
        SaveGameSettings(s, fs);
}

void Game::SaveScreenshot() {
    // Observed — captures GL framebuffer; not yet reconstructed
}

void Game::SetAnimatedWater(bool on) {
    fluidAnim.enabled = on;
}

void Game::TryBreakBlock() {
    if (!player_) return;
    player_->AddOrDeleteBlock(true, 0);
}

void Game::TryPlaceBlock(unsigned char blockType) {
    if (!player_) return;
    player_->AddOrDeleteBlock(false, blockType);
}

// Palette selection index is S-Index (blocks.txt column) stored in Material+0x20.
// Binary path: SelectPaletteItem → EntityPlayerSet.SelectMaterial → MtlSelIndexToCell.
// menu_mtl_N selects absolute palette slot; page offsets handled by SelectPalettePage.
unsigned char Game::GetSelectedBlockType() const {
    // Combined index: page base + item (SelectPaletteItem adds page base at +0xdac)
    int sel = paletteItem_;
    if (sel < 0) sel = 0;
    unsigned char cell = grid_.MtlSelIndexToCell(sel);
    if (cell != 0) return cell;
    // Fallback: if materials not loaded or S-Index -1 (air/water headers), place stone
    return 1;
}

bool Game::SaveWorld(int slot) {
    char path[256];
    GetSaveName(path, "world_%d.elw", slot);
    UpdateAndSaveGameHeader();
    return grid_.SaveWorld(path);
}
bool Game::LoadWorld(int slot) {
    char path[256];
    GetSaveName(path, "world_%d.elw", slot);
    if (!grid_.LoadWorld(path)) return false;
    // re-spawn player above terrain
    SpawnPlayer();
    return true;
}
