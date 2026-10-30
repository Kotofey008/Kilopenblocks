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
    // Original @ 0x66350 size 1195 — loads UI scene objects, wires messages
    scene_.Load("assets/scenes/start.scene");
}

void Game::InitSceneConnections() {
    // Original @ 0x66800 size 596 — connects UI message handlers to Game methods
}

void Game::Play(int slot) {
    // Original @ 0x6c720 size 2679 — major entry: load/create world, spawn player
    state_ = GameState::Loading;
    grid_.InitDimensionsFromWorldBounds();
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
    // Feed input to player
    if (player_) {
        player_->SetYawPitch(
            player_->GetYaw() + input_.GetLookDeltaX(),
            player_->GetPitch() - input_.GetLookDeltaY());
        input_.ConsumeLookDelta();
        player_->SetMoveInput(input_.GetMoveForward(), input_.GetMoveStrafe(), input_.GetJump());
        if (input_.ConsumeBreakAction()) TryBreakBlock();
        if (input_.ConsumePlaceAction()) TryPlaceBlock(GetSelectedBlockType());
        player_->FixedUpdate();
        player_->FixedPostUpdate();
        fluidAnim.Update(1.f/60.f);
        input_.EndFrame();
        // Camera follows eye
        auto eye = player_->GetPosition();
        eye.y += 1.62f;
        camera_.SetPosition(eye);
        camera_.SetYawPitch(player_->GetYaw(), player_->GetPitch());
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
    if (!player_) {
        player_ = new EntityPlayer(&playerSet_);
        playerSet_.SetLocalPlayer(player_);
    }
    player_->SetGrid(&grid_);
    player_->SetPosition(Vector((float)x + 0.5f, (float)y, (float)z + 0.5f));
    player_->Init();
    camera_.SetPerspective(70.f, 1.777f, 0.1f);
    camera_.SetFarPlane(256.f);
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
void Game::UpdateTime() { timeOfDay_ = std::fmod(timeOfDay_ + 0.0001f, 1.f); }

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

    // Visibility: "hud_* SetVisible 0" / "menu_* SetVisible 1" etc.
    // Pattern: <prefix> SetVisible <0|1>
    auto pos = s.find(" SetVisible ");
    if (pos != std::string::npos) {
        std::string prefix = s.substr(0, pos);
        bool vis = (s.find(" SetVisible 1") != std::string::npos);
        // Apply to matching scene objects
        for (auto* obj : scene_.objects) {
            if (!obj || obj->GetKind() != SceneObject::Kind::UI) continue;
            auto* ui = static_cast<UISceneObject*>(obj);
            // simple wildcard: prefix ends with * or exact match
            if (!prefix.empty() && prefix.back() == '*') {
                std::string p = prefix.substr(0, prefix.size() - 1);
                if (ui->name.compare(0, p.size(), p) == 0)
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
    // Ray from eye along look direction
    Vector eye = player_->GetEyePosition();
    Vector dir = player_->GetLookDirection();
    Vector to(eye.x + dir.x * 6.f, eye.y + dir.y * 6.f, eye.z + dir.z * 6.f);
    GridDataTraceResult tr;
    grid_.TracePoint(eye, to, tr, nullptr);
    if (tr.hit) {
        grid_.BreakBlock(tr);
        // mesh rebuild will be triggered by MarkModifiedRegions + EnsureRegionMeshes
    }
}

void Game::TryPlaceBlock(unsigned char blockType) {
    if (!player_) return;
    Vector eye = player_->GetEyePosition();
    Vector dir = player_->GetLookDirection();
    Vector to(eye.x + dir.x * 6.f, eye.y + dir.y * 6.f, eye.z + dir.z * 6.f);
    GridDataTraceResult tr;
    grid_.TracePoint(eye, to, tr, nullptr);
    if (tr.hit) {
        grid_.PlaceBlock(tr, blockType);
    }
}

// Creative palette order (observed SelectPaletteItem indices map to placeable blocks)
static const unsigned char kPaletteBlocks[] = {
    1,  // Stone
    2,  // Brick
    3,  // Granite
    4,  // Oak Wood
    6,  // Oak Planks
    7,  // Dirt
    8,  // Grass
    11, // Glass
    17, // Torch
    10, // Leaves
};
static const int kPaletteCount = sizeof(kPaletteBlocks)/sizeof(kPaletteBlocks[0]);

unsigned char Game::GetSelectedBlockType() const {
    int idx = paletteItem_;
    if (idx < 0) idx = 0;
    if (idx >= kPaletteCount) idx = idx % kPaletteCount;
    return kPaletteBlocks[idx];
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
