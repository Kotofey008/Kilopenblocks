#include "UIElement.h"
#include "Assets.h"
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <cstring>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

float UIDocument::GetVar(const std::string& name, float def) const {
    auto it = vars_.find(name);
    return it == vars_.end() ? def : it->second;
}

void UIDocument::ParseVars(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.size() < 3 || line[0] != ';' || line[1] != '$') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(2, eq - 2));
        std::string val = trim(line.substr(eq + 1));
        std::istringstream vs(val);
        float a = 0, b = 0;
        if (vs >> a) {
            vars_[key] = a;
            if (vs >> b) vars_[key + "#h"] = b;
        }
    }
}

static void parseSizeOrPos(const std::string& val, const UIDocument& doc, float& outA, float& outB) {
    std::string s = val;
    if (s.size() > 1 && s[0] == '$' && s.find(' ') == std::string::npos) {
        std::string k = s.substr(1);
        outA = doc.GetVar(k, 0);
        outB = doc.GetVar(k + "#h", outA);
        return;
    }
    std::istringstream iss(s);
    std::string t1, t2;
    iss >> t1 >> t2;
    auto one = [&](const std::string& t) -> float {
        if (t.empty()) return 0;
        int sign = 1;
        size_t i = 0;
        if (t[0] == '-') { sign = -1; i = 1; }
        if (i < t.size() && t[i] == '$')
            return sign * doc.GetVar(t.substr(i + 1), 0);
        return sign * std::strtof(t.c_str(), nullptr);
    };
    outA = one(t1);
    outB = one(t2);
}

bool UIDocument::LoadFromText(const std::string& text) {
    elements_.clear();
    roots_.clear();
    vars_.clear();
    ParseVars(text);

    std::istringstream in(text);
    std::string line;
    UIElement* cur = nullptr;
    while (std::getline(in, line)) {
        if (!line.empty() && (unsigned char)line[0] == 0xEF && line.size() >= 3)
            line = line.substr(3);
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '$') continue; // color vars

        if (line == "UIElement" || line == "UISpriteButton" || line == "UITextureView"
            || line == "UISliderElement") {
            elements_.push_back(UIElement{});
            cur = &elements_.back();
            if (line == "UISpriteButton") cur->type = UIElement::Type::SpriteButton;
            else if (line == "UITextureView") cur->type = UIElement::Type::TextureView;
            else if (line == "UISliderElement") cur->type = UIElement::Type::Slider;
            continue;
        }
        if (line == "End") { cur = nullptr; continue; }
        if (!cur) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        // strip quotes
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);

        if (key == "Name") cur->name = val;
        else if (key == "Parent") cur->parentName = val;
        else if (key == "Text") cur->text = val;
        else if (key == "Font") cur->fontName = val;
        else if (key == "HitTest") cur->hitTest = (val == "1" || val == "true" || val == "True");
        else if (key == "Visible") cur->visible = (val != "0" && val != "false");
        else if (key == "XAlign") cur->xAlign = std::strtof(val.c_str(), nullptr);
        else if (key == "YAlign") cur->yAlign = std::strtof(val.c_str(), nullptr);
        else if (key == "Size") parseSizeOrPos(val, *this, cur->sizeW, cur->sizeH);
        else if (key == "Position") parseSizeOrPos(val, *this, cur->posX, cur->posY);
    }

    std::map<std::string, UIElement*> byName;
    for (auto& e : elements_) byName[e.name] = &e;
    for (auto& e : elements_) {
        if (!e.parentName.empty()) {
            auto it = byName.find(e.parentName);
            if (it != byName.end()) {
                e.parent = it->second;
                it->second->children.push_back(&e);
            }
        } else {
            roots_.push_back(&e);
        }
    }
    return !elements_.empty();
}

bool UIDocument::LoadAsset(const char* assetPath) {
    std::string text;
    if (!Assets::ReadText(assetPath, text)) return false;
    return LoadFromText(text);
}

std::string UIElement::DisplayText() const {
    // "/MoveForward/↑" → "↑" ; "Menu" → "Menu"
    if (text.empty()) return name;
    size_t last = text.rfind('/');
    if (last != std::string::npos && last + 1 < text.size())
        return text.substr(last + 1);
    return text;
}

void UIElement::ResolveLayout(float screenW, float screenH) {
    float pw = screenW, ph = screenH, px = 0, py = 0;
    if (parent) {
        pw = parent->screenRect.w;
        ph = parent->screenRect.h;
        px = parent->screenRect.x;
        py = parent->screenRect.y;
    }
    float w = sizeW > 0 ? sizeW : pw;
    float h = sizeH > 0 ? sizeH : ph;

    // Align within parent: 0=left/top, 0.5=center, 1=right/bottom
    float x = px + (pw - w) * xAlign + posX;
    float y = py + (ph - h) * yAlign + posY;

    screenRect.x = x;
    screenRect.y = y;
    screenRect.w = w;
    screenRect.h = h;
    for (auto* ch : children)
        ch->ResolveLayout(screenW, screenH);
}

void UIDocument::ResolveAll(float screenW, float screenH) {
    // Map design 800x480 vars onto actual screen for root frames
    float designW = GetVar("screenSize", 800.f);
    float designH = GetVar("screenSize#h", 480.f);
    if (designW < 1.f) designW = 800.f;
    if (designH < 1.f) designH = 480.f;
    float sx = screenW / designW;
    float sy = screenH / designH;
    float scale = std::min(sx, sy);
    // Prefer filling width for phone landscape; keep aspect of design
    // Override root sizes to actual screen so children position in screen space
    SetVar("screenSize", screenW);
    SetVar("screenSize#h", screenH);
    SetVar("windowSize", screenW);
    SetVar("windowSize#h", screenH);
    // Scale offsets/buttons proportionally
    auto scaleKey = [&](const char* k) {
        if (vars_.count(k)) vars_[k] *= scale;
        std::string kh = std::string(k) + "#h";
        if (vars_.count(kh)) vars_[kh] *= scale;
    };
    // Re-parse sizes from scaled vars: re-apply element sizes
    // Simpler path: resolve with design coords then scale all screenRects
    // Reset vars from originals first — we already overwrote. Use scale on rects after resolve.

    // Restore button metrics relative to design then scale layout result
    // Actually: resolve in design space then uniform scale to screen
    SetVar("screenSize", designW);
    SetVar("screenSize#h", designH);
    SetVar("windowSize", designW);
    SetVar("windowSize#h", designH);

    // Re-read sizes from vars for elements that use $buttonSize etc.
    // Elements already stored absolute size at load time. Scale them:
    for (auto& e : elements_) {
        e.screenRect = {};
    }
    // Scale stored sizes/positions once based on design→screen
    static bool scaled = false;
    // Always work from load-time values: keep originals in sizeW; apply scale factor each resolve
    for (auto* r : roots_) {
        // Temporarily scale size/pos for resolve
        std::vector<std::pair<UIElement*, UIElement>> backup;
        (void)backup;
        r->ResolveLayout(designW, designH);
    }
    // Map design-space rects onto screen (letterbox optional — stretch fill)
    for (auto& e : elements_) {
        e.screenRect.x *= sx;
        e.screenRect.y *= sy;
        e.screenRect.w *= sx;
        e.screenRect.h *= sy;
    }
}

void UIDocument::SetVisiblePrefix(const std::string& prefix, bool visible) {
    for (auto& e : elements_) {
        if (e.name.size() >= prefix.size() &&
            e.name.compare(0, prefix.size(), prefix) == 0)
            e.visible = visible;
    }
}

void UIDocument::SetAllVisible(bool visible) {
    for (auto& e : elements_) e.visible = visible;
}

UIElement* UIDocument::Find(const char* name) {
    for (auto& e : elements_)
        if (e.name == name) return &e;
    return nullptr;
}

UIElement* UIDocument::HitTest(float x, float y) {
    UIElement* hit = nullptr;
    float bestArea = 1e30f;
    for (auto& e : elements_) {
        if (!e.visible || !e.hitTest) continue;
        if (!e.screenRect.Contains(x, y)) continue;
        float area = e.screenRect.w * e.screenRect.h;
        if (area < bestArea) {
            bestArea = area;
            hit = &e;
        }
    }
    return hit;
}
