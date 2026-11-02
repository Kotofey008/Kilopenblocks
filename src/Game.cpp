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
    hudLoaded_ = hud_.LoadAsset("ui/hud.txt");
    menuLoaded_ = menu_.LoadAsset("ui/menu.txt");
    // Game::LoadFont — Sans sizes used by UI (font/font_N.txt)
    fonts_.FindOrLoad("Sans", 16);
    fonts_.FindOrLoad("Sans", 20);
    fonts_.FindOrLoad("Sans", 24);
    if (menuLoaded_) {
        menu_.SetAllVisible(false); // menu starts hidden (Visible:Bool=0 on menu_ui)
    }
    menuOpen_ = false;
}

void Game::OpenMenu() {
    // start.scene: hud_menu → ClearInput; hud_* SetVisible 0; menu_* SetVisible 1
    menuOpen_ = true;
    input_.SetMenuMode(true);
    if (hudLoaded_) hud_.SetAllVisible(false);
    if (menuLoaded_) menu_.SetAllVisible(true);
}

void Game::CloseMenu() {
    menuOpen_ = false;
    input_.SetMenuMode(false);
    if (hudLoaded_) hud_.SetAllVisible(true);
    if (menuLoaded_) menu_.SetAllVisible(false);
}

void Game::HandleTouchPress(int x, int y) {
    // Menu: ElementClickedMessage for palette / close (start.scene menu_ui)
    if (!menuOpen_ || !menuLoaded_) return;
    // Screen size from input
    // Resolve then hit-test
    // (Resolve uses last known design; Frame also resolves — use input screen)
    // Use a conservative 800x480 if unknown — Frame keeps input size updated.
    UIElement* hit = menu_.HitTest((float)x, (float)y);
    if (!hit) {
        // Tap empty area closes inventory (common mobile UX; binary uses frame* click)
        CloseMenu();
        return;
    }
    const std::string& n = hit->name;
    if (n.compare(0, 9, "menu_mtl_") == 0 && n.size() > 9) {
        int idx = std::atoi(n.c_str() + 9);
        SelectPaletteItem(idx);
        // start.scene: ClearInput; SelectPaletteItem; hud visible; menu hidden
        CloseMenu();
        return;
    }
    if (n == "menu_next_mtl_set") {
        SelectPalettePage(palettePage_ - 2, 0);
        return;
    }
    if (n == "menu_prev_mtl_set") {
        SelectPalettePage(palettePage_ - 1, 0);
        return;
    }
    // Other menu_* buttons: keep menu open for now (options/new terrain not fully restored)
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

void Game::SelectPaletteItem(int index) { paletteItem_ = index; }
void Game::SelectPalettePage(int page, int) { palettePage_ = page; }

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
void Game::UpdateHUDUI() {}
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
        else if (method == "ScreenClicked") ScreenClicked();
        else if (method == "UpdateSkyBox") UpdateSkyBox();
        else {
            // unknown — log for further recovery
            // std::cerr << "Unhandled Game Call: " << method << std::endl;
        }
        return;
    }

    // "Game SelectPaletteItem N"
    if (s.compare(0, 23, "Game SelectPaletteItem ") == 0) {
        int idx = std::atoi(s.c_str() + 23);
        SelectPaletteItem(idx);
        return;
    }

    // "Game SelectPalettePage N M"
    if (s.compare(0, 23, "Game SelectPalettePage ") == 0) {
        int page = 0, arg = 0;
        std::sscanf(s.c_str() + 22, "%d %d", &page, &arg);
        SelectPalettePage(page, arg);
        return;
    }

    // "Game CreateElement"
    if (s == "Game CreateElement") {
        CreateElement();
        return;
    }

    // Visibility: "hud_* SetVisible 0" / "menu_* SetVisible 1"
    auto pos = s.find(" SetVisible ");
    if (pos != std::string::npos) {
        std::string prefix = s.substr(0, pos);
        bool vis = (s.find(" SetVisible 1") != std::string::npos);
        std::string stem = prefix;
        if (!stem.empty() && stem.back() == '*')
            stem.pop_back();
        // Document-level UI (primary for rendering)
        if (stem == "hud" || prefix == "hud_*") {
            if (vis) CloseMenu();
            else { menuOpen_ = true; input_.SetMenuMode(true); if (hudLoaded_) hud_.SetAllVisible(false); }
            if (hudLoaded_) hud_.SetVisiblePrefix("hud_", vis);
        }
        if (stem == "menu" || prefix == "menu_*") {
            if (vis) OpenMenu();
            else CloseMenu();
            if (menuLoaded_) menu_.SetVisiblePrefix("menu_", vis);
        }
        for (auto* obj : scene_.objects) {
            if (!obj || obj->GetKind() != SceneObject::Kind::UI) continue;
            auto* ui = static_cast<UISceneObject*>(obj);
            if (!prefix.empty() && prefix.back() == '*') {
                std::string pfx = prefix.substr(0, prefix.size() - 1);
                if (ui->name.compare(0, pfx.size(), pfx) == 0)
                    ui->visible = vis;
            } else if (ui->name == prefix) {
                ui->visible = vis;
            }
        }
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
