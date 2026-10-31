#include "InputMgr.h"
#include <cmath>
#include <cstdlib>

InputMgr::InputMgr() {}

InputMgr::HudLayout InputMgr::ComputeLayout() const {
    HudLayout L{};
    int s = screenH_ / 6; // button size ~ like original 110 on 480
    if (s < 64) s = 64;
    if (s > 140) s = 140;
    L.btnS = s;
    // D-pad bottom-left
    L.padR = s;
    L.padCx = s + s / 2;
    L.padCy = screenH_ - (s + s / 2);
    // Action cluster bottom-right
    L.btnJumpX = screenW_ - 2 * s - s / 4;
    L.btnJumpY = screenH_ - 2 * s - s / 4;
    L.btnDelX = screenW_ - 3 * s - s / 2;
    L.btnDelY = screenH_ - s - s / 4;
    L.btnAddX = screenW_ - s - s / 4;
    L.btnAddY = screenH_ - s - s / 4;
    L.btnActX = screenW_ - 2 * s - s / 4;
    L.btnActY = screenH_ - 3 * s - s / 2;
    return L;
}

InputMgr::Zone InputMgr::HitTest(int x, int y) const {
    HudLayout L = ComputeLayout();
    auto inBtn = [&](int bx, int by) {
        return x >= bx && x < bx + L.btnS && y >= by && y < by + L.btnS;
    };
    // pad: circular-ish region
    int dx = x - L.padCx, dy = y - L.padCy;
    if (dx * dx + dy * dy <= (L.padR * 2) * (L.padR * 2))
        return Zone::Pad;
    if (inBtn(L.btnJumpX, L.btnJumpY)) return Zone::Jump;
    if (inBtn(L.btnDelX, L.btnDelY)) return Zone::Delete;
    if (inBtn(L.btnAddX, L.btnAddY)) return Zone::Add;
    if (inBtn(L.btnActX, L.btnActY)) return Zone::Activate;
    return Zone::Look;
}

void InputMgr::TouchPressEvent(unsigned int id, int x, int y) {
    if (id >= kMaxTouches) return;
    touches_[id].active = true;
    touches_[id].x = touches_[id].startX = x;
    touches_[id].y = touches_[id].startY = y;
    Zone z = HitTest(x, y);
    touches_[id].zone = z;
    if (z == Zone::Jump) jump_ = true;
    if (z == Zone::Delete) breakPending_ = true;
    if (z == Zone::Add) placePending_ = true;
    if (z == Zone::Activate) activatePending_ = true;
    if (z == Zone::Pad) {
        // direction from pad center
        HudLayout L = ComputeLayout();
        float pdx = (float)(x - L.padCx) / (float)L.padR;
        float pdy = (float)(y - L.padCy) / (float)L.padR;
        // screen Y down → forward is -pdy
        moveStrafe_ = pdx;
        moveForward_ = -pdy;
        if (moveStrafe_ > 1) moveStrafe_ = 1; if (moveStrafe_ < -1) moveStrafe_ = -1;
        if (moveForward_ > 1) moveForward_ = 1; if (moveForward_ < -1) moveForward_ = -1;
    }
}

void InputMgr::TouchMoveEvent(unsigned int id, int x, int y) {
    if (id >= kMaxTouches || !touches_[id].active) return;
    int dx = x - touches_[id].x;
    int dy = y - touches_[id].y;
    touches_[id].x = x;
    touches_[id].y = y;

    if (touches_[id].zone == Zone::Look) {
        lookDX_ += dx * sensitivity_;
        lookDY_ += dy * sensitivity_;
    } else if (touches_[id].zone == Zone::Pad) {
        HudLayout L = ComputeLayout();
        float pdx = (float)(x - L.padCx) / (float)L.padR;
        float pdy = (float)(y - L.padCy) / (float)L.padR;
        moveStrafe_ = pdx;
        moveForward_ = -pdy;
        if (moveStrafe_ > 1) moveStrafe_ = 1; if (moveStrafe_ < -1) moveStrafe_ = -1;
        if (moveForward_ > 1) moveForward_ = 1; if (moveForward_ < -1) moveForward_ = -1;
    }
}

void InputMgr::TouchReleaseEvent(unsigned int id, int x, int y) {
    if (id >= kMaxTouches) return;
    if (touches_[id].active) {
        if (touches_[id].zone == Zone::Pad) {
            moveForward_ = moveStrafe_ = 0.f;
        }
        if (touches_[id].zone == Zone::Jump) jump_ = false;
    }
    touches_[id].active = false;
    touches_[id].zone = Zone::None;
    (void)x; (void)y;
}

void InputMgr::MousePressEvent(MouseButton btn) {
    if (btn == MouseButton::Left) breakPending_ = true;
    if (btn == MouseButton::Right) placePending_ = true;
}
void InputMgr::MouseReleaseEvent(MouseButton) {}

bool InputMgr::ConsumeBreakAction() {
    bool v = breakPending_; breakPending_ = false; return v;
}
bool InputMgr::ConsumePlaceAction() {
    bool v = placePending_; placePending_ = false; return v;
}
bool InputMgr::ConsumeActivateAction() {
    bool v = activatePending_; activatePending_ = false; return v;
}

void InputMgr::EndFrame() {
    // held jump stays; look deltas consumed by Game
}
