#pragma once
#include "Font.h"
class Texture;
class GridData;
#include "Mesh.h"
#include "GameUIMetrics.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

struct UIRect {
    float x = 0, y = 0, w = 0, h = 0;
    UIRect() = default;
    UIRect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    bool Contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

// Style recovered from hud.txt / menu.txt:
//   State[0].Default / State[0].Pressed  — normal + touch
//   State[1].Default / State[1].Pressed  — selected (palette slot / page tab)
// Colors AARRGGBB. BackgroundFrame[2] = selection ring
// ($selectionFrameColor = FF4080FF on menu_mtl_*).
// Binary style block is 0x2c bytes (GetCurrentStyle * 0x2c + stateStyles):
//   +0x00 frame[0].width (float)  +0x04 frame[0].color (u32 ARGB)
//   +0x08 frame[1].width          +0x0c frame[1].color
//   +0x10 frame[2].width          +0x14 frame[2].color
//   +0x18 backgroundColor
//   +0x20 textColor               +0x24 textShadowColor
// DrawBackground @ 0x103b60: for i in 0..2: if width!=0 inset; if color alpha draw ring;
// then fill remaining with backgroundColor (if alpha).
// NOTE: frame0 Color often unset in UI files → color=0 → inset only, no stroke.
struct UIStyle {
    float frame0Width = 0.f;
    uint32_t frame0Color = 0;      // binary default 0
    float frame1Width = 0.f;
    uint32_t frame1Color = 0;      // binary default 0 (not invented)
    float frame2Width = 0.f;
    uint32_t frame2Color = 0;
    uint32_t backgroundColor = 0;
    uint32_t textColor = 0xFFDCDCDC;
    uint32_t textShadowColor = 0x641E1E1E;
    // binary style +0x28 — Texture*/TAObject* used by DrawImage
    Texture* texture = nullptr;
};


class UIElement {
public:
    enum class Type { Element = 0, SpriteButton = 1, TextureView = 2, Slider = 3, GridModelMtl = 4 };

    std::string name;
    std::string parentName;
    std::string text;
    std::string fontName = "Sans";
    Type type = Type::Element;
    bool visible = true;
    bool hitTest = true;
    float xAlign = 0.f, yAlign = 0.f;
    float posX = 0, posY = 0;
    float sizeW = 0, sizeH = 0;
    bool autoResizeX = false; // AutoResizeX — grow with parent width
    bool autoResizeY = false; // AutoResizeY — grow with parent height
    UIRect screenRect;
    UIElement* parent = nullptr;
    std::vector<UIElement*> children;
    bool pressed = false;
    bool selected = false; // State[1] — palette slot / page tab
    bool draggable = false; // Draggable = 1 (slider thumb)
    // UISliderElement fields (binary +0x1e0 value, +0x1e4 horizontal, +0x1e8 lineWidth)
    float currentValue = 0.f; // 0..1
    bool horizontal = true;
    float lineWidth = 8.f;
    bool isDragging = false;
    // Binary UIElement touch flags (GetTouchState):
    //  0x4000 pressed, 0x8000 clicked (press+release), 0x10000 released edge
    static constexpr uint32_t kTouchPressed  = 0x4000;
    static constexpr uint32_t kTouchClicked  = 0x8000;
    static constexpr uint32_t kTouchReleased = 0x10000;
    uint32_t touchState = 0;
    bool useTouchMove = false; // GetUseTouchMove
    // UIGridModelMtl (menu.txt): ModelName, Materials[], ArrayWidth
    std::string modelName;
    std::vector<int> materials;
    int arrayWidth = 3;
    // DrawImage: optional texture name (Texture / Bitmap)
    std::string textureName;

    UIStyle styleDefault;          // State[0].Default
    UIStyle stylePressed;          // State[0].Pressed
    UIStyle styleSelected;         // State[1].Default
    UIStyle styleSelectedPressed;  // State[1].Pressed

    void ResolveLayout(float screenW, float screenH);
    std::string DisplayText() const;
    const UIStyle& CurrentStyle() const {
        if (selected)
            return pressed ? styleSelectedPressed : styleSelected;
        return pressed ? stylePressed : styleDefault;
    }
};

class UIDocument {
public:
    bool LoadFromText(const std::string& text);
    bool LoadAsset(const char* assetPath);
    void ResolveAll(float screenW, float screenH);
    // Override $-vars from GameUIMetrics and scale geometry from design-800
    void ApplyMetrics(const GameUIMetrics& m);
    // Position slider thumbs from CurrentValue; call after ResolveAll
    void LayoutSliders();
    UIElement* FindSliderForThumb(UIElement* thumb);
    void SetVisiblePrefix(const std::string& prefix, bool visible);
    void SetAllVisible(bool visible);
    UIElement* Find(const char* name);
    UIElement* HitTest(float x, float y);
    // Binary UIElement::HitTestH — children reverse order, then self if hitTest+visible
    UIElement* HitTestH(float x, float y);
    const std::vector<UIElement*>& Roots() const { return roots_; }
    std::vector<UIElement>& All() { return elements_; }
    float GetVar(const std::string& name, float def = 0.f) const;
    void SetVar(const std::string& name, float v) { vars_[name] = v; }
    uint32_t GetColorVar(const std::string& name, uint32_t def = 0) const;
    bool Loaded() const { return !elements_.empty(); }

    // Draw path: Draw → roots DrawH → DrawElement (BG + Image + Text) per binary vtable
    void Draw(float screenW, float screenH, FontResource& fonts) const;
    // Binary UIElement::DrawH @ 0x107080 — hierarchical draw + clip
    void DrawH(UIElement* e, float parentX, float parentY, float parentW, float parentH,
               float clipX, float clipY, float clipW, float clipH,
               FontResource& fonts) const;
    // Single element: DrawBackground + DrawImage + DrawText (vtable 0x20/0x24/0x28)
    void DrawElement(const UIElement& e, float clipX, float clipY, float clipW, float clipH,
                     FontResource& fonts) const;
    void SetBlocksAtlas(Texture* t) { blocksAtlas_ = t; }
    Texture* BlocksAtlas() const { return blocksAtlas_; }
    void SetGridData(GridData* g) { gridForMtl_ = g; }
    GridData* GetGridData() const { return gridForMtl_; }
    // Binary UpdateDragAction @ 0x107570 — drag thumb / free drag inside parent
    void UpdateDragAction(UIElement* e, float touchX, float touchY);
    Texture* LoadUITexture(const char* path);

private:
    std::vector<UIElement> elements_;
    std::vector<UIElement*> roots_;
    std::map<std::string, float> vars_;
    std::map<std::string, uint32_t> colorVars_;
    void ParseVars(const std::string& text);
    int metricsFontSize_ = 16;
    GridData* gridForMtl_ = nullptr;
    Texture* blocksAtlas_ = nullptr; // blocks.png for UIGridModelMtl
    std::map<std::string, std::unique_ptr<Texture>> textureCache_;
public:
    int MetricsFontSize() const { return metricsFontSize_; }
};
