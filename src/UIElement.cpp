#include "UIElement.h"
#include "Assets.h"
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <algorithm>

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
        // ;$buttonSize = 110 110
        if (line.size() < 3 || line[0] != ';') continue;
        if (line[1] != '$') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(2, eq - 2));
        std::string val = trim(line.substr(eq + 1));
        // strip trailing comment
        std::istringstream vs(val);
        float a = 0, b = 0;
        if (vs >> a) {
            vars_[key] = a;
            if (vs >> b) vars_[key + "#h"] = b;
        }
    }
}

static float evalNum(const std::string& tok, const UIDocument& doc) {
    if (tok.empty()) return 0;
    if (tok[0] == '$') {
        std::string k = tok.substr(1);
        // $offset1 or -$offset1 handled by caller
        return doc.GetVar(k, 0);
    }
    return std::strtof(tok.c_str(), nullptr);
}

static void parseSizeOrPos(const std::string& val, const UIDocument& doc, float& outA, float& outB) {
    // "110 110" or "$buttonSize" or "$offset1 -$offset1"
    std::string s = val;
    // expand $var that has pair
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
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;

        if (line == "UIElement" || line == "SpriteButton" || line == "TextureView") {
            elements_.push_back(UIElement());
            cur = &elements_.back();
            if (line == "SpriteButton") cur->type = UIElement::Type::SpriteButton;
            else if (line == "TextureView") cur->type = UIElement::Type::TextureView;
            continue;
        }
        if (line == "End") { cur = nullptr; continue; }
        if (!cur) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);

        if (key == "Name") cur->name = val;
        else if (key == "Parent") cur->parentName = val;
        else if (key == "Text") cur->text = val;
        else if (key == "Font") cur->fontName = val;
        else if (key == "Visible") cur->visible = (val == "1" || val == "true");
        else if (key == "HitTest") cur->hitTest = (val != "0" && val != "false");
        else if (key == "XAlign") cur->xAlign = (int)std::strtol(val.c_str(), nullptr, 10);
        else if (key == "YAlign") cur->yAlign = (int)std::strtol(val.c_str(), nullptr, 10);
        else if (key == "Size") parseSizeOrPos(val, *this, cur->sizeW, cur->sizeH);
        else if (key == "Position") parseSizeOrPos(val, *this, cur->posX, cur->posY);
    }

    // Link parents
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
    float x = px + posX;
    float y = py + posY;
    if (xAlign == 1) x = px + pw - w - posX;
    if (yAlign == 1) y = py + ph - h + posY; // posY often negative upward in original
    // Original Y: YAlign=1 is bottom; Position y negative moves up from bottom
    if (yAlign == 1 && posY <= 0)
        y = py + ph - h + posY; // posY=-110 → above bottom
    screenRect.x=x; screenRect.y=y; screenRect.w=w; screenRect.h=h;
    for (auto* ch : children)
        ch->ResolveLayout(screenW, screenH);
}

void UIDocument::ResolveAll(float screenW, float screenH) {
    for (auto* r : roots_)
        r->ResolveLayout(screenW, screenH);
}

UIElement* UIDocument::Find(const char* name) {
    for (auto& e : elements_)
        if (e.name == name) return &e;
    return nullptr;
}

UIElement* UIDocument::HitTest(float x, float y) {
    // Prefer deepest leaf
    UIElement* hit = nullptr;
    for (auto& e : elements_) {
        if (!e.visible || !e.hitTest) continue;
        if (e.screenRect.Contains(x, y)) {
            if (!hit || e.screenRect.w * e.screenRect.h < hit->screenRect.w * hit->screenRect.h)
                hit = &e;
        }
    }
    return hit;
}
