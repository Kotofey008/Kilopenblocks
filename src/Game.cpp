#include "Game.h"
#include <cmath>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

// Observed static world-size table (values still approximate; symbol s_WorldSizesHi size 72)
int Game::s_WorldSizesHi[] = { 128, 256, 384, 512, 640, 768, 896, 1024, 0 };

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
    // called from UI; cycle or read current selection
}

void Game::UpdateTerrainSize() {}
void Game::CreateElement() {
    // "Game CreateElement" — start new terrain generation
    Play(0);
}
void Game::EnterGameList() {}
void Game::InsertGame(int, int) {}

void Game::SelectDrawDistance(DrawDistance d) { drawDistance_ = d; }
void Game::UpdateDrawDistance() {}
void Game::SelectLeavesQuality(GridData::LeavesQuality q) { grid_.SetLeavesQuality(q); }
void Game::UpdateLeavesQuality() {}
void Game::EnableClouds(bool on) { cloudsEnabled_ = on; }
void Game::UpdateClouds() {}
void Game::UpdateAnimatedWater() {}
void Game::ApplyGraphicsSettings() {}
void Game::ApplyGameSettings() {}
void Game::UpdateTimeOfDay() { /* driven by UpdateTime each frame */ }
void Game::UpdateSensitivity() {}
void Game::UpdateSkyBox() { /* SkyBox owned by GLApplication */ }
void Game::UpdateTime() {
    // Globals.TimeMultiplier scales day cycle (scene default 1)
    float step = 0.0001f * timeMultiplier_;
    timeOfDay_ = std::fmod(timeOfDay_ + step, 1.f);
}

void Game::SelectPaletteItem(int index) {
    // Binary: +0xdac = arg + pageBase (+0xdb0). Slot 0..8 within current page.
    if (index < 0) index = 0;
    if (index > 8) {
        // absolute index from messages
        paletteItem_ = index;
        return;
    }
    paletteItem_ = palettePage_ * 9 + index;
}

void Game::SelectPalettePage(int page, int) {
    // start.scene specials: -2 = next set, -1 = prev set
    if (page == -2) {
        palettePage_ += 1;
    } else if (page == -1) {
        palettePage_ -= 1;
    } else if (page >= 0) {
        palettePage_ = page;
    }
    if (palettePage_ < 0) palettePage_ = 0;
    // Clamp to plausible S-Index range (blocks.txt has ~70 placeables)
    if (palettePage_ > 20) palettePage_ = 20;
    // Keep selection on same slot within new page
    int slot = paletteItem_ % 9;
    paletteItem_ = palettePage_ * 9 + slot;
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
void Game::SetUINeedFastResponse() {}
void Game::FinishScreenshot() {}
void Game::ShowProgressScreen(const char* msg) { ShowMessage(msg, MessagePriority::Normal); }
void Game::HideProgressScreen() {}
void Game::EnterGameSettings() {}
void Game::EnterControlsSettings() {}
void Game::ScreenClicked() {}

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
        else if (method == "EnterGraphicsSettings") { /* panel switch already done via SetVisible */ }
        else if (method == "FlyModeToggle") {
            // EntityPlayer fly mode — wire when player modes restored
        }
        else if (method == "ApplyControlsSettings") { /* stub */ }
        else if (method == "UpdateScreenSize") { /* stub */ }
        else if (method == "ScreenClicked") ScreenClicked();
        else if (method == "UpdateSkyBox") UpdateSkyBox();
        else {
            // unknown — leave for further recovery
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
    if (s == "Game CopyElement" || s == "Game DeleteElement") {
        // Save-slot ops — stub until save UI recovered
        return;
    }
    if (s.compare(0, 18, "Game SelectElement ") == 0) {
        // select_terrain prev/next — stub
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
