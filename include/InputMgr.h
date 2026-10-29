#pragma once

// InputMgr recovered from symbols:
// TouchPress/Move/ReleaseEvent, IsMouseButtonDown/Pressed/Released,
// MousePress/Release/DoublePressEvent, EntityPlayerSet::HandleTouch

enum class MouseButton : int { Left = 0, Right = 1, Middle = 2, Count = 3 };

class InputMgr {
public:
    InputMgr();

    void TouchPressEvent(unsigned int id, int x, int y);
    void TouchMoveEvent(unsigned int id, int x, int y);
    void TouchReleaseEvent(unsigned int id, int x, int y);

    void MousePressEvent(MouseButton btn);
    void MouseReleaseEvent(MouseButton btn);
    bool IsMouseButtonPressed(MouseButton btn) const;
    bool IsMouseButtonReleased(MouseButton btn) const;
    bool IsMouseButtonDown(MouseButton btn) const;

    // Derived state for gameplay
    float GetLookDeltaX() const { return lookDX_; }
    float GetLookDeltaY() const { return lookDY_; }
    void ConsumeLookDelta() { lookDX_ = lookDY_ = 0.f; }

    float GetMoveForward() const { return moveForward_; }
    float GetMoveStrafe() const { return moveStrafe_; }
    bool GetJump() const { return jump_; }
    void SetVirtualStick(float forward, float strafe) {
        moveForward_ = forward; moveStrafe_ = strafe;
    }
    void SetJump(bool j) { jump_ = j; }

    // Actions: left-click / short tap = break; right-click / long-tap / second finger = place
    bool ConsumeBreakAction();
    bool ConsumePlaceAction();

    void SetScreenSize(int w, int h) { screenW_ = w; screenH_ = h; }
    void EndFrame(); // clear one-shot pressed/released flags

private:
    static constexpr int kMaxTouches = 8;
    struct Touch {
        bool active = false;
        int x = 0, y = 0;
        int startX = 0, startY = 0;
        double pressTime = 0;
    };
    Touch touches_[kMaxTouches];
    float lookDX_ = 0.f, lookDY_ = 0.f;
    float moveForward_ = 0.f, moveStrafe_ = 0.f;
    bool jump_ = false;
    float sensitivity_ = 0.15f;
    int screenW_ = 800, screenH_ = 480;

    bool mouseDown_[3] = {};
    bool mousePressed_[3] = {};
    bool mouseReleased_[3] = {};

    bool breakPending_ = false;
    bool placePending_ = false;
    static double Now();
};
