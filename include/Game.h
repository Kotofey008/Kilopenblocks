#pragma once
#include "GridData.h"
#include "Texture.h"
#include "TerrainGenerator.h"
#include "Scene.h"
#include "Entity.h"
#include "Camera.h"
#include "InputMgr.h"
#include "UIElement.h"
#include "GameUIMetrics.h"
#include "Font.h"
#include <string>
#include <vector>
#include <functional>
#include <map>
class Stream;

// Central Game class — methods recovered from symbols + UI message strings in start.scene / ui/*.txt
// Message DSL examples:
//   "Game Call UpdateDrawDistance"
//   "Game SelectPaletteItem 0"
//   "Game CreateElement"
//   "hud_* SetVisible 1"


// GameHeader layout recovered from SaveGameHeader sequential ByteOrderSerialize of 4-byte fields
// Offsets 0,4,8,0xc,0x10,0x14,0x18,... — exact field names still partial
struct GameHeader {
    // Layout recovered from UpdateGameListUI / SaveGameHeader field use in binary
    int magic = 0x53505845;     // 'EXPS' when valid save (binary compares 0x53505845)
    int version = 133;
    int worldMinX = 0, worldMinY = 0, worldMinZ = 0;
    int worldMaxX = 0, worldMaxY = 0, worldMaxZ = 0;
    int terrainType = 0;
    int seed = 0;
    int flags = 0;
    int createTime = 0;         // unix-ish; TimeStr in UpdateGameListUI
    int modifyTime = 0;
    float spawnX = 0, spawnY = 0, spawnZ = 0;
    // legacy aliases used by older Ser paths
    int worldSizeX = 0, worldSizeY = 0, worldSizeZ = 0;
};

struct GameSettings {
    int drawDistance = 1;
    int leavesQuality = 2;
    int clouds = 1;
    int animatedWater = 1;
    float sensitivity = 1.f;
    float timeOfDay = 0.5f;
};

class Game {
public:
    enum class TerrainType { Earth = 0, Flat = 1, Test = 2, Count };
    enum class DrawDistance { Tiny = 0, Short = 1, Normal = 2, Far = 3, Count }; // binary 0..3 labels Tiny/Short/Normal/Far
    // LeavesQuality values: Off=0 Fast=1 Fancy=2 (use GridData::LeavesQuality)
    enum class GameState { Menu = 0, Playing = 1, Loading = 2, Paused = 3 };
    enum class MessagePriority { Low = 0, Normal = 1, High = 2 };

    Game();
    ~Game();

    // Lifecycle
    void InitUI();
    void ApplySceneConfigs(); // push Globals/Camera/EntityPlayerSet into runtime systems
    void InitSceneConnections();
    void Play(int slot);
    void Update();
    void TryBreakBlock();
    void TryPlaceBlock(unsigned char blockType);
    // Binary EntityPlayer mode 3 → ActivateCell (E / hud_activate)
    void TryActivateBlock();
    // Crosshair hit refreshed each Update() for DrawCursor
    const GridDataTraceResult& GetCrosshairHit() const { return crosshairHit_; }
    bool HasCrosshairHit() const { return crosshairHit_.hit; }
    void SetAnimatedWater(bool on);
    FluidAnim fluidAnim;
    void SpawnPlayer();
    void OnForeground();
    void OnBackground();

    // Terrain / world
    void SelectTerrainType(TerrainType t);
    void SelectScreenSize(ScreenSizeEnum e);
    void ApplyUIMetrics();
    void UpdateTerrainType();   // from UI "Game Call UpdateTerrainType"
    void UpdateTerrainSize();                        // 0x65c90
    void UpdateTestFlyingBlocks();                   // 0x65900
    void UpdateTestGroundLevel();
    void CreateElement();       // "Game CreateElement" — create new terrain
    void EnterGameList();
    void LeaveGameList();
    void UpdateGameListUI();
    void InsertGame(int a, int b);

    // Graphics / options (from UI messages) — addresses from binary
    void SelectDrawDistance(DrawDistance d);          // 0x636f0
    void UpdateDrawDistance();                       // → UpdateParameter("UpdateDrawDistance")
    void UpdateParameter(const char* name);       // 0x67210
    void SelectLeavesQuality(GridData::LeavesQuality q); // 0x638a0
    void UpdateLeavesQuality();
    void SelectClouds(bool on);                      // 0x63a20 — pending + UI
    void EnableClouds(bool on);                     // 0x63b80 — apply to Clouds objects
    void UpdateClouds();
    void SelectAnimatedWater(bool on);               // 0x63d50 — pending + UI
    void EnableAnimatedWater(bool on);              // 0x63eb0 — apply to GridModel
    void UpdateAnimatedWater();
    void ApplyGraphicsSettings();                    // 0x641b0
    void ApplyGameSettings();                        // 0x64600
    void ApplyControlsSettings();                    // 0x64070
    void UpdateTimeOfDay();                          // 0x66a60
    float GetTimeOfDay() const { return timeOfDay_; }
    void SetTimeOfDay(float t) { timeOfDay_ = t; }
    void UpdateSensitivity();                        // 0x66be0
    void UpdateSkyBox();
    void UpdateTime();

    // Palette / inventory
    void SelectPaletteItem(int index);               // 0x651c0
    unsigned char GetSelectedBlockType() const;
    void SelectPalettePage(int page, int arg);       // 0x64e30
    void UpdatePaletteUI();                          // 0x74980
    void SelectNext();                               // 0x6a740 — game list
    void SelectPrev();                               // 0x6a770

    // Fly mode — 0x646c0 / 0x6b910
    void FlyModeToggle();
    void SetFlying(bool on);
    bool CloudsEnabled() const { return cloudsEnabled_; }
    bool AnimatedWaterEnabled() const { return animatedWater_; }
    DrawDistance GetDrawDistance() const { return drawDistance_; }

    // UI helpers
    void ShowElement(UISceneObject* ui, const char* name, bool visible);
    void SetText(UISceneObject* ui, const char* element, const char* text);
    void SetValue(UISceneObject* ui, const char* element, float value);
    bool GetValue(UISceneObject* ui, const char* element, float& out);
    void ShowMessage(const char* text, MessagePriority prio);
    void UpdateScreenMessages();
    void UpdateHUDUI();
    void RefreshDynamicUILabels();                              // 0x64860
    void SetUINeedFastResponse();                    // GLApplication 0x6ff50
    void FinishScreenshot();                         // Scene 0xd3e20
    void ShowProgressScreen(const char* msg);
    void HideProgressScreen();
    void EnterGameSettings();                        // 0x66e20
    void EnterControlsSettings();                    // 0x66f00
    void EnterGraphicsSettings();                    // 0x64130
    void ScreenClicked();                            // OnScreenClicked 0x69ce0
    void Create();                                   // 0x6a850 — "Game CreateElement"
    void Copy();                                     // 0x6a900 — "Game CopyElement"
    void Delete();                                   // 0x6b3c0 — "Game DeleteElement"

    // Message dispatcher — parses the scene DSL strings
    void DispatchMessage(const std::string& msg);
    void DispatchMessageList(const std::vector<std::string>& msgs);

    // Save / load (observed)
    void GetSaveName(char* out, const char* fmt, ...);
    void RemoveSaveFiles(int slot);
    void SaveGameHeader(GameHeader& hdr, class Stream& stream);
    void LoadGameHeader(GameHeader& hdr, class Stream& stream);
    void SaveGameSettings(GameSettings& settings, class Stream& stream);
    void UpdateAndSaveGameHeader();
    void UpdateAndSaveGameSettings(GameState state);
    void SaveScreenshot();
    bool SaveWorld(int slot);
    bool LoadWorld(int slot);


    // Accessors
    GridData& GetGrid() { return grid_; }
    EntityPlayer* GetLocalPlayer() { return playerSet_.GetLocalPlayer(); }
    EntityPlayerSet& GetPlayerSet() { return playerSet_; }
    const EntityPlayerSet& GetPlayerSet() const { return playerSet_; }
    Camera& GetCamera() { return camera_; }
    InputMgr& GetInput() { return input_; }
    UIDocument& GetHud() {
        for (auto& p : uiPanels_)
            if (p.sceneName == "hud_ui" && p.loaded) return p.doc;
        return hud_;
    }
    UIDocument& GetMenu() {
        for (auto& p : uiPanels_)
            if (p.sceneName == "menu_ui" && p.loaded) return p.doc;
        return menu_;
    }
    FontResource& GetFonts() { return fonts_; }
    bool HudLoaded() const { return hudLoaded_; }
    bool MenuLoaded() const { return menuLoaded_; }
    bool IsMenuOpen() const { return menuOpen_; }
    void OpenMenu();
    void CloseMenu();
    // Called from input path after InputMgr::TouchPressEvent
    void HandleTouchPress(int x, int y);
    void HandleTouchMove(int x, int y);
    void HandleTouchRelease(int x, int y);

    // UISceneObject panels from start.scene (name → document). Used by RenderHUD.
    // Visibility is panel-level (matches "menu_* SetVisible" patterns on scene object names).
    struct UIPanel {
        std::string sceneName;              // e.g. "options_ui"
        UIDocument doc;
        std::vector<std::string> clickMsgs; // ElementClickedMessage strings from start.scene
        std::vector<std::string> dragMsgs;  // ElementDraggedMessage (slider bindings)
        bool visible = false;
        bool loaded = false;
    };
    std::vector<UIPanel>& UIPanels() { return uiPanels_; }
    const std::vector<UIPanel>& UIPanels() const { return uiPanels_; }
    void SetScreenSize(int w, int h) {
        bool changed = (screenW_ != w || screenH_ != h);
        screenW_ = w; screenH_ = h;
        if (changed && w > 0 && h > 0) ApplyUIMetrics();
    }
    int ScreenW() const { return screenW_; }
    int ScreenH() const { return screenH_; }
    int PalettePage() const { return palettePage_; }
    int PaletteItem() const { return paletteItem_; }
    Scene& GetScene() { return scene_; }
    const SceneGridModelConfig& GetGridModelConfig() const { return scene_.gridModel; }
    bool UseProceduralTextures() const { return useProceduralTextures_; }
    void GetWaterPlaneColor(float& r, float& g, float& b, float& a) const {
        r = waterPlaneR_; g = waterPlaneG_; b = waterPlaneB_; a = waterPlaneA_;
    }
    GameState GetState() const { return state_; }
    TerrainType GetTerrainType() const { return terrainType_; }

    // Static data observed
    static int s_WorldSizesHi[]; // size table

private:
    GridData grid_;
    Scene scene_;
    TerrainGeneratorEarth genEarth_;
    TerrainGeneratorFlat genFlat_;
    GameState state_ = GameState::Menu;
    TerrainType terrainType_ = TerrainType::Earth;
    int terrainSizeIndex_ = 3; // binary +0x238 — index into world-size table
    // Applied settings (binary +0x84 / +0x88 / +0x8c / +0x8d)
    DrawDistance drawDistance_ = DrawDistance::Normal;
    GridData::LeavesQuality leavesQualityApplied_ = GridData::LeavesQuality::Fancy;
    bool cloudsEnabled_ = true;
    bool animatedWater_ = true;
    // Pending from Select* (binary +0xd30 / +0xd34 / +0xd38 / +0xd39)
    DrawDistance drawDistancePending_ = DrawDistance::Normal;
    GridData::LeavesQuality leavesQualityPending_ = GridData::LeavesQuality::Fancy;
    bool cloudsPending_ = true;
    bool animatedWaterPending_ = true;
    // Draw-distance world half-extents table @ VA 0x109e60 (GOT 0x12ce04)
    // stride 12: {x, y, z} per level 0..3
    static const int s_DrawDistanceSize[4][3];
    int paletteItem_ = 0;   // binary +0xdac (absolute S-index)
    int palettePage_ = 0;   // binary +0xdb0 (page base, multiples of 9)
    int gameListIndex_ = 0; // binary +0x1ec — SelectNext/Prev
    int gameListCount_ = 0; // derived from vector size at +0x1f4/+0x1f8
    std::vector<int> gameListSlots_; // slot indices found on disk (binary vector at +0x1f4)
    int activeSaveSlot_ = 0; // binary +0x1f0 used by Create/Play
    int screenW_ = 800;
    int screenH_ = 480;
    float timeOfDay_ = 0.5f;
    float sensitivity_ = 1.f;       // binary +0x94 applied touch sens
    float pendingSensitivity_ = 1.f; // binary +0xd2c UI pending
    ScreenSizeEnum screenSizeEnum_ = ScreenSizeEnum::Phone; // binary +0x90
    GameUIMetrics uiMetrics_;
    int pendingScreenSize_ = 0;     // binary +0xd28
    bool screenSizeDirty_ = false;  // binary +0x98
    float timeMultiplier_ = 1.f; // Globals.TimeMultiplier from start.scene
    long long worldTimeMs_ = 0;  // Game+0x40 clock accumulator (binary UpdateTime)
    bool timeSliderApplyClock_ = false; // true only while user drags time slider
    // GridModel plane colors + procedural flag (from start.scene)
    float waterPlaneR_ = 0.f, waterPlaneG_ = 0.f, waterPlaneB_ = 0.f, waterPlaneA_ = 0.f;
    float lavaPlaneR_ = 0.f, lavaPlaneG_ = 0.f, lavaPlaneB_ = 0.f, lavaPlaneA_ = 0.f;
    bool useProceduralTextures_ = true;
    EntityPlayerSet playerSet_;
    EntityPlayer* player_ = nullptr;
    GridDataTraceResult crosshairHit_;
    Camera camera_;
    InputMgr input_;
    UIDocument hud_;
    UIDocument menu_;
    FontResource fonts_;
    bool hudLoaded_ = false;
    bool menuLoaded_ = false;
    bool menuOpen_ = false;
    std::vector<UIPanel> uiPanels_;
    // Touch press→release click path (binary CheckElementState 0x8000 on release)
    UIPanel* pressedPanel_ = nullptr;
    std::string pressedElementName_;
    bool touchWasDragging_ = false;

    void ParseAndRunCommand(const std::string& cmd);
    void FireElementClicked(UIPanel* panel, const std::string& elementName);
    void LoadUIPanelsFromScene();
    void SetPanelVisiblePattern(const std::string& pattern, bool visible);
    void SyncMenuOpenFromPanels();
    bool MatchNamePattern(const std::string& name, const std::string& pattern) const;
    void RunElementClickedMessage(const std::string& fullMsg);
    UIPanel* FindVisiblePanelHit(float x, float y, UIElement** outHit);
};
