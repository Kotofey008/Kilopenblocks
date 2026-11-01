#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// UIElement hierarchy recovered from symbols + assets/ui/hud.txt
// Layout uses XAlign/YAlign (0=left/top, 1=right/bottom) and Position offsets.

struct UIRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
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
    int xAlign = 0, yAlign = 0; // 0 left/top, 1 right/bottom
    float posX = 0, posY = 0;
    float sizeW = 0, sizeH = 0;
    UIRect screenRect; // resolved each frame
    UIElement* parent = nullptr;
    std::vector<UIElement*> children;

    void ResolveLayout(float screenW, float screenH);
};

class UIDocument {
public:
    bool LoadFromText(const std::string& text);
    bool LoadAsset(const char* assetPath); // e.g. "ui/hud.txt"
    void ResolveAll(float screenW, float screenH);
    UIElement* Find(const char* name);
    UIElement* HitTest(float x, float y); // topmost visible with hitTest
    const std::vector<UIElement*>& Roots() const { return roots_; }
    std::vector<UIElement>& All() { return elements_; }

    // Variables from ;$name = value lines
    float GetVar(const std::string& name, float def = 0.f) const;

private:
    std::vector<UIElement> elements_;
    std::vector<UIElement*> roots_;
    std::map<std::string, float> vars_;
    std::map<std::string, float> vars2_; // for "W H" pairs store W as name, H as name#h
    void ParseVars(const std::string& text);
};
