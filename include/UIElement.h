#pragma once
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

class UIElement {
public:
    enum class Type { Element = 0, SpriteButton = 1, TextureView = 2, Slider = 3 };

    std::string name;
    std::string parentName;
    std::string text; // "/Label/visible text" or plain
    std::string fontName = "Sans";
    Type type = Type::Element;
    bool visible = true;
    bool hitTest = true;
    float xAlign = 0.f, yAlign = 0.f; // 0 left/top, 0.5 center, 1 right/bottom
    float posX = 0, posY = 0;
    float sizeW = 0, sizeH = 0;
    UIRect screenRect;
    UIElement* parent = nullptr;
    std::vector<UIElement*> children;
    bool pressed = false;

    void ResolveLayout(float screenW, float screenH);
    // Visible caption after last '/' in Text="/Id/Caption"
    std::string DisplayText() const;
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
    bool Loaded() const { return !elements_.empty(); }

private:
    std::vector<UIElement> elements_;
    std::vector<UIElement*> roots_;
    std::map<std::string, float> vars_;
    void ParseVars(const std::string& text);
};
