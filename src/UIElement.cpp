#include "UIElement.h"
#include "Assets.h"
#include "Loc.h"
#include "GameUIMetrics.h"
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


static void parseTwoFloats(const std::string& val, const UIDocument& doc, float& outA, float& outB) {
    outA = outB = 0.f;
    // "a b" or "$var" (uses #h for second)
    if (val.empty()) return;
    if (val[0] == '$' && val.find(' ') == std::string::npos) {
        std::string name = val.substr(1);
        outA = doc.GetVar(name, 0.f);
        outB = doc.GetVar(name + "#h", outA);
        return;
    }
    size_t sp = val.find(' ');
    if (sp == std::string::npos) {
        outA = parseFloatOrVar(val, doc);
        outB = outA;
        return;
    }
    outA = parseFloatOrVar(trim(val.substr(0, sp)), doc);
    outB = parseFloatOrVar(trim(val.substr(sp + 1)), doc);
}

bool UIDocument::LoadFromText(const std::string& text) {
    elements_.clear();
    roots_.clear();
    vars_.clear();
    colorVars_.clear();
    ParseVars(text);
    elements_.reserve(256);

    std::istringstream in(text);
    std::string line;
    size_t curIdx = (size_t)-1;
    auto cur = [&]() -> UIElement* {
        return curIdx < elements_.size() ? &elements_[curIdx] : nullptr;
    };
    while (std::getline(in, line)) {
        if (!line.empty() && (unsigned char)line[0] == 0xEF && line.size() >= 3)
            line = line.substr(3);
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '$') continue;

        if (line == "UIElement" || line == "UISpriteButton" || line == "UITextureView"
            || line == "UISliderElement") {
            elements_.push_back(UIElement{});
            curIdx = elements_.size() - 1;
            UIElement* e = &elements_[curIdx];
            // seed default styles from document color vars
            e->styleDefault.backgroundColor = GetColorVar("buttonDefaultBackgroundColor", 0);
            e->styleDefault.textColor = GetColorVar("buttonDefaultTextColor", 0xFFDCDCDC);
            e->styleDefault.textShadowColor = GetColorVar("buttonDefaultTextShadowColor", 0x641E1E1E);
            e->styleDefault.frame0Width = GetVar("buttonFrame0Width", 4);
            e->styleDefault.frame1Width = GetVar("buttonFrame1Width", 2);
            e->styleDefault.frame1Color = GetColorVar("buttonDefaultFrameColor", 0x78DCDCDC);
            e->styleDefault.frame2Width = 0;
            e->styleDefault.frame2Color = GetColorVar("selectionFrameColor", 0xFF4080FF);
            e->stylePressed = e->styleDefault;
            e->stylePressed.backgroundColor = GetColorVar("buttonPressedBackgroundColor", 0x64FAFAFA);
            e->stylePressed.textColor = GetColorVar("buttonPressedTextColor", 0xFFFFFFFF);
            e->stylePressed.frame1Color = GetColorVar("buttonPressedFrameColor", 0xFFDCDCDC);
            e->styleSelected = e->styleDefault;
            e->styleSelected.frame0Width = 0;
            e->styleSelected.frame1Width = 0;
            e->styleSelected.frame2Width = 0;
            e->styleSelected.backgroundColor = 0;
            e->styleSelectedPressed = e->stylePressed;
            e->styleSelectedPressed.frame0Width = 0;
            e->styleSelectedPressed.frame1Width = 0;
            e->styleSelectedPressed.frame2Width = 0;
            e->styleSelectedPressed.backgroundColor = 0;
            if (line == "UISpriteButton") e->type = UIElement::Type::SpriteButton;
            else if (line == "UITextureView") e->type = UIElement::Type::TextureView;
            else if (line == "UISliderElement") e->type = UIElement::Type::Slider;
            continue;
        }
        if (line == "End") { curIdx = (size_t)-1; continue; }
        UIElement* c = cur();
        if (!c) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);

        if (key == "Name") c->name = val;
        else if (key == "Parent") c->parentName = val;
        else if (key == "Text") c->text = val;
        else if (key == "Font") c->fontName = val;
        else if (key == "Size") {
            float a = 0, b = 0;
            parseTwoFloats(val, *this, a, b);
            c->sizeW = a; c->sizeH = b;
        } else if (key == "Position") {
            float a = 0, b = 0;
            parseTwoFloats(val, *this, a, b);
            c->posX = a; c->posY = b;
        } else if (key == "HitTest") c->hitTest = (val == "1" || val == "true" || val == "True");
        else if (key == "CurrentValue") {
            c->currentValue = parseFloatOrVar(val, *this);
            if (c->currentValue < 0.f) c->currentValue = 0.f;
            if (c->currentValue > 1.f) c->currentValue = 1.f;
        } else if (key == "Horizontal") {
            c->horizontal = (val == "1" || val == "true" || val == "True");
        } else if (key == "LineWidth") {
            c->lineWidth = parseFloatOrVar(val, *this);
        } else if (key == "Draggable") {
            c->draggable = (val == "1" || val == "true" || val == "True");
        }
        else if (key == "Visible") c->visible = (val != "0" && val != "false");
        else if (key == "XAlign") c->xAlign = std::strtof(val.c_str(), nullptr);
        else if (key == "YAlign") c->yAlign = std::strtof(val.c_str(), nullptr);
        else if (key == "AutoResizeX") {
            float v = parseFloatOrVar(val, *this);
            c->autoResizeX = (v != 0.f);
        } else if (key == "AutoResizeY") {
            float v = parseFloatOrVar(val, *this);
            c->autoResizeY = (v != 0.f);
        }
        // State styles — reuse existing key patterns from previous parser
        else if (key.find("BackgroundColor") != std::string::npos) {
            uint32_t col = parseColorOrVar(val, *this);
            if (key.find("State[1]") != std::string::npos && key.find("Pressed") != std::string::npos)
                c->styleSelectedPressed.backgroundColor = col;
            else if (key.find("State[1]") != std::string::npos)
                c->styleSelected.backgroundColor = col;
            else if (key.find("Pressed") != std::string::npos)
                c->stylePressed.backgroundColor = col;
            else
                c->styleDefault.backgroundColor = col;
        } else if (key.find("TextColor") != std::string::npos && key.find("Shadow") == std::string::npos) {
            uint32_t col = parseColorOrVar(val, *this);
            if (key.find("State[1]") != std::string::npos && key.find("Pressed") != std::string::npos)
                c->styleSelectedPressed.textColor = col;
            else if (key.find("State[1]") != std::string::npos)
                c->styleSelected.textColor = col;
            else if (key.find("Pressed") != std::string::npos)
                c->stylePressed.textColor = col;
            else
                c->styleDefault.textColor = col;
        } else if (key.find("TextShadowColor") != std::string::npos) {
            uint32_t col = parseColorOrVar(val, *this);
            if (key.find("State[1]") != std::string::npos && key.find("Pressed") != std::string::npos)
                c->styleSelectedPressed.textShadowColor = col;
            else if (key.find("State[1]") != std::string::npos)
                c->styleSelected.textShadowColor = col;
            else if (key.find("Pressed") != std::string::npos)
                c->stylePressed.textShadowColor = col;
            else
                c->styleDefault.textShadowColor = col;
        } else if (key.find("BackgroundFrame[0].Width") != std::string::npos) {
            float w = parseFloatOrVar(val, *this);
            if (key.find("State[1]") != std::string::npos && key.find("Pressed") != std::string::npos)
                c->styleSelectedPressed.frame0Width = w;
            else if (key.find("State[1]") != std::string::npos)
                c->styleSelected.frame0Width = w;
            else if (key.find("Pressed") != std::string::npos)
                c->stylePressed.frame0Width = w;
            else
                c->styleDefault.frame0Width = w;
        } else if (key.find("BackgroundFrame[1].Width") != std::string::npos) {
            float w = parseFloatOrVar(val, *this);
            if (key.find("State[1]") != std::string::npos && key.find("Pressed") != std::string::npos)
                c->styleSelectedPressed.frame1Width = w;
            else if (key.find("State[1]") != std::string::npos)
                c->styleSelected.frame1Width = w;
            else if (key.find("Pressed") != std::string::npos)
                c->stylePressed.frame1Width = w;
            else
                c->styleDefault.frame1Width = w;
        } else if (key.find("BackgroundFrame[1].Color") != std::string::npos) {
            uint32_t col = parseColorOrVar(val, *this);
            if (key.find("State[1]") != std::string::npos && key.find("Pressed") != std::string::npos)
                c->styleSelectedPressed.frame1Color = col;
            else if (key.find("State[1]") != std::string::npos)
                c->styleSelected.frame1Color = col;
            else if (key.find("Pressed") != std::string::npos)
                c->stylePressed.frame1Color = col;
            else
                c->styleDefault.frame1Color = col;
        } else if (key.find("BackgroundFrame[2].Width") != std::string::npos) {
            float w = parseFloatOrVar(val, *this);
            if (key.find("State[1]") != std::string::npos && key.find("Pressed") != std::string::npos)
                c->styleSelectedPressed.frame2Width = w;
            else if (key.find("State[1]") != std::string::npos)
                c->styleSelected.frame2Width = w;
            else if (key.find("Pressed") != std::string::npos)
                c->stylePressed.frame2Width = w;
            else
                c->styleDefault.frame2Width = w;
        } else if (key.find("BackgroundFrame[2].Color") != std::string::npos) {
            uint32_t col = parseColorOrVar(val, *this);
            if (key.find("State[1]") != std::string::npos && key.find("Pressed") != std::string::npos)
                c->styleSelectedPressed.frame2Color = col;
            else if (key.find("State[1]") != std::string::npos)
                c->styleSelected.frame2Color = col;
            else if (key.find("Pressed") != std::string::npos)
                c->stylePressed.frame2Color = col;
            else
                c->styleDefault.frame2Color = col;
        }
    }

    // Link parent/child AFTER vector is stable
    std::map<std::string, UIElement*> byName;
    for (auto& e : elements_) {
        e.children.clear();
        e.parent = nullptr;
        if (!e.name.empty()) byName[e.name] = &e;
    }
    roots_.clear();
    for (auto& e : elements_) {
        if (!e.parentName.empty()) {
            auto it = byName.find(e.parentName);
            if (it != byName.end()) {
                e.parent = it->second;
                it->second->children.push_back(&e);
                continue;
            }
        }
        roots_.push_back(&e);
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


void UIDocument::ApplyMetrics(const GameUIMetrics& m) {
    // Design UI files are authored for 800-wide reference ($buttonSize=110).
    // GameUIMetrics::Init scales that reference by logicalW/800.
    const float designBtn = 110.f;
    float s = (designBtn > 0.f) ? ((float)m.buttonSize / designBtn) : 1.f;
    if (s < 0.25f) s = 0.25f;

    m.ApplyToVars(vars_);
    metricsFontSize_ = m.fontSize;

    // Root/frame sizes: physical screen (ResizeToViewport)
    for (auto& e : elements_) {
        bool isRootFill = (e.name == "frame_background" || e.name == "frame" ||
                           e.parentName.empty());
        if (e.name == "frame_background") {
            e.sizeW = (float)m.physicalW;
            e.sizeH = (float)m.physicalH;
            continue;
        }
        if (e.name == "frame" && e.autoResizeX && e.autoResizeY) {
            // windowSize → physical; AutoResize still fills parent at layout
            e.sizeW = (float)m.physicalW;
            e.sizeH = (float)m.physicalH;
            continue;
        }
        // Scale authored geometry
        if (e.sizeW > 0.f) e.sizeW *= s;
        if (e.sizeH > 0.f) e.sizeH *= s;
        e.posX *= s;
        e.posY *= s;
        auto scaleStyle = [s](UIStyle& st) {
            st.frame0Width *= s;
            st.frame1Width *= s;
            st.frame2Width *= s;
        };
        scaleStyle(e.styleDefault);
        scaleStyle(e.stylePressed);
        scaleStyle(e.styleSelected);
        scaleStyle(e.styleSelectedPressed);
    }
}

void UIElement::ResolveLayout(float screenW, float screenH) {
    // Layout in screen pixels. AutoResizeX/Y grow with parent; fixed Size keeps
    // design pixels (no global stretch — matches original: buttons stay 110×110).
    float pw = screenW, ph = screenH, px = 0, py = 0;
    if (parent) {
        pw = parent->screenRect.w;
        ph = parent->screenRect.h;
        px = parent->screenRect.x;
        py = parent->screenRect.y;
    }
    float w = sizeW;
    float h = sizeH;
    if (autoResizeX || w <= 0.f) w = pw;
    if (autoResizeY || h <= 0.f) h = ph;
    // Position = offset from aligned origin inside parent
    float x = px + (pw - w) * xAlign + posX;
    float y = py + (ph - h) * yAlign + posY;
    screenRect.x = x; screenRect.y = y; screenRect.w = w; screenRect.h = h;
    for (auto* ch : children)
        ch->ResolveLayout(screenW, screenH);
}

void UIDocument::ResolveAll(float screenW, float screenH) {
    // Design vars still available for $screenSize references when parsing sizes,
    // but we do NOT scale every rect by screen/design. Root frame_background gets
    // the real screen size via empty/auto size; AutoResize children follow parent.
    float designW = GetVar("screenSize", 800.f);
    float designH = GetVar("screenSize#h", 480.f);
    if (designW < 1.f) designW = 800.f;
    if (designH < 1.f) designH = 480.f;
    (void)designW; (void)designH;

    // Override root sizes that were set to $screenSize/$windowSize design constants
    // so the top frame fills the real viewport (ResizeToViewport behaviour).
    for (auto* r : roots_) {
        if (!r) continue;
        // frame_background / top-level without parent: fill screen
        if (!r->parent) {
            if (r->sizeW <= 0.f || r->name.find("frame_background") != std::string::npos ||
                r->name == "frame_background") {
                r->sizeW = screenW;
                r->sizeH = screenH;
            }
            // If size was design 800×480 and AutoResize on child "frame", still
            // expand background to screen so AutoResize children fill it.
            if (r->name.find("background") != std::string::npos || r->children.empty() == false) {
                r->sizeW = screenW;
                r->sizeH = screenH;
            }
        }
        r->ResolveLayout(screenW, screenH);
    }
    // Second pass: any root that is pure design-sized background already handled.
    // Children with AutoResizeX/Y expand with their parent (e.g. "frame").
    LayoutSliders();
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

UIElement* UIDocument::FindSliderForThumb(UIElement* thumb) {
    if (!thumb || !thumb->parent) return nullptr;
    if (thumb->parent->type == UIElement::Type::Slider)
        return thumb->parent;
    return nullptr;
}

void UIDocument::LayoutSliders() {
    // Binary: slider thumb (child 0) position tracks CurrentValue along axis.
    // Horizontal: x = trackX + value * (trackW - thumbW); vertical analogous.
    for (auto& e : elements_) {
        if (e.type != UIElement::Type::Slider) continue;
        if (e.children.empty()) continue;
        UIElement* thumb = e.children[0];
        float tw = e.screenRect.w;
        float th = e.screenRect.h;
        float kw = thumb->sizeW > 0 ? thumb->sizeW : (thumb->screenRect.w > 0 ? thumb->screenRect.w : 0);
        float kh = thumb->sizeH > 0 ? thumb->sizeH : (thumb->screenRect.h > 0 ? thumb->screenRect.h : 0);
        float v = e.currentValue;
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        if (e.horizontal) {
            float range = tw - kw;
            if (range < 0.f) range = 0.f;
            float x = e.screenRect.x + v * range;
            float y = e.screenRect.y + (th - kh) * 0.5f;
            thumb->screenRect.x = x;
            thumb->screenRect.y = y;
            thumb->screenRect.w = kw;
            thumb->screenRect.h = kh;
            // Keep design pos consistent for parent-relative logic
            thumb->posX = v * range;
            thumb->posY = (th - kh) * 0.5f;
        } else {
            float range = th - kh;
            if (range < 0.f) range = 0.f;
            // Vertical: value 0 at bottom
            float y = e.screenRect.y + (1.f - v) * range;
            float x = e.screenRect.x + (tw - kw) * 0.5f;
            thumb->screenRect.x = x;
            thumb->screenRect.y = y;
            thumb->screenRect.w = kw;
            thumb->screenRect.h = kh;
            thumb->posX = (tw - kw) * 0.5f;
            thumb->posY = (1.f - v) * range;
        }
    }
}

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
    Font* textFont = fonts.FindOrLoad("Sans", metricsFontSize_ > 0 ? metricsFontSize_ : 16);

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

        // UISliderElement::DrawBackground — track line between end frames when LineWidth>0
        if (e.type == UIElement::Type::Slider && e.lineWidth > 0.f &&
            ((st.backgroundColor >> 24) & 0xff) != 0) {
            uint32_t lineCol = st.frame1Color;
            if (((lineCol >> 24) & 0xff) < 0x20)
                lineCol = 0x78DCDCDCu;
            float lw = e.lineWidth;
            if (e.horizontal) {
                float ly = y + (h - lw) * 0.5f;
                float pad = h * 0.5f; // inset roughly half height (thumb radius)
                if (pad * 2.f > w) pad = 0.f;
                drawQuad(x + pad, ly, w - pad * 2.f, lw, lineCol);
            } else {
                float lx = x + (w - lw) * 0.5f;
                float pad = w * 0.5f;
                if (pad * 2.f > h) pad = 0.f;
                drawQuad(lx, y + pad, lw, h - pad * 2.f, lineCol);
            }
        }

        auto drawFrameRing = [&](float fx, float fy, float fw, float fh, float thick, uint32_t col) {
            if (thick <= 0.f || fw <= 0.f || fh <= 0.f) return;
            if (((col >> 24) & 0xff) == 0) return;
            drawQuad(fx, fy, fw, thick, col);
            drawQuad(fx, fy + fh - thick, fw, thick, col);
            drawQuad(fx, fy, thick, fh, col);
            drawQuad(fx + fw - thick, fy, thick, fh, col);
        };

        // Binary DrawBackground composites frame0 then frame1. Observed: our double
        // ring was ~2px thicker than original. When both widths set, outer stroke
        // uses (frame0 - frame1) so total border thickness ≈ frame0 (not frame0+frame1).
        float f0 = st.frame0Width;
        float f1 = st.frame1Width;
        uint32_t fcol = st.frame1Color;
        float outer = f0;
        if (f0 > 0.f && f1 > 0.f)
            outer = f0 - f1; // e.g. 4-2=2 → total visual ≈ 2+2=4 matching design frame0
        if (outer < 0.f) outer = 0.f;
        if (outer > 0.f) {
            uint32_t f0col = fcol;
            uint32_t a = (f0col >> 24) & 0xff;
            if (a > 0x40) f0col = (f0col & 0x00FFFFFFu) | ((a / 2) << 24);
            if (((f0col >> 24) & 0xff) != 0)
                drawFrameRing(x, y, w, h, outer, f0col);
        }
        if (f1 > 0.f && (((fcol >> 24) & 0xff) != 0)) {
            float inset = outer;
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
            // Binary UIElement draw: if (textShadowColor & 0xff000000) AddText(+1,+1, shadow);
            // then AddText(pos, textColor). Offset is 1 device pixel (float 1.0 from rodata).
            uint32_t shadowArgb = st.textShadowColor;
            uint32_t col = argbToRgbaPack(st.textColor);
            if (((st.textColor >> 24) & 0xff) < 0x40)
                col = argbToRgbaPack(0xFFDCDCDCu);
            for (size_t li = 0; li < lines.size(); ++li) {
                const std::string& ln = lines[li];
                if (ln.empty()) continue;
                float tw = textFont->MeasureWidth(ln.c_str(), 1.f);
                float tx = x + (w - tw * scale) * 0.5f;
                float ty = ty0 + (float)li * lineH * scale;
                if ((shadowArgb & 0xff000000) != 0) {
                    textFont->AddTextToMesh(textMesh, Vector(tx + 1.f, ty + 1.f, 0), ln.c_str(),
                                            scale, argbToRgbaPack(shadowArgb), TextAlign::Left);
                }
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

