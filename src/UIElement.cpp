#include "UIElement.h"
#include "Assets.h"
#include "Loc.h"
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
            cur->styleDefault.frame2Width = 0;
            cur->styleDefault.frame2Color = GetColorVar("selectionFrameColor", 0xFF4080FF);
            cur->stylePressed = cur->styleDefault;
            cur->stylePressed.backgroundColor = GetColorVar("buttonPressedBackgroundColor", 0x64FAFAFA);
            cur->stylePressed.textColor = GetColorVar("buttonPressedTextColor", 0xFFFFFFFF);
            cur->stylePressed.frame1Color = GetColorVar("buttonPressedFrameColor", 0xFFDCDCDC);
            // State[1] starts from State[0] colors but zero frames until menu.txt
            // sets BackgroundFrame[*] (menu_mtl_* has frame2; menu_mtl_set_* only fill).
            cur->styleSelected = cur->styleDefault;
            cur->styleSelected.frame0Width = 0;
            cur->styleSelected.frame1Width = 0;
            cur->styleSelected.frame2Width = 0;
            cur->styleSelected.backgroundColor = 0;
            cur->styleSelectedPressed = cur->stylePressed;
            cur->styleSelectedPressed.frame0Width = 0;
            cur->styleSelectedPressed.frame1Width = 0;
            cur->styleSelectedPressed.frame2Width = 0;
            cur->styleSelectedPressed.backgroundColor = 0;
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
        // ---- State[0] Default ----
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
        else if (key.find("State[0].Default.BackgroundFrame[2].Width") != std::string::npos)
            cur->styleDefault.frame2Width = parseFloatOrVar(val, *this);
        else if (key.find("State[0].Default.BackgroundFrame[2].Color") != std::string::npos)
            cur->styleDefault.frame2Color = parseColorOrVar(val, *this);
        // ---- State[0] Pressed ----
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
        else if (key.find("State[0].Pressed.BackgroundFrame[2].Width") != std::string::npos)
            cur->stylePressed.frame2Width = parseFloatOrVar(val, *this);
        else if (key.find("State[0].Pressed.BackgroundFrame[2].Color") != std::string::npos)
            cur->stylePressed.frame2Color = parseColorOrVar(val, *this);
        // ---- State[1] Default (selected) ----
        else if (key.find("State[1].Default.BackgroundColor") != std::string::npos)
            cur->styleSelected.backgroundColor = parseColorOrVar(val, *this);
        else if (key.find("State[1].Default.TextColor") != std::string::npos)
            cur->styleSelected.textColor = parseColorOrVar(val, *this);
        else if (key.find("State[1].Default.TextShadowColor") != std::string::npos)
            cur->styleSelected.textShadowColor = parseColorOrVar(val, *this);
        else if (key.find("State[1].Default.BackgroundFrame[0].Width") != std::string::npos)
            cur->styleSelected.frame0Width = parseFloatOrVar(val, *this);
        else if (key.find("State[1].Default.BackgroundFrame[1].Width") != std::string::npos)
            cur->styleSelected.frame1Width = parseFloatOrVar(val, *this);
        else if (key.find("State[1].Default.BackgroundFrame[1].Color") != std::string::npos)
            cur->styleSelected.frame1Color = parseColorOrVar(val, *this);
        else if (key.find("State[1].Default.BackgroundFrame[2].Width") != std::string::npos)
            cur->styleSelected.frame2Width = parseFloatOrVar(val, *this);
        else if (key.find("State[1].Default.BackgroundFrame[2].Color") != std::string::npos)
            cur->styleSelected.frame2Color = parseColorOrVar(val, *this);
        // ---- State[1] Pressed (selected + touch) ----
        else if (key.find("State[1].Pressed.BackgroundColor") != std::string::npos)
            cur->styleSelectedPressed.backgroundColor = parseColorOrVar(val, *this);
        else if (key.find("State[1].Pressed.TextColor") != std::string::npos)
            cur->styleSelectedPressed.textColor = parseColorOrVar(val, *this);
        else if (key.find("State[1].Pressed.TextShadowColor") != std::string::npos)
            cur->styleSelectedPressed.textShadowColor = parseColorOrVar(val, *this);
        else if (key.find("State[1].Pressed.BackgroundFrame[0].Width") != std::string::npos)
            cur->styleSelectedPressed.frame0Width = parseFloatOrVar(val, *this);
        else if (key.find("State[1].Pressed.BackgroundFrame[1].Width") != std::string::npos)
            cur->styleSelectedPressed.frame1Width = parseFloatOrVar(val, *this);
        else if (key.find("State[1].Pressed.BackgroundFrame[1].Color") != std::string::npos)
            cur->styleSelectedPressed.frame1Color = parseColorOrVar(val, *this);
        else if (key.find("State[1].Pressed.BackgroundFrame[2].Width") != std::string::npos)
            cur->styleSelectedPressed.frame2Width = parseFloatOrVar(val, *this);
        else if (key.find("State[1].Pressed.BackgroundFrame[2].Color") != std::string::npos)
            cur->styleSelectedPressed.frame2Color = parseColorOrVar(val, *this);
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
    // Binary: Text = "/LocKey/Fallback" → GLApplication::Localize (0x6fd50)
    // looks up LocKey in loc/<lang>.csv; on miss uses the Fallback segment.
    // Newlines from CSV are preserved (multi-line button labels like "New\nTerrain").
    return Localization::Instance().Localize(text);
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
    // ------------------------------------------------------------------
    // Critical: world pass leaves GL_COLOR_ARRAY / GL_TEXTURE_COORD_ARRAY
    // enabled and blocksAtlas bound. With those client states active,
    // glColor4f is ignored and untextured quads sample the block atlas.
    // Original UI path draws after 3D with a clean 2D state.
    // ------------------------------------------------------------------
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

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
        g = ((c >>  8) & 0xff) / 255.f;
        b = ( c        & 0xff) / 255.f;
    };
    // Pack as little-endian RGBA bytes for glColorPointer UNSIGNED_BYTE
    auto argbToRgbaPack = [](uint32_t argb) -> uint32_t {
        uint32_t a = (argb >> 24) & 0xff;
        uint32_t r = (argb >> 16) & 0xff;
        uint32_t g = (argb >>  8) & 0xff;
        uint32_t b =  argb        & 0xff;
        return r | (g << 8) | (b << 16) | (a << 24);
    };
    auto drawQuad = [&](float x, float y, float w, float h, uint32_t argb) {
        if (w <= 0.f || h <= 0.f) return;
        uint32_t aa = (argb >> 24) & 0xff;
        if (aa == 0) return;
        float r,g,b,a; splitARGB(argb, r,g,b,a);
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glColor4f(r, g, b, a);
        const GLfloat verts[] = { x,y, x+w,y, x+w,y+h, x,y+h };
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableClientState(GL_VERTEX_ARRAY);
        glColor4f(1, 1, 1, 1);
    };

    MeshBuffer textMesh;
    Font* textFont = fonts.FindOrLoad("Sans", 16);

    for (const auto& e : elements_) {
        if (!e.visible) continue;
        if (e.screenRect.w < 1.f || e.screenRect.h < 1.f) continue;

        // Layout-only frames: skip drawing (still used for layout / hit parents)
        if (e.name == "frame_background" || e.name == "frame") continue;
        if (e.name == "menu_mtl_sets") continue; // container
        // GM0 material panel is drawn separately via DrawMaterialSlots (block atlas)

        const UIStyle& st = e.CurrentStyle();
        float x = e.screenRect.x, y = e.screenRect.y, w = e.screenRect.w, h = e.screenRect.h;

        if (e.name.find("crosshair") != std::string::npos) {
            drawQuad(x, y, w, h, 0xFFFFFFFFu);
            continue;
        }

        // Skip pure image/model placeholders that have no text and transparent bg
        // menu_mtl_N: hit targets over material grid — no text; frames only when selected later
        bool isMtlSlot = (e.name.compare(0, 9, "menu_mtl_") == 0 && e.name.size() == 10);

        // DrawBackground fill (UIElement::DrawBackground @ 0xf3b60)
        // Outer frame0 (often 4px) is a soft border; frame1 is the hard outline;
        // frame2 is the selection ring drawn outside the rect.
        drawQuad(x, y, w, h, st.backgroundColor);

        auto drawFrameRing = [&](float fx, float fy, float fw, float fh, float thick, uint32_t col) {
            if (thick <= 0.f || fw <= 0.f || fh <= 0.f) return;
            if (((col >> 24) & 0xff) == 0) return;
            drawQuad(fx, fy, fw, thick, col);
            drawQuad(fx, fy + fh - thick, fw, thick, col);
            drawQuad(fx, fy, thick, fh, col);
            drawQuad(fx + fw - thick, fy, thick, fh, col);
        };

        float f0 = st.frame0Width;
        if (f0 > 0.f) {
            // Outer frame uses a dimmer variant of frame1 when no explicit color
            uint32_t f0col = st.frame1Color;
            uint32_t a = (f0col >> 24) & 0xff;
            if (a > 0x40) f0col = (f0col & 0x00FFFFFFu) | ((a / 2) << 24);
            drawFrameRing(x, y, w, h, f0, f0col);
        }

        float f1 = st.frame1Width;
        uint32_t fcol = st.frame1Color;
        if (f1 < 1.f && e.hitTest && !isMtlSlot) f1 = 2.f;
        if (f1 > 0.f) {
            if (((fcol >> 24) & 0xff) < 0x20) fcol = (fcol & 0x00FFFFFFu) | 0xA0000000u;
            // Inset frame1 inside frame0 so double-border matches binary look
            float inset = f0 > 0.f ? f0 : 0.f;
            drawFrameRing(x + inset, y + inset, w - inset * 2.f, h - inset * 2.f, f1, fcol);
        }

        // Selection frame (State[1] BackgroundFrame[2] — $selectionFrameColor)
        float f2 = st.frame2Width;
        uint32_t f2col = st.frame2Color;
        if (f2 > 0.f && (((f2col >> 24) & 0xff) > 0)) {
            drawFrameRing(x - f2, y - f2, w + f2 * 2.f, h + f2 * 2.f, f2, f2col);
        }

        // DrawText — multi-line labels (CSV may contain newlines)
        std::string label = e.DisplayText();
        if (!label.empty() && textFont && textFont->IsLoaded()) {
            // Split into lines
            std::vector<std::string> lines;
            {
                std::string cur;
                for (char ch : label) {
                    if (ch == '\n' || ch == '\r') {
                        if (ch == '\r') continue;
                        lines.push_back(cur);
                        cur.clear();
                    } else {
                        cur.push_back(ch);
                    }
                }
                lines.push_back(cur);
            }
            float scale = 1.f;
            float maxLineW = 0.f;
            for (const auto& ln : lines)
                maxLineW = std::max(maxLineW, textFont->MeasureWidth(ln.c_str(), 1.f));
            float lineH = textFont->LineHeight();
            float blockH = lineH * (float)lines.size();
            if (maxLineW > 1.f && maxLineW * scale > w * 0.90f) scale = (w * 0.90f) / maxLineW;
            if (blockH * scale > h * 0.85f && blockH > 0.f) scale = (h * 0.85f) / blockH;
            if (scale < 0.45f) scale = 0.45f;
            float ty0 = y + (h - blockH * scale) * 0.5f;
            uint32_t shadow = argbToRgbaPack(st.textShadowColor);
            uint32_t col = argbToRgbaPack(st.textColor);
            if (((st.textColor >> 24) & 0xff) < 0x40)
                col = argbToRgbaPack(0xFFDCDCDCu);
            for (size_t li = 0; li < lines.size(); ++li) {
                const std::string& ln = lines[li];
                if (ln.empty()) continue;
                float tw = textFont->MeasureWidth(ln.c_str(), 1.f);
                float tx = x + (w - tw * scale) * 0.5f;
                float ty = ty0 + (float)li * lineH * scale;
                textFont->AddTextToMesh(textMesh, Vector(tx + 1.f, ty + 1.f, 0), ln.c_str(),
                                        scale, shadow, TextAlign::Left);
                textFont->AddTextToMesh(textMesh, Vector(tx, ty, 0), ln.c_str(),
                                        scale, col, TextAlign::Left);
            }
        }
    }

    // Flush text with font texture — client state only for this pass
    if (textFont && textFont->IsLoaded() && textMesh.IndexCount() > 0) {
        glEnable(GL_TEXTURE_2D);
        textFont->GetTexture().Bind();
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(1, 1, 1, 1);
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
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glColor4f(1, 1, 1, 1);
#endif
}

