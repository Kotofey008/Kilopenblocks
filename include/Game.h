#pragma once
#include "GridData.h"
#include "Texture.h"
#include "TerrainGenerator.h"
#include "Scene.h"
#include "Entity.h"
#include "Camera.h"
#include "InputMgr.h"
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
    int version = 133;          // inferred from app version 1.3.3
    int worldSizeX = 0;
    int worldSizeY = 0;
    int worldSizeZ = 0;
    int terrainType = 0;
    int seed = 0;
    int flags = 0;
    // more fields present in original; expanded as recovered
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
    enum class DrawDistance { Near = 0, Medium = 1, Far = 2, Count };
    enum class LeavesQuality { Low = 0, Medium = 1, High = 2, Count }; // matches GridData default 2
    enum class GameState { Menu = 0, Playing = 1, Loading = 2, Paused = 3 };
    enum class MessagePriority { Low = 0, Normal = 1, High = 2 };

    Game();
    ~Game();

    // Lifecycle
    void InitUI();
    void InitSceneConnections();
    void Play(int slot);
    void Update();
    void TryBreakBlock();
    void TryPlaceBlock(unsigned char blockType);
    void SetAnimatedWater(bool on);
    FluidAnim fluidAnim;
    void SpawnPlayer();
    void OnForeground();
    void OnBackground();

    // Terrain / world
    void SelectTerrainType(TerrainType t);
    void UpdateTerrainType();   // from UI "Game Call UpdateTerrainType"
    void UpdateTerrainSize();
    void CreateElement();       // "Game CreateElement" — create new terrain
    void EnterGameList();
    void InsertGame(int a, int b);

    // Graphics / options (from UI messages)
    void SelectDrawDistance(DrawDistance d);
    void UpdateDrawDistance();
    void SelectLeavesQuality(GridData::LeavesQuality q);
    void UpdateLeavesQuality();
    void EnableClouds(bool on);
    void UpdateClouds();
    void UpdateAnimatedWater();
    void ApplyGraphicsSettings();
    void ApplyGameSettings();
    void UpdateTimeOfDay();
    float GetTimeOfDay() const { return timeOfDay_; }
    void SetTimeOfDay(float t) { timeOfDay_ = t; }
    void UpdateSensitivity();
    void UpdateSkyBox();
    void UpdateTime();

    // Palette / inventory
    void SelectPaletteItem(int index);
    unsigned char GetSelectedBlockType() const;
    void SelectPalettePage(int page, int arg);

    // UI helpers
    void ShowElement(UISceneObject* ui, const char* name, bool visible);
    void SetText(UISceneObject* ui, const char* element, const char* text);
    void SetValue(UISceneObject* ui, const char* element, float value);
    bool GetValue(UISceneObject* ui, const char* element, float& out);
    void ShowMessage(const char* text, MessagePriority prio);
    void UpdateScreenMessages();
    void UpdateHUDUI();
    void SetUINeedFastResponse();
    void FinishScreenshot();
    void ShowProgressScreen(const char* msg);
    void HideProgressScreen();
    void EnterGameSettings();
    void EnterControlsSettings();
    void ScreenClicked();

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
    Camera& GetCamera() { return camera_; }
    InputMgr& GetInput() { return input_; }
    Scene& GetScene() { return scene_; }
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
    DrawDistance drawDistance_ = DrawDistance::Medium;
    bool cloudsEnabled_ = true;
    bool animatedWater_ = true;
    int paletteItem_ = 0;
    int palettePage_ = 0;
    float timeOfDay_ = 0.5f;
    float sensitivity_ = 1.f;
    EntityPlayerSet playerSet_;
    EntityPlayer* player_ = nullptr;
    Camera camera_;
    InputMgr input_;

    void ParseAndRunCommand(const std::string& cmd);
};
