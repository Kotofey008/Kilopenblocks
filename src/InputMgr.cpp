#include "InputMgr.h"
#include <cmath>
#include <ctime>

double InputMgr::Now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

InputMgr::InputMgr() {}

void InputMgr::TouchPressEvent(unsigned int id, int x, int y) {
    if (id >= kMaxTouches) return;
    touches_[id].active = true;
    touches_[id].x = touches_[id].startX = x;
    touches_[id].y = touches_[id].startY = y;
    touches_[id].pressTime = Now();
}

void InputMgr::TouchMoveEvent(unsigned int id, int x, int y) {
    if (id >= kMaxTouches || !touches_[id].active) return;
    int dx = x - touches_[id].x;
    int dy = y - touches_[id].y;
    touches_[id].x = x;
    touches_[id].y = y;
    // Right half of screen: look; left half: virtual stick (approx)
    if (touches_[id].startX > screenW_ / 2) {
        lookDX_ += dx * sensitivity_;
        lookDY_ += dy * sensitivity_;
    } else {
        float sx = (x - touches_[id].startX) / 80.f;
        float sy = (touches_[id].startY - y) / 80.f; // up = forward
        if (sx < -1) sx = -1; if (sx > 1) sx = 1;
        if (sy < -1) sy = -1; if (sy > 1) sy = 1;
        moveStrafe_ = sx;
        moveForward_ = sy;
    }
}

void InputMgr::TouchReleaseEvent(unsigned int id, int x, int y) {
    if (id >= kMaxTouches) return;
    if (touches_[id].active) {
        double held = Now() - touches_[id].pressTime;
        int dx = x - touches_[id].startX;
        int dy = y - touches_[id].startY;
        int dist2 = dx*dx + dy*dy;
        // Short tap with little movement → action
        if (dist2 < 20*20) {
            if (held < 0.35)
                breakPending_ = true;   // short tap = dig
            else
                placePending_ = true;   // long press = place
        }
        if (touches_[id].startX <= screenW_ / 2) {
            moveForward_ = moveStrafe_ = 0.f;
        }
    }
    touches_[id].active = false;
}

void InputMgr::MousePressEvent(MouseButton btn) {
    int i = (int)btn;
    if (i < 0 || i >= 3) return;
    mouseDown_[i] = true;
    mousePressed_[i] = true;
    if (btn == MouseButton::Left) breakPending_ = true;
    if (btn == MouseButton::Right) placePending_ = true;
}

void InputMgr::MouseReleaseEvent(MouseButton btn) {
    int i = (int)btn;
    if (i < 0 || i >= 3) return;
    mouseDown_[i] = false;
    mouseReleased_[i] = true;
}

bool InputMgr::IsMouseButtonPressed(MouseButton btn) const {
    int i = (int)btn; return i >= 0 && i < 3 && mousePressed_[i];
}
bool InputMgr::IsMouseButtonReleased(MouseButton btn) const {
    int i = (int)btn; return i >= 0 && i < 3 && mouseReleased_[i];
}
bool InputMgr::IsMouseButtonDown(MouseButton btn) const {
    int i = (int)btn; return i >= 0 && i < 3 && mouseDown_[i];
}

bool InputMgr::ConsumeBreakAction() {
    bool v = breakPending_; breakPending_ = false; return v;
}
bool InputMgr::ConsumePlaceAction() {
    bool v = placePending_; placePending_ = false; return v;
}

void InputMgr::EndFrame() {
    for (int i = 0; i < 3; ++i) {
        mousePressed_[i] = false;
        mouseReleased_[i] = false;
    }
}
