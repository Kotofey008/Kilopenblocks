#pragma once
#include "Font.h"
#include "Mesh.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

struct UIRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

// Style recovered from hud.txt / menu.txt:
//   State[0].Default / State[0].Pressed  — normal + touch
//   State[1].Default / State[1].Pressed  — selected (palette slot / page tab)
// Colors AARRGGBB. BackgroundFrame[2] = selection ring
// ($selectionFrameColor = FF4080FF on menu_mtl_*).
struct UIStyle {
    uint32_t backgroundColor = 0;
    uint32_t textColor = 0xFFDCDCDC;
    uint32_t textShadowColor = 0x641E1E1E;
    float frame0Width = 0; // outer frame
    float frame1Width = 0; // inner frame
    uint32_t frame1Color = 0x78DCDCDC;
    float frame2Width = 0; // selection frame (State[1] on menu_mtl_*)
    uint32_t frame2Color = 0xFF4080FF;
};

class UIElement {
public:
    enum class Type { Element = 0, SpriteButton = 1, TextureView = 2, Slider = 3 };

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
    UIRect screenRect;
    UIElement* parent = nullptr;
    std::vector<UIElement*> children;
    bool pressed = false;
    bool selected = false; // State[1] — palette slot / page tab

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
    void SetVisiblePrefix(const std::string& prefix, bool visible);
    void SetAllVisible(bool visible);
    UIElement* Find(const char* name);
    UIElement* HitTest(float x, float y);
    const std::vector<UIElement*>& Roots() const { return roots_; }
    std::vector<UIElement>& All() { return elements_; }
    float GetVar(const std::string& name, float def = 0.f) const;
    void SetVar(const std::string& name, float v) { vars_[name] = v; }
    uint32_t GetColorVar(const std::string& name, uint32_t def = 0) const;
    bool Loaded() const { return !elements_.empty(); }

    // Original draw path stand-in for UIElement::Draw → DrawBackground + DrawText
    // (full 4.7KB DrawBackground not ported; frame+fill+text matches observed style fields)
    void Draw(float screenW, float screenH, FontResource& fonts) const;

private:
    std::vector<UIElement> elements_;
    std::vector<UIElement*> roots_;
    std::map<std::string, float> vars_;
    std::map<std::string, uint32_t> colorVars_;
    void ParseVars(const std::string& text);
};
