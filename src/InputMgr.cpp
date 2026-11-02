#include "InputMgr.h"
#include <cstring>

InputMgr::InputMgr() {}

const char* InputMgr::HitHudButton(int x, int y) const {
    if (!hudRects_.valid) return nullptr;
    float fx = (float)x, fy = (float)y;
    if (hudRects_.up.Contains(fx, fy)) return "hud_up";
    if (hudRects_.down.Contains(fx, fy)) return "hud_down";
    if (hudRects_.left.Contains(fx, fy)) return "hud_left";
    if (hudRects_.right.Contains(fx, fy)) return "hud_right";
    if (hudRects_.jump.Contains(fx, fy)) return "hud_jump";
    if (hudRects_.del.Contains(fx, fy)) return "hud_delete";
    if (hudRects_.add.Contains(fx, fy)) return "hud_add";
    if (hudRects_.act.Contains(fx, fy)) return "hud_activate";
    if (hudRects_.menu.Contains(fx, fy)) return "hud_menu";
    return nullptr;
}

void InputMgr::RecomputeHeldMovement() {
    moveForward_ = 0.f;
    moveStrafe_ = 0.f;
    jump_ = false;
    for (int i = 0; i < kMaxTouches; ++i) {
        if (!touches_[i].active || !touches_[i].button) continue;
        const char* b = touches_[i].button;
        if (std::strcmp(b, "hud_up") == 0) moveForward_ += 1.f;
        else if (std::strcmp(b, "hud_down") == 0) moveForward_ -= 1.f;
        else if (std::strcmp(b, "hud_left") == 0) moveStrafe_ -= 1.f;
        else if (std::strcmp(b, "hud_right") == 0) moveStrafe_ += 1.f;
        else if (std::strcmp(b, "hud_jump") == 0) jump_ = true;
    }
    if (moveForward_ > 1.f) moveForward_ = 1.f;
    if (moveForward_ < -1.f) moveForward_ = -1.f;
    if (moveStrafe_ > 1.f) moveStrafe_ = 1.f;
    if (moveStrafe_ < -1.f) moveStrafe_ = -1.f;
}

void InputMgr::TouchPressEvent(unsigned int id, int x, int y) {
    if (id >= kMaxTouches) return;
    touches_[id].active = true;
    touches_[id].x = touches_[id].startX = x;
    touches_[id].y = touches_[id].startY = y;
    touches_[id].button = nullptr;

    // In menu mode: no look / movement from HUD pad (Game handles menu hits)
    if (menuMode_) {
        touches_[id].zone = Zone::None;
        return;
    }

    const char* btn = HitHudButton(x, y);
    if (btn) {
        touches_[id].zone = Zone::HudButton;
        touches_[id].button = btn;
        if (std::strcmp(btn, "hud_delete") == 0) breakPending_ = true;
        else if (std::strcmp(btn, "hud_add") == 0) placePending_ = true;
        else if (std::strcmp(btn, "hud_activate") == 0) activatePending_ = true;
        else if (std::strcmp(btn, "hud_menu") == 0) menuPending_ = true;
        RecomputeHeldMovement();
        return;
    }
    touches_[id].zone = Zone::Look;
}

void InputMgr::TouchMoveEvent(unsigned int id, int x, int y) {
    if (id >= kMaxTouches || !touches_[id].active) return;
    int dx = x - touches_[id].x;
    int dy = y - touches_[id].y;
    touches_[id].x = x;
    touches_[id].y = y;

    if (touches_[id].zone == Zone::Look) {
        lookDX_ += (float)dx * sensitivity_;
        lookDY_ += (float)dy * sensitivity_;
    } else if (touches_[id].zone == Zone::HudButton && touches_[id].button) {
        // TouchOver: stay on button or cancel if slid off (discrete hold)
        const char* now = HitHudButton(x, y);
        if (now && std::strcmp(now, touches_[id].button) == 0) {
            // still on same button
        } else if (now && (std::strcmp(touches_[id].button, "hud_up") == 0
                        || std::strcmp(touches_[id].button, "hud_down") == 0
                        || std::strcmp(touches_[id].button, "hud_left") == 0
                        || std::strcmp(touches_[id].button, "hud_right") == 0
                        || std::strcmp(touches_[id].button, "hud_jump") == 0)) {
            // allow slide between movement buttons
            touches_[id].button = now;
        } else if (!now) {
            // slid off — clear this touch's contribution
            touches_[id].button = nullptr;
        }
        RecomputeHeldMovement();
    }
}

void InputMgr::TouchReleaseEvent(unsigned int id, int x, int y) {
    if (id >= kMaxTouches) return;
    (void)x; (void)y;
    touches_[id].active = false;
    touches_[id].zone = Zone::None;
    touches_[id].button = nullptr;
    RecomputeHeldMovement();
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

bool InputMgr::IsHudButtonHeld(const char* name) const {
    if (!name) return false;
    for (int i = 0; i < kMaxTouches; ++i) {
        if (!touches_[i].active || !touches_[i].button) continue;
        if (std::strcmp(touches_[i].button, name) == 0) return true;
    }
    return false;
}
bool InputMgr::ConsumeMenuAction() {
    bool v = menuPending_; menuPending_ = false; return v;
}

void InputMgr::EndFrame() {}

bool InputMgr::AnyLookTouch() const {
    for (int i = 0; i < kMaxTouches; ++i)
        if (touches_[i].active && touches_[i].zone == Zone::Look) return true;
    return false;
}
