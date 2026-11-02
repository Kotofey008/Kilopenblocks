#pragma once
#include "UIElement.h"

// Original UI (hud.txt + start.scene ElementTouchOverMessage):
// Discrete buttons hud_up/down/left/right/jump + hud_delete/add/activate/menu
// Look: drag on non-button area (not a virtual joystick/stick)

enum class MouseButton : int { Left = 0, Right = 1, Middle = 2, Count = 3 };

class InputMgr {
public:
    InputMgr();

    void TouchPressEvent(unsigned int id, int x, int y);
    void TouchMoveEvent(unsigned int id, int x, int y);
    void TouchReleaseEvent(unsigned int id, int x, int y);

    void MousePressEvent(MouseButton btn);
    void MouseReleaseEvent(MouseButton btn);

    float GetLookDeltaX() const { return lookDX_; }
    float GetLookDeltaY() const { return lookDY_; }
    void ConsumeLookDelta() { lookDX_ = lookDY_ = 0.f; }

    void SetTouchSensitivity(float s) {
        if (s < 0.01f) s = 0.01f;
        if (s > 5.f) s = 5.f;
        sensitivity_ = s;
    }
    float GetTouchSensitivity() const { return sensitivity_; }

    float GetMoveForward() const { return moveForward_; }
    float GetMoveStrafe() const { return moveStrafe_; }
    bool GetJump() const { return jump_; }

    bool ConsumeBreakAction();
    bool ConsumePlaceAction();
    bool ConsumeActivateAction();
    bool ConsumeMenuAction();

    void SetScreenSize(int w, int h) { screenW_ = w; screenH_ = h; }
    void EndFrame();

    struct HudRects {
        UIRect up, down, left, right, jump, del, add, act, menu;
        bool valid = false;
    };
    void SyncHudRects(const HudRects& r) { hudRects_ = r; }
    const HudRects& GetHudRects() const { return hudRects_; }

    // Which discrete HUD button is under (x,y); empty if none
    const char* HitHudButton(int x, int y) const;

    bool AnyLookTouch() const;
    bool MenuMode() const { return menuMode_; }
    void SetMenuMode(bool m) { menuMode_ = m; }

private:
    enum class Zone { None, Look, HudButton };

    static constexpr int kMaxTouches = 8;
    struct Touch {
        bool active = false;
        int x = 0, y = 0;
        int startX = 0, startY = 0;
        Zone zone = Zone::None;
        const char* button = nullptr; // hud_* name while held
    };
    Touch touches_[kMaxTouches];
    float lookDX_ = 0.f, lookDY_ = 0.f;
    float moveForward_ = 0.f, moveStrafe_ = 0.f;
    bool jump_ = false;
    float sensitivity_ = 0.33f;
    int screenW_ = 800, screenH_ = 480;

    bool breakPending_ = false;
    bool placePending_ = false;
    bool activatePending_ = false;
    bool menuPending_ = false;
    bool menuMode_ = false;
    HudRects hudRects_;

    void RecomputeHeldMovement();
};
