#include "Game.h"
#include "Stream.h"
#include "Loc.h"
#include "GameUIMetrics.h"
#include <cmath>
#include <chrono>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <vector>
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
    // Localize table (GLApplication::Localize @ 0x6fd50) from assets/loc/<lang>.csv
    // Language is set by GLApplication before InitUI via InitFromSystem; default en here.
    if (Localization::Instance().CurrentLang().empty())
        Localization::Instance().LoadLanguage("en");
    scene_.Load("scenes/start.scene");
    ApplySceneConfigs();
    LoadUIPanelsFromScene();
    // GameUIMetrics → $buttonSize / $screenSize (Phone default)
    ApplyUIMetrics();
    RefreshDynamicUILabels();
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
    // Metrics base ~22 at 800-wide; preload nearby sizes for scale variants.
    fonts_.FindOrLoad("Sans", 16);
    fonts_.FindOrLoad("Sans", 18);
    fonts_.FindOrLoad("Sans", 20);
    fonts_.FindOrLoad("Sans", 22);
    fonts_.FindOrLoad("Sans", 24);
    fonts_.FindOrLoad("Sans", 28);
    menuOpen_ = false;
}

// ---------------------------------------------------------------------------
// UISceneObject panels + ElementClickedMessage (from start.scene)
// Original: each UISceneObject has FileName + ElementClickedMessage[] strings.
// "menu_* SetVisible 1" matches scene object names (menu_ui), not only element
// name prefixes. Element clicks match the first token of each message.
// ---------------------------------------------------------------------------

static std::string NormalizeUIAssetPath(const std::string& fileName) {
    // start.scene uses "Screen/ui/hud.txt"; assets live under "ui/hud.txt"
    std::string p = fileName;
    if (p.size() >= 7 && (p.compare(0, 7, "Screen/") == 0 || p.compare(0, 7, "screen/") == 0))
        p = p.substr(7);
    // also accept leading slash
    while (!p.empty() && p[0] == '/') p.erase(p.begin());
    return p;
}

void Game::LoadUIPanelsFromScene() {
    // Dynamic: walk Scene::objects for UISceneObject entries produced by
    // Scene::Load("scenes/start.scene"). No hard-coded panel list.
    uiPanels_.clear();
    for (SceneObject* obj : scene_.objects) {
        if (!obj || obj->GetKind() != SceneObject::Kind::UI) continue;
        auto* ui = static_cast<UISceneObject*>(obj);
        if (ui->fileName.empty()) continue;

        UIPanel panel;
        panel.sceneName = ui->name.empty() ? ui->fileName : ui->name;
        std::string asset = NormalizeUIAssetPath(ui->fileName);
        panel.loaded = panel.doc.LoadAsset(asset.c_str());
        panel.visible = false;
        panel.clickMsgs = ui->elementClickedMessages;
        panel.dragMsgs = ui->elementDraggedMessages;
        if (!panel.loaded) {
            std::cout << "UI panel load FAILED: sceneName=" << panel.sceneName
                      << " asset=" << asset << std::endl;
        } else {
            std::cout << "UI panel loaded: " << panel.sceneName
                      << " <- " << asset
                      << " msgs=" << panel.clickMsgs.size() << std::endl;
        }
        uiPanels_.push_back(std::move(panel));
    }
    if (uiPanels_.empty()) {
        std::cout << "LoadUIPanelsFromScene: no UISceneObjects in scene "
                  << "(start.scene parse missing FileName/messages?)" << std::endl;
    }
}


void Game::ApplyUIMetrics() {
    int w = screenW_ > 0 ? screenW_ : 800;
    int h = screenH_ > 0 ? screenH_ : 480;
    uiMetrics_.Init(w, h, screenSizeEnum_);
    // Reload panels from design assets then scale once (ApplyMetrics is not idempotent)
    if (!uiPanels_.empty()) {
        LoadUIPanelsFromScene();
        for (auto& p : uiPanels_) {
            if (!p.loaded) continue;
            p.doc.ApplyMetrics(uiMetrics_);
            p.doc.SetGridData(&grid_);
            p.doc.ResolveAll((float)w, (float)h);
            if (p.sceneName == "hud_ui") {
                hudLoaded_ = true;
                p.visible = !menuOpen_;
                p.doc.SetAllVisible(p.visible);
            } else if (p.sceneName == "menu_ui") {
                menuLoaded_ = true;
                p.visible = menuOpen_;
                p.doc.SetAllVisible(p.visible);
            } else {
                // keep prior visibility if any
            }
        }
    }
    std::cout << "GameUIMetrics: enum=" << (int)screenSizeEnum_
              << " physical=" << w << "x" << h
              << " logicalW=" << uiMetrics_.logicalW
              << " button=" << uiMetrics_.buttonSize
              << " font=" << uiMetrics_.fontSize << std::endl;
}

void Game::SelectScreenSize(ScreenSizeEnum e) {
    // Game::SelectScreenSize @ 0x63ef0 → pending + ReloadUI
    pendingScreenSize_ = (int)e;
    screenSizeEnum_ = e;
    LoadUIPanelsFromScene();
    ApplyUIMetrics();
    for (auto& p : uiPanels_) {
        if (p.sceneName == "hud_ui" && p.loaded) {
            hudLoaded_ = true;
            p.visible = !menuOpen_;
            p.doc.SetAllVisible(p.visible);
        } else if (p.sceneName == "menu_ui" && p.loaded) {
            menuLoaded_ = true;
        }
    }
    RefreshDynamicUILabels();
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
    // Binary Enter*Settings / open panel paths call Update* to fill *_v labels
    // immediately (not only after first drag).
    if (visible) {
        if (MatchNamePattern("controls_options_ui", pattern) || pattern == "controls_options_*")
            EnterControlsSettings(); // SetValue from applied sensitivity, then label
        if (MatchNamePattern("game_options_ui", pattern) || pattern == "game_options_*")
            // Binary EnterGameSettings @ 0x66e20: SetValue(time) from world index,
            // then UpdateTimeOfDay for label only — must NOT write clock from stale
            // slider CurrentValue on open.
            EnterGameSettings();
        if (MatchNamePattern("terrain_options_ui", pattern) || pattern == "terrain_options_*"
            || MatchNamePattern("new_terrain_ui", pattern) || pattern == "new_terrain_*") {
            // SetAllVisible(true) above re-showed every element; re-apply
            // generator-dependent visibility (SelectTerrainType @ 0x65de0).
            SelectTerrainType(terrainType_);
            // Ensure slider thumbs + integer labels match CurrentValue after show.
            for (auto& p : uiPanels_) {
                if (!p.loaded || !p.visible) continue;
                if (p.sceneName.find("terrain_options") == std::string::npos
                    && p.sceneName.find("new_terrain") == std::string::npos)
                    continue;
                p.doc.LayoutSliders();
            }
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


void Game::HandleTouchMove(int x, int y) {
    float fx = (float)x, fy = (float)y;
    for (auto& p : uiPanels_) {
        if (!p.visible || !p.loaded) continue;
        for (auto& e : p.doc.All()) {
            if (!e.isDragging) continue;
            // Binary UpdateDragAction: slider thumb or free-drag clamped to parent
            p.doc.UpdateDragAction(&e, fx, fy);
            // Slider value bindings — update companion *_v labels
            UIElement* slider = (e.type == UIElement::Type::Slider) ? &e
                : (e.parent && e.parent->type == UIElement::Type::Slider) ? e.parent : nullptr;
            if (slider && slider->type == UIElement::Type::Slider) {
                // Binary: ElementDraggedMessage → Game Call UpdateSensitivity / UpdateTimeOfDay /
                // UpdateTerrainSize / UpdateTestFlyingBlocks / UpdateTestGroundLevel.
                // Those functions format *_v labels (not raw currentValue).
                for (const auto& msg : p.dragMsgs) {
                    size_t semi = msg.find(';');
                    std::string pat = (semi == std::string::npos) ? msg : msg.substr(0, semi);
                    if (MatchNamePattern(slider->name, pat)
                        || (slider->name.size() > 2
                            && MatchNamePattern(slider->name.substr(0, slider->name.size() - 2) + "_i", pat))
                        || MatchNamePattern(e.name, pat)) {
                        if (semi != std::string::npos)
                            RunElementClickedMessage(msg);
                        break;
                    }
                }
                // Fallback if scene has no ElementDraggedMessage for this slider:
                if (slider->name == "sensitivity" || slider->name == "sensitivity_i")
                    UpdateSensitivity();
                else if (slider->name == "time" || slider->name == "time_i"
                         || slider->name == "time_of_day") {
                    // Player is dragging → commit clock (binary Apply does this
                    // on confirm; live drag mirrors sky immediately).
                    timeSliderApplyClock_ = true;
                    UpdateTimeOfDay();
                    timeSliderApplyClock_ = false;
                }
                else if (slider->name == "terrain_size" || slider->name == "terrain_size_i")
                    UpdateTerrainSize();
                else if (slider->name == "test_flying_blocks" || slider->name == "test_flying_blocks_i")
                    UpdateTestFlyingBlocks();
                else if (slider->name == "test_ground_level" || slider->name == "test_ground_level_i"
                         || slider->name == "flat_ground_level" || slider->name == "flat_ground_level_i")
                    UpdateTestGroundLevel();
            }
            return;
        }
    }
}

void Game::FireElementClicked(UIPanel* panel, const std::string& elementName) {
    if (!panel || elementName.empty()) return;
    for (const auto& msg : panel->clickMsgs) {
        size_t semi = msg.find(';');
        std::string pat = (semi == std::string::npos) ? msg : msg.substr(0, semi);
        if (MatchNamePattern(elementName, pat)) {
            RunElementClickedMessage(msg);
            return;
        }
    }
}

void Game::HandleTouchRelease(int x, int y) {
    float fx = (float)x, fy = (float)y;
    const bool wasDragging = touchWasDragging_;
    touchWasDragging_ = false;

    // Clear pressed / drag flags on all elements (binary: clear 0x4000, set 0x10000)
    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        for (auto& e : p.doc.All()) {
            if (e.isDragging) e.isDragging = false;
            if (e.pressed || (e.touchState & UIElement::kTouchPressed)) {
                e.pressed = false;
                e.touchState &= ~UIElement::kTouchPressed;
                e.touchState |= UIElement::kTouchReleased;
            }
            for (auto* c : e.children) {
                if (!c) continue;
                if (c->isDragging) c->isDragging = false;
                if (c->pressed) {
                    c->pressed = false;
                    c->touchState &= ~UIElement::kTouchPressed;
                    c->touchState |= UIElement::kTouchReleased;
                }
            }
        }
    }

    // Slider drag: no ElementClickedMessage (binary CheckElementState skips while dragging)
    if (wasDragging) {
        pressedPanel_ = nullptr;
        pressedElementName_.clear();
        return;
    }

    // Click = press + release (0x8000). Prefer release hit if still on same control;
    // else use the element that was pressed (release can drift slightly).
    UIElement* releaseHit = nullptr;
    UIPanel* releasePanel = FindVisiblePanelHit(fx, fy, &releaseHit);
    std::string clickName;
    UIPanel* clickPanel = nullptr;
    if (releaseHit && releasePanel && pressedPanel_ == releasePanel
        && !pressedElementName_.empty()
        && (releaseHit->name == pressedElementName_
            || MatchNamePattern(releaseHit->name, pressedElementName_))) {
        clickName = releaseHit->name;
        clickPanel = releasePanel;
        releaseHit->touchState |= UIElement::kTouchClicked;
    } else if (pressedPanel_ && !pressedElementName_.empty()) {
        // Released outside original control → still fire if release is over same panel
        // and name was a button (binary keeps click if press+release on same element).
        // Strict: only if releaseHit is null OR still same name.
        if (!releaseHit || releaseHit->name == pressedElementName_) {
            clickName = pressedElementName_;
            clickPanel = pressedPanel_;
            if (releaseHit) releaseHit->touchState |= UIElement::kTouchClicked;
        }
    }

    pressedPanel_ = nullptr;
    pressedElementName_.clear();

    if (clickPanel && !clickName.empty())
        FireElementClicked(clickPanel, clickName);
}


void Game::HandleTouchPress(int x, int y) {
    // Binary: UpdateTouchState sets pressed (0x4000) only. Click (0x8000) +
    // ElementClickedMessage fire on release via CheckElementState.
    float fx = (float)x, fy = (float)y;
    UIElement* hit = nullptr;
    UIPanel* panel = FindVisiblePanelHit(fx, fy, &hit);

    pressedPanel_ = nullptr;
    pressedElementName_.clear();
    touchWasDragging_ = false;

    // Miss on open menu → frame* close (same as clicking background frame)
    if (!panel || !hit) {
        if (menuOpen_) {
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

    // Binary UpdateTouchState: set pressed (0x4000) on hit element
    hit->pressed = true;
    hit->touchState |= UIElement::kTouchPressed;
    hit->touchState &= ~(UIElement::kTouchClicked | UIElement::kTouchReleased);

    // Slider: trough or Draggable thumb → drag path, no click on release
    {
        UIElement* slider = nullptr;
        UIElement* thumb = nullptr;
        if (hit->type == UIElement::Type::Slider) {
            slider = hit;
            if (!hit->children.empty()) thumb = hit->children[0];
        } else if (hit->draggable) {
            slider = panel->doc.FindSliderForThumb(hit);
            thumb = hit;
        }
        if (slider && slider->type == UIElement::Type::Slider) {
            touchWasDragging_ = true;
            slider->isDragging = true;
            slider->pressed = true;
            slider->touchState |= UIElement::kTouchPressed;
            if (thumb) {
                thumb->isDragging = true;
                thumb->pressed = true;
                thumb->touchState |= UIElement::kTouchPressed;
            }
            panel->doc.UpdateDragAction(thumb ? thumb : slider, (float)x, (float)y);
            HandleTouchMove(x, y);
            return;
        }
    }

    // Remember for release-click; do NOT dispatch messages here
    pressedPanel_ = panel;
    pressedElementName_ = hit->name;
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
    // Binary ApplyGraphicsSettings: GridData+0x22c = (DrawDistanceTable[level].x - 0x20) / 2
    // Tiny 96→32, Short 128→48, Normal 160→64, Far 192→80
    {
        int level = (int)drawDistancePending_;
        if (level < 0) level = 0;
        if (level > 3) level = 3;
        drawDistance_ = (DrawDistance)level;
        int hx = s_DrawDistanceSize[level][0];
        int vd = (hx - 0x20) / 2;
        if (vd < 16) vd = 16;
        grid_.SetViewDistanceCells(vd);
    }
    // this+0xf0 batch flag: suppress neighbour SetState|0xC000 during bulk gen
    // (SetCell COMPLETE.md — gate on batch path).
    grid_.SetBatchFlag(1);
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
    grid_.SetBatchFlag(0);
    // PutCell wrote 0xE000/0xC3C0 with bit 0x4000 but skipped hierarchy; sync so
    // UpdateWaterState walks gen-time liquid sources (binary post-gen path).
    grid_.SyncHierarchyFromStates();
    // New world clock: UI 08:00. Day length 0x127500=1209600 ms; hour = 0xC4E0=50304 ms.
    // 8 * 50304 = 402432 → displays 08:00 (not 00:00).
    worldTimeMs_ = 8LL * 50304LL;
    timeOfDay_ = (float)worldTimeMs_ / 1209600.f;
    SpawnPlayer();
    state_ = GameState::Playing;
    // Create/play paths in start.scene often omit "hud_* SetVisible 1"; binary shows HUD
    // when entering Playing. Close menus and reveal on-screen controls.
    menuOpen_ = false;
    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        if (p.sceneName == "hud_ui") {
            p.visible = true;
            p.doc.SetAllVisible(true);
            hudLoaded_ = true;
        } else {
            p.visible = false;
            p.doc.SetAllVisible(false);
        }
    }
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
            // Binary EntityPlayer::Update @ 0x5d290 each frame:
            //   TracePoint (always) → AddOrDeleteBlock (while flags held).
            // TracePoint clears +0xf0 then re-rays — cursor never sticks on old cell.
            player_->TracePoint();
            if (input_.IsBreakHeld()) {
                playerSet_.SetBreakAction(true);
                TryBreakBlock();
            }
            if (input_.IsPlaceHeld()) {
                playerSet_.SetPlaceAction(true);
                TryPlaceBlock(GetSelectedBlockType());
            }
            if (input_.ConsumeActivateAction()) {
                playerSet_.SetActivateAction(true);
                TryActivateBlock();
            }
            (void)input_.ConsumeBreakAction();
            (void)input_.ConsumePlaceAction();
            player_->FixedUpdate();
            player_->FixedPostUpdate();
            playerSet_.ClearActionFlags();
            // DrawCursor uses this frame's hit only (cellIndex < 0 → no cursor)
            crosshairHit_ = player_->GetHitTrace();
        } else {
            input_.ConsumeLookDelta();
            player_->SetMoveInput(0.f, 0.f, false);
            // Binary EntityPlayerSet::Render gates on game phase; no cursor in menu
            crosshairHit_.Clear();
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


// Local helpers (defined later with settings UI)
static Game::UIPanel* FindPanelBySceneName(std::vector<Game::UIPanel>& panels, const char* name);
static void SetPanelElementText(Game::UIPanel* panel, const char* elemName, const char* text);

void Game::SelectTerrainType(TerrainType t) {
    // Game::SelectTerrainType @ 0x65de0
    // ShowElement on terrain_options_ui by type (binary 0x65fd4..0x6614f):
    //   Flat(1): flat_ground_level / _i / _v
    //   Test(2): test_ground_level* + test_flying_blocks*
    //   Earth(0): those groups hidden
    // then UpdateTerrainSize / Update*Ground / UpdateTestFlyingBlocks.
    terrainType_ = t;
    const char* key = "/TerrainTypeEarth/Earth";
    switch (t) {
        case TerrainType::Flat: key = "/TerrainTypeFlat/Flat"; break;
        case TerrainType::Test: key = "/TerrainTypeTest/Test"; break;
        default: break;
    }
    std::string typeName = Localization::Instance().Localize(key);
    std::string head = Localization::Instance().Localize("/TerrainType/Terrain Type: ");
    std::string full = head + typeName;

    const bool showFlat = (t == TerrainType::Flat); // type == 1
    const bool showTest = (t == TerrainType::Test); // type == 2
    // Binary 0x65fc3 setbe after cmp: show terrain_size when type <= 1 (Earth/Flat).
    // Test has fixed world size — no size slider.
    const bool showSize = (t != TerrainType::Test);

    auto showGroup = [](UIDocument& doc, const char* base, bool vis) {
        if (UIElement* e = doc.Find(base)) e->visible = vis;
        std::string i = std::string(base) + "_i";
        std::string v = std::string(base) + "_v";
        if (UIElement* e = doc.Find(i.c_str())) e->visible = vis;
        if (UIElement* e = doc.Find(v.c_str())) e->visible = vis;
    };

    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        if (p.sceneName.find("new_terrain") != std::string::npos) {
            if (UIElement* e = p.doc.Find("terrain_type"))
                e->text = full;
        }
        if (p.sceneName.find("terrain_options") != std::string::npos
            || p.sceneName.find("new_terrain") != std::string::npos) {
            showGroup(p.doc, "flat_ground_level", showFlat);
            showGroup(p.doc, "test_ground_level", showTest);
            showGroup(p.doc, "test_flying_blocks", showTest);
            showGroup(p.doc, "terrain_size", showSize);
        }
    }

    UpdateTerrainSize();
    UpdateTestGroundLevel();
    UpdateTestFlyingBlocks();
}

void Game::UpdateTerrainType() {
    UpdateParameter("UpdateTerrainType");
}

// Game::s_WorldSizesLo @ RO 0x109da0 / s_WorldSizesHi @ 0x109e00
// Stride 0xc = {x, y, z}. Count always 6 (this+0xe78). X shown via sprintf "%d".
// Phone (width < 640) uses Y=64; wider uses Y=128. X/Z ladder identical.
static const int kWorldSizeTableLo[][3] = {
    {128, 64, 128},
    {256, 64, 256},
    {512, 64, 512},
    {1024, 64, 1024},
    {2048, 64, 2048},
    {4096, 64, 4096},
};
static const int kWorldSizeTableHi[][3] = {
    {128, 128, 128},
    {256, 128, 256},
    {512, 128, 512},
    {1024, 128, 1024},
    {2048, 128, 2048},
    {4096, 128, 4096},
};
static const int kWorldSizeCount = 6;

static const int (*ActiveWorldSizeTable())[3] {
    // Binary picks Lo when GetScreenWidth() < 0x280 (640).
    // Approximate with design width until screen is known: prefer Hi for tablets.
    return kWorldSizeTableHi;
}

// Game::UpdateTerrainSize @ 0x65c90
// index = (int)((1-v)*(n-1) + 0.5); clamp; sprintf(terrain_size_v, "%d", table[i].x)
// Vertical slider: v=0 at TOP → index n-1 → 4096; v=1 at BOTTOM → 128.
void Game::UpdateTerrainSize() {
    if (terrainType_ == TerrainType::Test)
        return; // no size slider for Test
    const int (*table)[3] = ActiveWorldSizeTable();
    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        UIElement* s = p.doc.Find("terrain_size");
        if (!s) s = p.doc.Find("terrain_size_i");
        if (!s || s->type != UIElement::Type::Slider) continue;
        float v = s->currentValue;
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        // Binary: (1 - local_64) * (n-1) + 0.5
        int idx = (int)((1.f - v) * (float)(kWorldSizeCount - 1) + 0.5f);
        if (idx < 0) idx = 0;
        if (idx >= kWorldSizeCount) idx = kWorldSizeCount - 1;
        terrainSizeIndex_ = idx;
        int dim = table[idx][0]; // X
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", dim); // binary "%d"
        if (UIElement* lab = p.doc.Find("terrain_size_v"))
            lab->text = buf;
    }
}

// Game::UpdateTestFlyingBlocks @ 0x65900 — sprintf "%d %%"
void Game::UpdateTestFlyingBlocks() {
    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        UIElement* s = p.doc.Find("test_flying_blocks");
        if (!s) s = p.doc.Find("test_flying_blocks_i");
        if (!s || s->type != UIElement::Type::Slider) continue;
        float v = s->currentValue;
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        // Inverted with other more-sliders: left=0%, right=100% uses v directly
        // (binary maps the same; previous build used inverted size/height).
        int pct = (int)(v * 100.f + 0.5f);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d %%", pct);
        if (UIElement* lab = p.doc.Find("test_flying_blocks_v"))
            lab->text = buf;
    }
}

// Game::UpdateFlatGroundLevel @ 0x65b40
// inv = 1-v; mid = sizeY/2; h = (int)((mid-4)*inv + 0.5) + 4; sprintf "%d"
// v=0 (top) → max height; v=1 (bottom) → min (~4).
static int GroundHeightFromSlider(float v, int sizeY) {
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    float inv = 1.f - v;
    int mid = sizeY / 2;
    if (mid < 0) mid = 0;
    int h = (int)((float)(mid - 4) * inv + 0.5f) + 4;
    if (h < 4) h = 4;
    if (h > mid && mid >= 4) h = mid;
    return h;
}

void Game::UpdateTestGroundLevel() {
    // Flat + Test ground labels; binary sprintf "%d".
    const int (*table)[3] = ActiveWorldSizeTable();
    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        {
            UIElement* s = p.doc.Find("flat_ground_level");
            if (!s) s = p.doc.Find("flat_ground_level_i");
            if (s && s->type == UIElement::Type::Slider) {
                int idx = terrainSizeIndex_;
                if (idx < 0) idx = 0;
                if (idx >= kWorldSizeCount) idx = kWorldSizeCount - 1;
                int sizeY = table[idx][1];
                int h = GroundHeightFromSlider(s->currentValue, sizeY);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d", h);
                if (UIElement* lab = p.doc.Find("flat_ground_level_v"))
                    lab->text = buf;
            }
        }
        {
            UIElement* s = p.doc.Find("test_ground_level");
            if (!s) s = p.doc.Find("test_ground_level_i");
            if (s && s->type == UIElement::Type::Slider) {
                // Test world uses fixed Y extent; binary path still integer.
                int h = GroundHeightFromSlider(s->currentValue, 128);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d", h);
                if (UIElement* lab = p.doc.Find("test_ground_level_v"))
                    lab->text = buf;
            }
        }
    }
}

// Game::Create @ 0x6a850 — DSL "Game CreateElement"
void Game::Create() {
    activeSaveSlot_ = 0; // this+0x1f0 = 0
    RemoveSaveFiles(activeSaveSlot_);
    Play(activeSaveSlot_);
}

void Game::CreateElement() { Create(); }

void Game::LeaveGameList() {
    // Game::LeaveGameList @ 0x7a040 — clear list cursor side-effects; keep vector for re-enter
    (void)0;
}

void Game::EnterGameList() {
    // Game::EnterGameList @ 0x7a110 — Dir::FindFile scan of save slots, then UpdateGameListUI
    LeaveGameList();
    gameListSlots_.clear();
    // Binary walks save directory; we probe known slot files world_%d.elw / save_%d.dat
    for (int slot = 0; slot < 64; ++slot) {
        char path[512];
        GetSaveName(path, "world_%d.elw", slot);
        FILE* f = std::fopen(path, "rb");
        if (!f) {
            GetSaveName(path, "save_%d.dat", slot);
            f = std::fopen(path, "rb");
        }
        if (f) {
            std::fclose(f);
            gameListSlots_.push_back(slot);
        }
    }
    gameListCount_ = (int)gameListSlots_.size();
    if (gameListIndex_ < 0) gameListIndex_ = 0;
    if (gameListCount_ > 0 && gameListIndex_ >= gameListCount_)
        gameListIndex_ = gameListCount_ - 1;
    if (gameListCount_ == 0) gameListIndex_ = 0;
    UpdateGameListUI();
}

static void TimeStrLocal(char* out, int outSz, int unixTime) {
    // Binary TimeStr @ 0x76ff0 — formats timestamp for save list labels
    if (!out || outSz < 8) return;
    if (unixTime <= 0) {
        std::snprintf(out, (size_t)outSz, "—");
        return;
    }
    time_t t = (time_t)unixTime;
    struct tm tmv;
#if defined(_WIN32)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    std::snprintf(out, (size_t)outSz, "%04d-%02d-%02d %02d:%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min);
}

void Game::UpdateGameListUI() {
    // Game::UpdateGameListUI — SetText/SetVisible/SetEnabled on select_terrain + terrain_info
    // When header magic == 'EXPS' (0x53505845): show size/pos/times via TimeStr
    char buf[256];
    char t0[64], t1[64];
    const int n = gameListCount_;
    const int idx = (n > 0) ? gameListIndex_ : 0;
    int slot = (n > 0 && idx >= 0 && idx < n) ? gameListSlots_[(size_t)idx] : -1;

    auto setOnPanels = [&](const char* elem, const std::string& text) {
        for (auto& p : uiPanels_) {
            if (!p.loaded) continue;
            if (p.sceneName.find("select_terrain") == std::string::npos &&
                p.sceneName.find("terrain_info") == std::string::npos)
                continue;
            if (UIElement* e = p.doc.Find(elem))
                e->text = text;
        }
    };
    auto setVisible = [&](const char* elem, bool vis) {
        for (auto& p : uiPanels_) {
            if (!p.loaded) continue;
            if (p.sceneName.find("select_terrain") == std::string::npos &&
                p.sceneName.find("terrain_info") == std::string::npos)
                continue;
            if (UIElement* e = p.doc.Find(elem))
                e->visible = vis;
        }
    };

    if (n <= 0 || slot < 0) {
        std::string empty = Localization::Instance().Localize("/NoSavedGames/No saved games.");
        setOnPanels("file", empty);
        std::snprintf(buf, sizeof(buf), "0/0");
        setOnPanels("index", buf);
        setVisible("play", false);
        setVisible("delete", false);
        setVisible("prev", false);
        setVisible("next", false);
        return;
    }

    GameHeader hdr;
    bool hasHdr = false;
    {
        char path[512];
        GetSaveName(path, "header_%d.dat", slot);
        FileStream fs;
        if (fs.Open(path, FileStream::Mode::Read)) {
            LoadGameHeader(hdr, fs);
            hasHdr = true;
        } else {
            // world file alone still counts as a save entry
            hasHdr = false;
        }
    }

    std::snprintf(buf, sizeof(buf), "save_%d", slot);
    setOnPanels("file", buf);
    std::snprintf(buf, sizeof(buf), "%d/%d", idx + 1, n);
    setOnPanels("index", buf);

    if (hasHdr && (hdr.magic == 0x53505845 || hdr.version > 0)) {
        TimeStrLocal(t0, (int)sizeof(t0), hdr.createTime);
        TimeStrLocal(t1, (int)sizeof(t1), hdr.modifyTime);
        std::string info;
        info += t0; info.push_back('\n');
        info += t1; info.push_back('\n');
        char dim[64];
        int sx = hdr.worldSizeX ? hdr.worldSizeX : (hdr.worldMaxX - hdr.worldMinX);
        int sy = hdr.worldSizeY ? hdr.worldSizeY : (hdr.worldMaxY - hdr.worldMinY);
        int sz = hdr.worldSizeZ ? hdr.worldSizeZ : (hdr.worldMaxZ - hdr.worldMinZ);
        std::snprintf(dim, sizeof(dim), "%dx%dx%d", sx, sy, sz);
        info += dim; info.push_back('\n');
        char pos[64];
        std::snprintf(pos, sizeof(pos), "%.0f %.0f %.0f",
                      (double)hdr.spawnX, (double)hdr.spawnY, (double)hdr.spawnZ);
        info += pos;
        setOnPanels("info", info);
        setOnPanels("properties", info);
    }

    setVisible("play", true);
    setVisible("delete", true);
    setVisible("prev", idx > 0);
    setVisible("next", idx + 1 < n);
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
    static const char* kDist[] = { "Tiny", "Short", "Normal", "Far" };
    int i = (int)d;
    if (i < 0) i = 0; if (i > 3) i = 3;
    std::string text = Localization::Instance().Localize("/DrawDistance/Draw Distance: ")
                     + kDist[i];
    if (UIPanel* p = FindPanelBySceneName(uiPanels_, "graphics_options_ui"))
        SetPanelElementText(p, "draw_distance", text.c_str());
}

// Game::SelectLeavesQuality @ 0x638a0 — pending +0xd34
void Game::SelectLeavesQuality(GridData::LeavesQuality q) {
    leavesQualityPending_ = q;
    static const char* kLeaves[] = { "Off", "Fast", "Fancy" };
    int i = (int)q;
    if (i < 0) i = 0; if (i > 2) i = 2;
    std::string text = Localization::Instance().Localize("/LeavesQuality/Leaves: ")
                     + kLeaves[i];
    if (UIPanel* p = FindPanelBySceneName(uiPanels_, "graphics_options_ui"))
        SetPanelElementText(p, "leaves_quality", text.c_str());
    // Apply to GridData immediately so NeedFace/layer change is visible without
    // waiting for ApplyGraphicsSettings (binary only applies there; SetLeavesQuality
    // marks every region dirty @+0x1a so meshes rebuild next frames).
    if (leavesQualityApplied_ != q) {
        leavesQualityApplied_ = q;
        grid_.SetLeavesQuality(q);
    }
}

// Game::SelectClouds @ 0x63a20 — pending +0xd38
void Game::SelectClouds(bool on) {
    cloudsPending_ = on;
    std::string text = Localization::Instance().Localize("/Clouds/Clouds: ")
                     + (on ? "On" : "Off");
    if (UIPanel* p = FindPanelBySceneName(uiPanels_, "graphics_options_ui"))
        SetPanelElementText(p, "clouds", text.c_str());
}

// Game::SelectAnimatedWater @ 0x63d50 — pending +0xd39
void Game::SelectAnimatedWater(bool on) {
    animatedWaterPending_ = on;
    std::string text = Localization::Instance().Localize("/AnimatedWater/Animated water: ")
                     + (on ? "On" : "Off");
    if (UIPanel* p = FindPanelBySceneName(uiPanels_, "graphics_options_ui"))
        SetPanelElementText(p, "animated_water", text.c_str());
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
            SelectLeavesQuality(GridData::LeavesQuality::Fast);
        else if (cur == 1)
            SelectLeavesQuality(GridData::LeavesQuality::Fancy);
        else
            SelectLeavesQuality(GridData::LeavesQuality::Off);
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
        {
            ScreenSizeEnum allowed[3];
            int n = GameUIMetrics::GetAllowed(screenW_, screenH_, allowed);
            if (n < 1) n = 1;
            int idx = 0;
            for (int i = 0; i < n; ++i)
                if ((int)allowed[i] == (int)screenSizeEnum_) { idx = i; break; }
            idx = (idx + 1) % n;
            SelectScreenSize(allowed[idx]);
            pendingScreenSize_ = (int)allowed[idx];
        }
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
            // Still refresh +0x22c when extents already match table size
            int vd = (hx - 0x20) / 2;
            if (vd < 16) vd = 16;
            grid_.SetViewDistanceCells(vd);
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
// If TimesOfDay index changed: adjust this+0x40 clock toward selected tick
// (mod 0x127500), then UpdateSkyBox.
void Game::ApplyGameSettings() {
    // Binary: if TimesOfDay index changed, adjust this+0x40 toward selected tick.
    // Read slider + commit clock.
    timeSliderApplyClock_ = true;
    UpdateTimeOfDay();
    timeSliderApplyClock_ = false;
    UpdateSkyBox();
    UpdateAndSaveGameSettings(state_);
}

// Game::ApplyControlsSettings @ 0x64070
void Game::ApplyControlsSettings() {
    if ((int)screenSizeEnum_ != pendingScreenSize_) {
        screenSizeDirty_ = true;
        screenSizeEnum_ = static_cast<ScreenSizeEnum>(pendingScreenSize_);
        ApplyUIMetrics();
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

// Game::UpdateTimeOfDay @ 0x66a60 (ghidra)
// index = (int)((1 - sliderValue) * (count-1) + 0.5); label from TimesOfDay[index].
// Continuous analogue: dayFraction = 1 - v  (same (1-v) orientation as terrain size).
void Game::UpdateTimeOfDay() {
    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        UIElement* s = p.doc.Find("time");
        if (!s) s = p.doc.Find("time_i");
        if (!s) s = p.doc.Find("time_of_day");
        if (!s || s->type != UIElement::Type::Slider) continue;
        float v = s->currentValue;
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        // Binary: (1 - local_64) * …
        float day = 1.f - v;
        if (day < 0.f) day = 0.f;
        if (day > 1.f) day = 1.f;

        float hoursF = day * 24.f;
        int hours = (int)hoursF;
        if (hours >= 24) hours = 23;
        int mins = (int)((hoursF - (float)hours) * 60.f);
        if (mins > 59) mins = 59;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", hours, mins);
        if (UIElement* lab = p.doc.Find("time_v"))
            lab->text = buf;
        if (UIElement* lab = p.doc.Find("time_of_day_v"))
            lab->text = buf;

        if (timeSliderApplyClock_) {
            timeOfDay_ = day;
            const long long kDayMs = 1209600LL;
            long long target = (long long)(day * (double)kDayMs);
            if (target < 0) target = 0;
            if (target >= kDayMs) target = kDayMs - 1;
            long long phase = worldTimeMs_ % kDayMs;
            if (phase < 0) phase += kDayMs;
            worldTimeMs_ += (target - phase);
        }
    }
}

// Game::UpdateSensitivity @ 0x66be0
// Constants from binary data: 0.5, 40, 30, 10, 0.1, -1.0
// Formats: "1", "%.1f", "1 / %.1f" → sensitivity_v
void Game::UpdateSensitivity() {
    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        UIElement* s = p.doc.Find("sensitivity");
        if (!s) s = p.doc.Find("sensitivity_i");
        if (!s || s->type != UIElement::Type::Slider) continue;
        float v = s->currentValue;
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        // t = (int)(v * 40 + 0.5)
        float t = (float)(int)(v * 40.f + 0.5f);
        float sens;
        if (v < 0.5f)
            sens = (t - 30.f) * 0.1f;
        else
            sens = (t - 10.f) * 0.1f;
        char buf[32];
        if (sens == -1.f) {
            // Binary special: store 1.0, text "1"
            sens = 1.f;
            buf[0] = '1'; buf[1] = '\0';
        } else if (sens < -1.f) {
            float absv = -sens;
            std::snprintf(buf, sizeof(buf), "1 / %.1f", (double)absv);
            sens = 1.f / absv;
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f", (double)sens);
        }
        pendingSensitivity_ = sens;
        if (UIElement* lab = p.doc.Find("sensitivity_v"))
            lab->text = buf;
    }
}
// Game::UpdateSkyBox @ 0x62d10
// timeOfDay = (worldTimeMs % 0x127500) / period; SetTimeOfDay on sky;
// Clouds::SetSkyLightScale = 0.2 + 0.8 * dayFactor (1.0 day / 0.2 night).
void Game::UpdateSkyBox() {
    const long long kDayMs = 1209600LL; // 0x127500
    long long phase = worldTimeMs_ % kDayMs;
    if (phase < 0) phase += kDayMs;
    timeOfDay_ = (float)phase / (float)kDayMs;
    // dayFactor mirrored for clouds (SkyBox recomputes on SetTimeOfDay in GL frame)
    // Binary: fVar7 = 0.8 * dayFactor + 0.2
    // Applied to Clouds via GLApplication after SetTimeOfDay.
}

// Game::UpdateTime @ 0x62cb0 — accumulate ApplicationBase::Clock delta into
// this+0x40 (64-bit). Game::UpdateSkyBox @ 0x62d10 then:
//   SetTimeOfDay( (timeCounter % 0x127500) / 1209600.0f )
// Day period 0x127500 = 1209600 ms ≈ 20.16 min (binary constant).
void Game::UpdateTime() {
    using clock = std::chrono::steady_clock;
    static clock::time_point s_prev = clock::now();
    static bool s_init = false;
    auto now = clock::now();
    if (!s_init) {
        s_prev = now;
        s_init = true;
        return;
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_prev).count();
    s_prev = now;
    if (ms < 0) ms = 0;
    // Binary Scene MaxTimestep = 200 (start.scene); clamp spike after resume
    if (ms > 200) ms = 200;
    // TimeMultiplier from Globals (default 1)
    float scale = timeMultiplier_;
    if (scale < 0.01f) scale = 0.01f;
    long long add = (long long)((float)ms * scale);
    worldTimeMs_ += add;
    // 0x127500 = 1209600
    const long long kDayMs = 1209600LL;
    long long phase = worldTimeMs_ % kDayMs;
    if (phase < 0) phase += kDayMs;
    timeOfDay_ = (float)phase / (float)kDayMs;
}

void Game::SelectPaletteItem(int index) {
    // Binary 0x651c0: dac = arg + db0; UpdatePaletteUI; EntityPlayerSet.SelectMaterial(dac)
    if (index < 0) index = 0;
    int pageBase = palettePage_ * 9; // db0 stored as page*9
    if (index <= 8)
        paletteItem_ = pageBase + index;
    else
        paletteItem_ = index;
    UpdatePaletteUI();
}

void Game::SelectPalettePage(int page, int) {
    // Binary 0x64e30: -2 = next, -1 = prev; wrap by material sel count / 9
    int pageCount = 7; // menu_mtl_set_0..6 default
    int maxSel = grid_.MaxSelIndex();
    if (maxSel > 0) {
        pageCount = (maxSel + 8) / 9;
        if (pageCount < 1) pageCount = 1;
        if (pageCount > 32) pageCount = 32;
    }
    if (page == -2) palettePage_ += 1;
    else if (page == -1) palettePage_ -= 1;
    else if (page >= 0) palettePage_ = page;
    if (palettePage_ < 0) {
        palettePage_ = pageCount - 1;
        if (palettePage_ < 0) palettePage_ = 0;
    }
    if (palettePage_ >= pageCount) palettePage_ = 0;
    int slot = paletteItem_ % 9;
    if (slot < 0) slot = 0;
    paletteItem_ = palettePage_ * 9 + slot;
    UpdatePaletteUI();
}

// Game::UpdatePaletteUI @ 0x74980
// Page tabs (menu_mtl_set_*): binary scrolls visible labels when pageCount > nTabs.
//   half = nTabs/2
//   if page < half:          label[i] = i+1,  selected iff i==page
//   else if page < total-half: label[i] = page+1-half+i, selected iff i==half
//   else:                    label[i] = total-nTabs+i+1, selected iff page==start+i
void Game::UpdatePaletteUI() {
    const int pageSize = 9;
    const int pageBase = palettePage_ * pageSize;
    const int selSlot = paletteItem_ - pageBase;

    int pageCount = 1;
    int maxSel = grid_.MaxSelIndex();
    if (maxSel > 0) {
        pageCount = (maxSel + pageSize - 1) / pageSize;
        if (pageCount < 1) pageCount = 1;
    }

    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        p.doc.SetPalettePage(palettePage_);

        // Collect page-tab elements in index order (menu_mtl_set_0 .. _N)
        std::vector<UIElement*> tabs;
        tabs.reserve(8);
        for (int i = 0; i < 16; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), "menu_mtl_set_%d", i);
            if (UIElement* e = p.doc.Find(name))
                tabs.push_back(e);
            else
                break;
        }
        const int nTabs = (int)tabs.size();
        if (nTabs > 0 && pageCount > 0) {
            const int half = nTabs / 2;
            int startLabel = 0; // 0-based page index shown on first tab
            if (palettePage_ < half || pageCount <= nTabs) {
                startLabel = 0;
            } else if (palettePage_ < pageCount - half) {
                startLabel = palettePage_ - half;
            } else {
                startLabel = pageCount - nTabs;
                if (startLabel < 0) startLabel = 0;
            }
            for (int i = 0; i < nTabs; ++i) {
                int pageIdx = startLabel + i;
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", pageIdx + 1);
                if (tabs[i]->text != buf)
                    tabs[i]->text = buf;
                tabs[i]->selected = (pageIdx == palettePage_);
            }
        }

        for (auto& e : p.doc.All()) {
            if (e.name.compare(0, 9, "menu_mtl_") == 0 && e.name.size() == 10) {
                int slot = e.name[9] - '0';
                e.selected = (slot >= 0 && slot <= 8 && slot == selSlot
                              && paletteItem_ == pageBase + slot);
            }
            if (e.type == UIElement::Type::GridModelMtl) {
                int n = e.arrayWidth * e.arrayHeight;
                if (n < 1) n = 9;
                if ((int)e.materials.size() < n) e.materials.resize((size_t)n, 0);
                // Draw path: materials[i] = local slot; absolute = palettePage_*9+slot
                // (binary stores absolute db0+i; our Draw adds page base once).
                for (int i = 0; i < n; ++i)
                    e.materials[(size_t)i] = i;
            }
        }
    }
}

void Game::SelectNext() {
    int next = gameListIndex_ + 1;
    if (next < gameListCount_) {
        gameListIndex_ = next;
        UpdateGameListUI();
    }
}

void Game::SelectPrev() {
    if (gameListIndex_ > 0) {
        gameListIndex_ -= 1;
        UpdateGameListUI();
    }
}
void Game::FlyModeToggle() {
    SetFlying(!playerSet_.GetFlying());
}
void Game::SetFlying(bool on) {
    // Game::SetFlying @ 0x7b910 — EntityPlayerSet::SetFlying + SetText on fly_mode_toggle.
    // Binary (PIC): when param_1 true → string @ Walking; when false → Flying.
    // Button shows the mode you SWITCH TO, not the current mode.
    playerSet_.SetFlying(on);
    const char* key = on ? "/Walking/Walking" : "/Flying/Flying";
    std::string label = Localization::Instance().Localize(key);
    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        if (UIElement* e = p.doc.Find("fly_mode_toggle"))
            e->text = label;
    }
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

// Binary SetFlying / SelectTerrainType / SelectScreenSize update element Text via Localize.
// Central refresh so labels stay correct without requiring a button press.
void Game::RefreshDynamicUILabels() {
    // fly_mode_toggle: show target mode (binary SetFlying string select)
    {
        bool flying = playerSet_.GetFlying();
        std::string fly = Localization::Instance().Localize(
            flying ? "/Walking/Walking" : "/Flying/Flying");
        for (auto& p : uiPanels_) {
            if (!p.loaded) continue;
            if (UIElement* e = p.doc.Find("fly_mode_toggle"))
                e->text = fly;
        }
    }
    // menu_full_version stays hidden (design Visible=0; wildcard menu_* SetVisible 1
    // must not re-show it over menu_select_terrain at the same position).
    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        if (UIElement* e = p.doc.Find("menu_full_version"))
            e->visible = false;
    }
    // screen_size: "Screen: " + Phone|Small tablet|Tablet
    {
        const char* sizeKey = "/ScreenSizePhone/";
        switch (static_cast<ScreenSizeEnum>(pendingScreenSize_)) {
            case ScreenSizeEnum::SmallTablet: sizeKey = "/ScreenSizeSmallTablet/"; break;
            case ScreenSizeEnum::Tablet: sizeKey = "/ScreenSizeTablet/"; break;
            default: break;
        }
        // also respect applied enum if pending matches
        ScreenSizeEnum se = screenSizeEnum_;
        if ((int)pendingScreenSize_ >= 0)
            se = static_cast<ScreenSizeEnum>(pendingScreenSize_);
        switch (se) {
            case ScreenSizeEnum::SmallTablet: sizeKey = "/ScreenSizeSmallTablet/"; break;
            case ScreenSizeEnum::Tablet: sizeKey = "/ScreenSizeTablet/"; break;
            default: sizeKey = "/ScreenSizePhone/"; break;
        }
        std::string head = Localization::Instance().Localize("/ScreenSize/Screen: ");
        std::string val = Localization::Instance().Localize(sizeKey);
        // Localize of key-only may return empty — fall back to English stems
        if (val.empty() || val[0] == '/') {
            if (se == ScreenSizeEnum::SmallTablet) val = "Small tablet";
            else if (se == ScreenSizeEnum::Tablet) val = "Tablet";
            else val = "Smartphone";
        }
        std::string full = head + val;
        for (auto& p : uiPanels_) {
            if (!p.loaded) continue;
            if (UIElement* e = p.doc.Find("screen_size"))
                e->text = full;
        }
    }
    // terrain_type: "Terrain Type: " + Earth|Flat|Test
    {
        const char* typeKey = "/TerrainTypeEarth/Earth";
        switch (terrainType_) {
            case TerrainType::Flat: typeKey = "/TerrainTypeFlat/Flat"; break;
            case TerrainType::Test: typeKey = "/TerrainTypeTest/Test"; break;
            default: break;
        }
        std::string head = Localization::Instance().Localize("/TerrainType/Terrain Type: ");
        std::string val = Localization::Instance().Localize(typeKey);
        if (val.empty() || val[0] == '/') {
            if (terrainType_ == TerrainType::Flat) val = "Flat";
            else if (terrainType_ == TerrainType::Test) val = "Flying blocks";
            else val = "Earth";
        }
        std::string full = head + val;
        for (auto& p : uiPanels_) {
            if (!p.loaded) continue;
            if (UIElement* e = p.doc.Find("terrain_type"))
                e->text = full;
        }
    }
}

void Game::UpdateHUDUI() {
    // Sync UIElement::pressed / selected from input + palette + options state.
    // Binary CheckElementState / state styles: State[0] default/pressed, State[1] selected.
    RefreshDynamicUILabels();

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

    // Game::UpdateHUDUI @ 0x74860 — hud_activate only when looking-cell index ≥0 and
    // GridData::IsActiveCell(cell). Design Visible=0; default stays hidden.
    if (hudLoaded_) {
        if (UIElement* act = hudDoc.Find("hud_activate")) {
            bool show = false;
            if (!menuOpen_ && player_) {
                Vector eye = player_->GetEyePosition();
                Vector dir = player_->GetLookDirection();
                float reach = player_->GetOperationDistance();
                Vector to(eye.x + dir.x * reach, eye.y + dir.y * reach, eye.z + dir.z * reach);
                GridDataTraceResult tr{};
                grid_.TracePoint(eye, to, tr, player_);
                if (tr.hit)
                    show = grid_.IsActiveCellType(tr.blockType);
            }
            act->visible = show;
            if (!show)
                act->pressed = false;
        }
    }

    // Palette selected + GridModelMtl page materials (binary UpdatePaletteUI)
    UpdatePaletteUI();

    // Walk every loaded panel (menu, graphics, terrain, …)
    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        for (auto& e : p.doc.All()) {
            // Preserve in-progress drag/press on slider thumbs
            if (e.isDragging)
                continue;

            // Terrain type radio buttons (new_terrain panel)
            if (e.name == "terrain_earth" || e.name == "earth")
                e.selected = (terrainType_ == TerrainType::Earth);
            else if (e.name == "terrain_flat" || e.name == "flat")
                e.selected = (terrainType_ == TerrainType::Flat);
            else if (e.name == "terrain_test" || e.name == "test")
                e.selected = (terrainType_ == TerrainType::Test);
        }

        // Graphics option display text (mirrors Select* pending labels)
        if (p.sceneName.find("graphics") != std::string::npos) {
            auto setText = [&](const char* name, const std::string& t) {
                if (UIElement* e = p.doc.Find(name)) e->text = t;
            };
            static const char* kDist[] = { "Tiny", "Short", "Normal", "Far" };
            int dd = (int)drawDistancePending_;
            if (dd < 0) dd = 0; if (dd > 3) dd = 3;
            setText("draw_distance",
                    Localization::Instance().Localize("/DrawDistance/Draw Distance: ") + kDist[dd]);
            static const char* kLeaves[] = { "Off", "Fast", "Fancy" };
            int lq = (int)leavesQualityPending_;
            if (lq < 0) lq = 0; if (lq > 2) lq = 2;
            setText("leaves_quality",
                    Localization::Instance().Localize("/LeavesQuality/Leaves: ") + kLeaves[lq]);
            setText("clouds",
                    Localization::Instance().Localize("/Clouds/Clouds: ") +
                    (cloudsPending_ ? "On" : "Off"));
            setText("animated_water",
                    Localization::Instance().Localize("/AnimatedWater/Animated water: ") +
                    (animatedWaterPending_ ? "On" : "Off"));
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

// Game::EnterGameSettings @ 0x66e20 (ghidra)
// GetTimesOfDay → SetValue("time", 1 - index/(n-1)) → UpdateTimeOfDay (label only).
// Continuous: sliderValue = 1 - timeOfDay_ so thumb matches binary orientation.
void Game::EnterGameSettings() {
    float day = timeOfDay_;
    if (day < 0.f) day = 0.f;
    if (day > 1.f) day = 1.f;
    float v = 1.f - day; // binary SetValue(1 - index/(n-1))
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    for (auto& p : uiPanels_) {
        if (!p.loaded) continue;
        if (p.sceneName.find("game_options") == std::string::npos) continue;
        for (const char* n : {"time", "time_i", "time_of_day"}) {
            if (UIElement* s = p.doc.Find(n)) {
                s->currentValue = v;
            }
        }
        p.doc.LayoutSliders();
    }
    timeSliderApplyClock_ = false;
    UpdateTimeOfDay();
}

// Game::EnterControlsSettings @ 0x66f00 — push sensitivity into controls UI
void Game::EnterControlsSettings() {
    // Binary: SetValue sensitivity slider (0..1), UpdateSensitivity
    pendingSensitivity_ = sensitivity_;
    // Approximate inverse of UpdateSensitivity quantisation for initial thumb pos.
    float sens = sensitivity_;
    if (sens < 0.05f) sens = 0.05f;
    float v;
    if (sens < 1.f) {
        // lower half: sens = (t-30)*0.1 → t = sens*10+30, v = (t-0.5)/40
        float t = sens * 10.f + 30.f;
        v = (t - 0.5f) / 40.f;
    } else {
        float t = sens * 10.f + 10.f;
        v = (t - 0.5f) / 40.f;
    }
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    if (UIPanel* p = FindPanelBySceneName(uiPanels_, "controls_options_ui")) {
        if (UIElement* s = p->doc.Find("sensitivity")) {
            s->currentValue = v;
            p->doc.LayoutSliders();
        }
        if (UIElement* s = p->doc.Find("sensitivity_i")) {
            s->currentValue = v;
            p->doc.LayoutSliders();
        }
        RefreshDynamicUILabels();
    }
    UpdateSensitivity();
}

// Game::EnterGraphicsSettings @ 0x64130
void Game::EnterGraphicsSettings() {
    // 0x64130: Select*(applied fields +0x84/+0x88/+0x8c/+0x8d) → fills pending + UI
    SelectDrawDistance(drawDistance_);
    SelectLeavesQuality(leavesQualityApplied_);
    SelectClouds(cloudsEnabled_);
    SelectAnimatedWater(animatedWater_);
}

// EnterControlsSettings already defined above with slider push; ensure pending mirrors applied.

// Game::OnScreenClicked @ 0x69ce0 — progress screen dismiss
void Game::ScreenClicked() {
    HideProgressScreen();
}

// Game::Copy @ 0x6a900 / Delete @ 0x6b3c0 — save slot ops (bodies large; structure only)
void Game::Copy() {
    // Binary copies save files for current gameListIndex_
}

void Game::Delete() {
    // Binary deletes save at gameListIndex_, refreshes list via EnterGameList scan
    if (gameListCount_ > 0 && gameListIndex_ >= 0 && gameListIndex_ < gameListCount_) {
        int slot = gameListSlots_[(size_t)gameListIndex_];
        RemoveSaveFiles(slot);
        char path[512];
        GetSaveName(path, "world_%d.elw", slot);
        std::remove(path);
        EnterGameList(); // rescan + UpdateGameListUI
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
        else if (method == "LeaveGameList") LeaveGameList();
        else if (method == "EnterGameSettings") EnterGameSettings();
        else if (method == "EnterControlsSettings") EnterControlsSettings();
        else if (method == "EnterGraphicsSettings") EnterGraphicsSettings();
        else if (method == "FlyModeToggle") FlyModeToggle();
        else if (method == "ApplyControlsSettings") ApplyControlsSettings();
        else if (method == "UpdateScreenSize") {
            // Game::SelectScreenSize path — cycle pendingScreenSize_
            {
            ScreenSizeEnum allowed[3];
            int n = GameUIMetrics::GetAllowed(screenW_, screenH_, allowed);
            if (n < 1) n = 1;
            int idx = 0;
            for (int i = 0; i < n; ++i)
                if ((int)allowed[i] == (int)screenSizeEnum_) { idx = i; break; }
            idx = (idx + 1) % n;
            SelectScreenSize(allowed[idx]);
            pendingScreenSize_ = (int)allowed[idx];
        }
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

    // "Game Play" — load selected save slot (EnterGameList index → gameListSlots_)
    if (s == "Game Play") {
        int slot = 0;
        if (gameListCount_ > 0 && gameListIndex_ >= 0 && gameListIndex_ < gameListCount_)
            slot = gameListSlots_[(size_t)gameListIndex_];
        activeSaveSlot_ = slot;
        if (!LoadWorld(slot)) {
            // empty/missing world → still enter play with generated terrain
            Play(slot);
        } else {
            SpawnPlayer();
            state_ = GameState::Playing;
            menuOpen_ = false;
            for (auto& p : uiPanels_) {
                if (!p.loaded) continue;
                if (p.sceneName == "hud_ui") {
                    p.visible = true;
                    p.doc.SetAllVisible(true);
                } else {
                    p.visible = false;
                    p.doc.SetAllVisible(false);
                }
            }
        }
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
#include <ctime>

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
    // Sequential 4-byte LE fields; expanded for list UI (magic/times/spawn)
    stream.Ser(hdr.magic);
    stream.Ser(hdr.version);
    stream.Ser(hdr.worldMinX); stream.Ser(hdr.worldMinY); stream.Ser(hdr.worldMinZ);
    stream.Ser(hdr.worldMaxX); stream.Ser(hdr.worldMaxY); stream.Ser(hdr.worldMaxZ);
    stream.Ser(hdr.terrainType);
    stream.Ser(hdr.seed);
    stream.Ser(hdr.flags);
    stream.Ser(hdr.createTime);
    stream.Ser(hdr.modifyTime);
    stream.Ser(hdr.spawnX); stream.Ser(hdr.spawnY); stream.Ser(hdr.spawnZ);
    stream.Ser(hdr.worldSizeX); stream.Ser(hdr.worldSizeY); stream.Ser(hdr.worldSizeZ);
}

void Game::LoadGameHeader(GameHeader& hdr, Stream& stream) {
    stream.Ser(hdr.magic);
    stream.Ser(hdr.version);
    stream.Ser(hdr.worldMinX); stream.Ser(hdr.worldMinY); stream.Ser(hdr.worldMinZ);
    stream.Ser(hdr.worldMaxX); stream.Ser(hdr.worldMaxY); stream.Ser(hdr.worldMaxZ);
    stream.Ser(hdr.terrainType);
    stream.Ser(hdr.seed);
    stream.Ser(hdr.flags);
    stream.Ser(hdr.createTime);
    stream.Ser(hdr.modifyTime);
    stream.Ser(hdr.spawnX); stream.Ser(hdr.spawnY); stream.Ser(hdr.spawnZ);
    stream.Ser(hdr.worldSizeX); stream.Ser(hdr.worldSizeY); stream.Ser(hdr.worldSizeZ);
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
    // Queues mode 1; SetCell in FixedUpdate
    player_->AddOrDeleteBlock(true, 0, false);
}

void Game::TryPlaceBlock(unsigned char blockType) {
    if (!player_) return;
    // Queues mode 1 or 2; SetCell in FixedUpdate / FixedPostUpdate
    player_->AddOrDeleteBlock(false, blockType, false);
}

// Binary: EntityPlayerSet flag +0x264 → mode 3 → ActivateCell in FixedUpdate
void Game::TryActivateBlock() {
    if (!player_) return;
    player_->AddOrDeleteBlock(false, 0, true);
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
