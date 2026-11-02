#include "UIElement.h"
#include "Assets.h"
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <cstring>
#include <cstdio>

#if defined(__ANDROID__)
#include <GLES/gl.h>
#endif

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

uint32_t UIDocument::GetColorVar(const std::string& name, uint32_t def) const {
    auto it = colorVars_.find(name);
    return it == colorVars_.end() ? def : it->second;
}

static uint32_t parseHexColor(const std::string& s) {
    // "78DCDCDC" or "FFDCDCDC" → AARRGGBB
    if (s.empty()) return 0;
    if (s[0] == '$') return 0; // resolved later
    unsigned long v = std::strtoul(s.c_str(), nullptr, 16);
    if (s.size() <= 6) {
        // RRGGBB → force opaque
        v |= 0xFF000000u;
    }
    return (uint32_t)v;
}

void UIDocument::ParseVars(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        // ;$buttonSize = 110 110
        if (line.size() >= 3 && line[0] == ';' && line[1] == '$') {
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
            continue;
        }
        // $buttonDefaultFrameColor = 78DCDCDC
        if (line.size() >= 2 && line[0] == '$') {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(1, eq - 1));
            std::string val = trim(line.substr(eq + 1));
            if (!val.empty() && val[0] == '$') {
                // reference another color var — resolve later if known
                std::string ref = val.substr(1);
                if (colorVars_.count(ref)) colorVars_[key] = colorVars_[ref];
            } else {
                colorVars_[key] = parseHexColor(val);
            }
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

static float parseFloatOrVar(const std::string& val, const UIDocument& doc) {
    if (val.empty()) return 0;
    if (val[0] == '$') return doc.GetVar(val.substr(1), 0);
    if (val[0] == '-' && val.size() > 1 && val[1] == '$')
        return -doc.GetVar(val.substr(2), 0);
    return std::strtof(val.c_str(), nullptr);
}

static uint32_t parseColorOrVar(const std::string& val, const UIDocument& doc) {
    if (val.empty()) return 0;
    if (val[0] == '$') return doc.GetColorVar(val.substr(1), 0xFFFFFFFF);
    return parseHexColor(val);
}

bool UIDocument::LoadFromText(const std::string& text) {
    elements_.clear();
    roots_.clear();
    vars_.clear();
    colorVars_.clear();
    ParseVars(text);

    std::istringstream in(text);
    std::string line;
    UIElement* cur = nullptr;
    while (std::getline(in, line)) {
        if (!line.empty() && (unsigned char)line[0] == 0xEF && line.size() >= 3)
            line = line.substr(3);
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '$') continue;

        if (line == "UIElement" || line == "UISpriteButton" || line == "UITextureView"
            || line == "UISliderElement") {
            elements_.push_back(UIElement{});
            cur = &elements_.back();
            // seed default styles from document color vars
            cur->styleDefault.backgroundColor = GetColorVar("buttonDefaultBackgroundColor", 0);
            cur->styleDefault.textColor = GetColorVar("buttonDefaultTextColor", 0xFFDCDCDC);
            cur->styleDefault.textShadowColor = GetColorVar("buttonDefaultTextShadowColor", 0x641E1E1E);
            cur->styleDefault.frame0Width = GetVar("buttonFrame0Width", 4);
            cur->styleDefault.frame1Width = GetVar("buttonFrame1Width", 2);
            cur->styleDefault.frame1Color = GetColorVar("buttonDefaultFrameColor", 0x78DCDCDC);
            cur->stylePressed = cur->styleDefault;
            cur->stylePressed.backgroundColor = GetColorVar("buttonPressedBackgroundColor", 0x64FAFAFA);
            cur->stylePressed.textColor = GetColorVar("buttonPressedTextColor", 0xFFFFFFFF);
            cur->stylePressed.frame1Color = GetColorVar("buttonPressedFrameColor", 0xFFDCDCDC);
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
        // State styles
        else if (key.find("State[0].Default.BackgroundColor") != std::string::npos)
            cur->styleDefault.backgroundColor = parseColorOrVar(val, *this);
        else if (key.find("State[0].Default.TextColor") != std::string::npos)
            cur->styleDefault.textColor = parseColorOrVar(val, *this);
        else if (key.find("State[0].Default.TextShadowColor") != std::string::npos)
            cur->styleDefault.textShadowColor = parseColorOrVar(val, *this);
        else if (key.find("State[0].Default.BackgroundFrame[0].Width") != std::string::npos)
            cur->styleDefault.frame0Width = parseFloatOrVar(val, *this);
        else if (key.find("State[0].Default.BackgroundFrame[1].Width") != std::string::npos)
            cur->styleDefault.frame1Width = parseFloatOrVar(val, *this);
        else if (key.find("State[0].Default.BackgroundFrame[1].Color") != std::string::npos)
            cur->styleDefault.frame1Color = parseColorOrVar(val, *this);
        else if (key.find("State[0].Pressed.BackgroundColor") != std::string::npos)
            cur->stylePressed.backgroundColor = parseColorOrVar(val, *this);
        else if (key.find("State[0].Pressed.TextColor") != std::string::npos)
            cur->stylePressed.textColor = parseColorOrVar(val, *this);
        else if (key.find("State[0].Pressed.TextShadowColor") != std::string::npos)
            cur->stylePressed.textShadowColor = parseColorOrVar(val, *this);
        else if (key.find("State[0].Pressed.BackgroundFrame[0].Width") != std::string::npos)
            cur->stylePressed.frame0Width = parseFloatOrVar(val, *this);
        else if (key.find("State[0].Pressed.BackgroundFrame[1].Width") != std::string::npos)
            cur->stylePressed.frame1Width = parseFloatOrVar(val, *this);
        else if (key.find("State[0].Pressed.BackgroundFrame[1].Color") != std::string::npos)
            cur->stylePressed.frame1Color = parseColorOrVar(val, *this);
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
    if (text.empty()) return "";
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
    float x = px + (pw - w) * xAlign + posX;
    float y = py + (ph - h) * yAlign + posY;
    screenRect.x = x; screenRect.y = y; screenRect.w = w; screenRect.h = h;
    for (auto* ch : children)
        ch->ResolveLayout(screenW, screenH);
}

void UIDocument::ResolveAll(float screenW, float screenH) {
    float designW = GetVar("screenSize", 800.f);
    float designH = GetVar("screenSize#h", 480.f);
    if (designW < 1.f) designW = 800.f;
    if (designH < 1.f) designH = 480.f;
    const float sx = screenW / designW;
    const float sy = screenH / designH;
    for (auto* r : roots_)
        r->ResolveLayout(designW, designH);
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
        if (area < bestArea) { bestArea = area; hit = &e; }
    }
    return hit;
}

// ---------------------------------------------------------------------------
// UIElement::DrawBackground / DrawText path (simplified geometry matching styles)
// Full binary DrawBackground is 4750 B (multi-frame borders, clipping FRect).
// We emit the same visual components: fill, frame rings, label via Font.
// ---------------------------------------------------------------------------
void UIDocument::Draw(float screenW, float screenH, FontResource& fonts) const {
#if !defined(__ANDROID__)
    (void)screenW; (void)screenH; (void)fonts;
    return;
#else
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0, screenW, screenH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto splitARGB = [](uint32_t c, float& r, float& g, float& b, float& a) {
        a = ((c >> 24) & 0xff) / 255.f;
        r = ((c >> 16) & 0xff) / 255.f;
        g = ((c >> 8) & 0xff) / 255.f;
        b = (c & 0xff) / 255.f;
    };
    auto drawQuad = [&](float x, float y, float w, float h, uint32_t argb) {
        if (w <= 0 || h <= 0) return;
        if (((argb >> 24) & 0xff) == 0) return; // fully transparent
        float r,g,b,a; splitARGB(argb, r,g,b,a);
        glDisable(GL_TEXTURE_2D);
        glColor4f(r, g, b, a);
        const GLfloat verts[] = { x,y, x+w,y, x+w,y+h, x,y+h };
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableClientState(GL_VERTEX_ARRAY);
    };

    // Collect text mesh per font
    MeshBuffer textMesh;
    Font* textFont = fonts.FindOrLoad("Sans", 16);
    uint32_t lastTextColor = 0;

    for (const auto& e : elements_) {
        if (!e.visible) continue;
        if (e.screenRect.w < 1.f || e.screenRect.h < 1.f) continue;
        // Skip pure layout frames with HitTest=0 and no text (frame_background)
        if (!e.hitTest && e.DisplayText().empty() && e.name.find("crosshair") == std::string::npos)
            continue;

        const UIStyle& st = e.CurrentStyle();
        float x = e.screenRect.x, y = e.screenRect.y, w = e.screenRect.w, h = e.screenRect.h;

        // Crosshair elements: thin filled rects
        if (e.name.find("crosshair") != std::string::npos) {
            drawQuad(x, y, w, h, 0xFFFFFFFFu);
            continue;
        }

        // DrawBackground: fill
        drawQuad(x, y, w, h, st.backgroundColor);

        // Frame rings (frame0 outer inset, frame1 colored border)
        float f0 = st.frame0Width;
        float f1 = st.frame1Width;
        if (f1 > 0.f && (st.frame1Color & 0xFF000000u)) {
            // outer ring of thickness f1
            drawQuad(x, y, w, f1, st.frame1Color);
            drawQuad(x, y + h - f1, w, f1, st.frame1Color);
            drawQuad(x, y, f1, h, st.frame1Color);
            drawQuad(x + w - f1, y, f1, h, st.frame1Color);
        }
        (void)f0;

        // DrawText
        std::string label = e.DisplayText();
        if (!label.empty() && textFont && textFont->IsLoaded()) {
            // scale text to fit roughly inside button
            float scale = 1.f;
            float tw = textFont->MeasureWidth(label.c_str(), 1.f);
            float th = textFont->LineHeight();
            if (tw > 1.f && tw * scale > w * 0.85f) scale = (w * 0.85f) / tw;
            if (th * scale > h * 0.7f) scale = (h * 0.7f) / th;
            float tx = x + (w - tw * scale) * 0.5f;
            float ty = y + (h - th * scale) * 0.5f;
            // shadow
            textFont->AddTextToMesh(textMesh, Vector(tx + 1.f, ty + 1.f, 0), label.c_str(),
                                    scale, st.textShadowColor, TextAlign::Left);
            textFont->AddTextToMesh(textMesh, Vector(tx, ty, 0), label.c_str(),
                                    scale, st.textColor, TextAlign::Left);
            lastTextColor = st.textColor;
        }
    }

    // Flush text mesh with font texture
    if (textFont && textMesh.IndexCount() > 0) {
        glEnable(GL_TEXTURE_2D);
        textFont->GetTexture().Bind();
        glColor4f(1, 1, 1, 1);
        // DrawPrimitive style client arrays
        const auto& verts = textMesh.Vertices();
        const auto& inds = textMesh.Indices();
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        const char* base = reinterpret_cast<const char*>(verts.data());
        glVertexPointer(3, GL_FLOAT, 24, base + 0);
        glColorPointer(4, GL_UNSIGNED_BYTE, 24, base + 12);
        glTexCoordPointer(2, GL_FLOAT, 24, base + 16);
        glDrawElements(GL_TRIANGLES, (int)inds.size(), GL_UNSIGNED_SHORT, inds.data());
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisable(GL_TEXTURE_2D);
    }
    (void)lastTextColor;
    glEnable(GL_DEPTH_TEST);
    glColor4f(1,1,1,1);
#endif
}
