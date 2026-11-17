#pragma once
#include <map>
#include <string>
#include <cmath>
#include <cstdio>

// ScreenSizeEnum — ApplicationBase / Game
// GetDefaultScreenSize() → 0 (Phone)
// GetAllowedScreenSizes: always Phone; if max(w,h)>799 add SmallTablet;
//   if max(w,h)>=1024 (0x400) add Tablet. Returns count 1..3.
enum class ScreenSizeEnum {
    Phone = 0,
    SmallTablet = 1,
    Tablet = 2
};

// GameUIMetrics::Init(int w, int h, ScreenSizeEnum) @ 0x6d890
// Produces the $-variables substituted into ui/*.txt (GetAsStrMap @ 0x6f4f0).
// Core: buttonSize = even(round(110 * logicalW / 800)), etc.
struct GameUIMetrics {
    int physicalW = 800;
    int physicalH = 480;
    int logicalW = 800;  // +0x10
    int logicalH = 480;  // +0x14
    ScreenSizeEnum sizeEnum = ScreenSizeEnum::Phone;

    // Derived UI metrics (pixels) — names match $vars in ui/*.txt
    int autoResize = 1;
    int windowYPos = 0;
    int fontSize = 16;
    int buttonFrame0Width = 4;
    int buttonFrame1Width = 2;
    int buttonFrame2Width = 4;
    int buttonSize = 110;
    int button2x1W = 220, button2x1H = 110;
    int button3x1W = 330, button3x1H = 110;
    int jumpButtonSize = 110;
    int valuesSize = 220;
    int sliderLineWidth = 8;
    int sliderValueW = 110, sliderValueH = 44;
    int hSliderW = 330, hSliderH = 110;
    int hSliderValueOffset = 455;
    int vSliderW = 110, vSliderH = 330;
    int vSliderValueOffset = -185;
    int offset1 = 110, offset2 = 220, offset3 = 330;
    int margin1 = 70, margin2 = 180, marginA = 235, margin3 = 290, margin4 = 400;
    int mtlSetsW = 322, mtlSetsH = 75;
    int mtlSetsPosX = -74, mtlSetsPosY = -203;
    int mtlSetNSize = 46; // height 40 in design — scaled
    int mtlSetNHeight = 40;
    int nextMtlSetW = 70, nextMtlSetH = 165;
    int prevMtlSetW = 70, prevMtlSetH = 165;
    int mtlPanelSize = 330;
    int screenshotW = 330, screenshotH = 198;
    int playButtonW = 398, playButtonH = 266;
    int messageW = 322, messageH = 58;
    int crosshairWidth = 2, crosshairHeight = 16;
    int windowNameH = 40;

    static int EvenRound(float v) {
        int i = (int)std::floor(v + 0.5f);
        return i & ~1; // clear lowest bit — matches `& 0xfffffffe`
    }

    // ApplicationBase::GetAllowedScreenSizes
    static int GetAllowed(int screenW, int screenH, ScreenSizeEnum out[3]) {
        int m = screenW > screenH ? screenW : screenH;
        out[0] = ScreenSizeEnum::Phone;
        int n = 1;
        if (m > 799) {
            out[n++] = ScreenSizeEnum::SmallTablet;
            if (m >= 0x400) // 1024
                out[n++] = ScreenSizeEnum::Tablet;
        }
        return n;
    }

    void Init(int screenW, int screenH, ScreenSizeEnum e) {
        physicalW = screenW > 0 ? screenW : 800;
        physicalH = screenH > 0 ? screenH : 480;
        sizeEnum = e;
        logicalW = physicalW;
        logicalH = physicalH;

        // Recovered scale factors from .rodata used by Init:
        // 0.55 SmallTablet, 0.60 Tablet; 0.5 rounding; 110/800 button ratio.
        const float kHalf = 0.5f;
        const float kBtn = 110.f;
        const float kRefW = 800.f;

        if (e == ScreenSizeEnum::SmallTablet) {
            logicalW = EvenRound((float)physicalW * 0.55f);
        } else if (e == ScreenSizeEnum::Tablet) {
            logicalW = EvenRound((float)physicalW * 0.60f);
        } else {
            // Phone special cases from binary (iPhone-class letterbox widths)
            if (physicalW == 0x470 && physicalH == 0x280) { // 1136x640
                logicalW = 0x3c0; // 960
            } else if (physicalW == 0x238 && physicalH == 0x140) { // 568x320
                logicalW = 0x1e0; // 480
            } else {
                logicalW = physicalW;
            }
        }
        // Clamp: logicalW <= 2 * height (binary)
        if (logicalW > physicalH * 2)
            logicalW = physicalH * 2;
        if (logicalW < 2) logicalW = 2;

        // Keep aspect for logicalH from physical
        logicalH = physicalH;
        // If we shrank width for tablet modes, height stays physical (layout in mixed space).
        // Binary keeps +0x14 = param_2 (physical height).

        auto scale = [&](float design) -> int {
            return EvenRound(design * (float)logicalW / kRefW);
        };
        auto scaleOddOk = [&](float design) -> int {
            return (int)std::floor(design * (float)logicalW / kRefW + kHalf);
        };

        buttonSize = scale(kBtn);
        if (buttonSize < 44) buttonSize = 44;

        button2x1W = scale(220.f); button2x1H = buttonSize;
        button3x1W = scale(330.f); button3x1H = buttonSize;
        jumpButtonSize = buttonSize;
        valuesSize = scale(220.f);
        sliderLineWidth = std::max(2, scaleOddOk(8.f));
        sliderValueW = buttonSize;
        sliderValueH = scale(44.f);
        hSliderW = scale(330.f); hSliderH = buttonSize;
        hSliderValueOffset = scale(455.f);
        vSliderW = buttonSize; vSliderH = scale(330.f);
        vSliderValueOffset = -scale(185.f);
        offset1 = buttonSize;
        offset2 = buttonSize * 2;
        offset3 = buttonSize * 3;
        margin1 = scale(70.f);
        margin2 = scale(180.f);
        marginA = scale(235.f);
        margin3 = scale(290.f);
        margin4 = scale(400.f);
        mtlSetsW = scale(322.f); mtlSetsH = scale(75.f);
        mtlSetsPosX = -scale(74.f);
        // Design: YAlign 0.5 + posY → strip TOP at frame TOP.
        // y=(frameH-setsH)*0.5+posY=0 → posY=-(frameH-setsH)/2
        // Design 480×75 → -202.5≈-203. Track layout height, not width-only scale.
        {
            int refH = scale(480.f);
            if (refH < 1) refH = 480;
            mtlSetsPosY = -EvenRound(0.5f * (float)(refH - mtlSetsH));
        }
        mtlSetNSize = scale(46.f); mtlSetNHeight = scale(40.f);
        nextMtlSetW = scale(70.f); nextMtlSetH = scale(165.f);
        prevMtlSetW = scale(70.f); prevMtlSetH = scale(165.f);
        mtlPanelSize = scale(330.f);
        screenshotW = scale(330.f); screenshotH = scale(198.f);
        playButtonW = scale(398.f); playButtonH = scale(266.f);
        messageW = scale(322.f); messageH = scale(58.f);
        // frames scale lightly with button
        buttonFrame0Width = std::max(2, scaleOddOk(4.f));
        buttonFrame1Width = std::max(1, scaleOddOk(2.f));
        buttonFrame2Width = std::max(2, scaleOddOk(4.f));
        crosshairWidth = std::max(1, scaleOddOk(2.f));
        crosshairHeight = std::max(8, scaleOddOk(16.f));
        windowNameH = scale(40.f);
        windowYPos = 0;
        autoResize = 1;

        // Font size: design UI reads closer to ~20–22 at 800-wide ref (font_22 available).
        // Binary LoadFont uses metrics-derived size; 16 was undersized vs original HUD/menu.
        fontSize = scaleOddOk(22.f);
        if (fontSize < 14) fontSize = 14;
        if (fontSize > 48) fontSize = 48;
    }

    // Inject into UIDocument var map (overrides ;$ defaults from txt)
    void ApplyToVars(std::map<std::string, float>& vars) const {
        auto set2 = [&](const char* key, float a, float b) {
            vars[key] = a;
            vars[std::string(key) + "#h"] = b;
        };
        set2("screenSize", (float)physicalW, (float)physicalH);
        set2("windowSize", (float)physicalW, (float)physicalH);
        vars["windowYPos"] = (float)windowYPos;
        vars["autoResize"] = (float)autoResize;
        set2("windowNameSize", (float)physicalW, (float)windowNameH);
        vars["buttonFrame0Width"] = (float)buttonFrame0Width;
        vars["buttonFrame1Width"] = (float)buttonFrame1Width;
        vars["buttonFrame2Width"] = (float)buttonFrame2Width;
        set2("buttonSize", (float)buttonSize, (float)buttonSize);
        set2("button2x1Size", (float)button2x1W, (float)button2x1H);
        set2("button3x1Size", (float)button3x1W, (float)button3x1H);
        set2("jumpButtonSize", (float)jumpButtonSize, (float)jumpButtonSize);
        set2("valuesSize", (float)valuesSize, (float)valuesSize);
        vars["sliderLineWidth"] = (float)sliderLineWidth;
        set2("sliderValueSize", (float)sliderValueW, (float)sliderValueH);
        set2("hSliderSize", (float)hSliderW, (float)hSliderH);
        vars["hSliderValueOffset"] = (float)hSliderValueOffset;
        set2("vSliderSize", (float)vSliderW, (float)vSliderH);
        vars["vSliderValueOffset"] = (float)vSliderValueOffset;
        vars["offset1"] = (float)offset1;
        vars["offset2"] = (float)offset2;
        vars["offset3"] = (float)offset3;
        vars["margin1"] = (float)margin1;
        vars["margin2"] = (float)margin2;
        vars["marginA"] = (float)marginA;
        vars["margin3"] = (float)margin3;
        vars["margin4"] = (float)margin4;
        set2("mtlSetsSize", (float)mtlSetsW, (float)mtlSetsH);
        set2("mtlSetsPos", (float)mtlSetsPosX, (float)mtlSetsPosY);
        set2("mtlSetNSize", (float)mtlSetNSize, (float)mtlSetNHeight);
        // mtlSetN positions are multiples of width
        vars["mtlSet0Pos"] = 0.f; vars["mtlSet0Pos#h"] = 0.f;
        for (int i = 1; i <= 6; ++i) {
            char k[32];
            std::snprintf(k, sizeof(k), "mtlSet%dPos", i);
            vars[k] = (float)(mtlSetNSize * i);
            vars[std::string(k) + "#h"] = 0.f;
        }
        set2("nextMtlSetSize", (float)nextMtlSetW, (float)nextMtlSetH);
        set2("prevMtlSetSize", (float)prevMtlSetW, (float)prevMtlSetH);
        // next/prev positions from design
        set2("nextMtlSetPos", 0.f, (float)(-EvenRound(83.f * (float)logicalW / 800.f)));
        set2("prevMtlSetPos", 0.f, (float)EvenRound(82.f * (float)logicalW / 800.f));
        set2("mtlPanelSize", (float)mtlPanelSize, (float)mtlPanelSize);
        vars["mtlScale"] = (float)std::max(32, EvenRound(64.f * (float)logicalW / 800.f));
        set2("screenshotSize", (float)screenshotW, (float)screenshotH);
        set2("playButtonSize", (float)playButtonW, (float)playButtonH);
        vars["playButtonTextPos"] = 0.f;
        vars["playButtonTextPos#h"] = (float)EvenRound(166.f * (float)logicalW / 800.f);
        set2("messageSize", (float)messageW, (float)messageH);
        vars["crosshairWidth"] = (float)crosshairWidth;
        vars["crosshairHeight"] = (float)crosshairHeight;
    }

};
