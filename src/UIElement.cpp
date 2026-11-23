#include <cmath>
#include "GridData.h"
#include "Texture.h"
#include <functional>
#include "UIElement.h"
#include "Assets.h"
#include "Loc.h"
#include "GameUIMetrics.h"
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <cstring>
#include <strings.h>
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
            || line == "UISliderElement" || line == "UIGridModelMtl") {
            elements_.push_back(UIElement{});
            curIdx = elements_.size() - 1;
            UIElement* e = &elements_[curIdx];
            // Binary UIElement::Style::Style + Frame::Frame (0x...):
            //   Frame[0..2]: Width=0, Color=0
            //   BackgroundColor=0; ImageColor=0xFFFFFFFF; TextColor=0xFFFFFFFF; Shadow=0
            // DrawBackground early-out: if bg alpha==0 AND all three frame widths==0 → return
            // Frames only when UI .txt sets BackgroundFrame[*] via SetProperty.
            // Do NOT seed $buttonFrame* / $buttonDefault* onto every element.
            e->styleDefault = UIStyle{};
            e->styleDefault.textColor = 0xFFFFFFFFu;
            e->stylePressed = e->styleDefault;
            e->styleSelected = e->styleDefault;
            e->styleSelectedPressed = e->styleDefault;
            if (line == "UISpriteButton") e->type = UIElement::Type::SpriteButton;
            else if (line == "UITextureView") e->type = UIElement::Type::TextureView;
            else if (line == "UISliderElement") e->type = UIElement::Type::Slider;
            else if (line == "UIGridModelMtl") e->type = UIElement::Type::GridModelMtl;
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
        } else if (key == "ModelName") {
            c->modelName = val;
        } else if (key == "ArrayWidth") {
            c->arrayWidth = (int)parseFloatOrVar(val, *this);
            if (c->arrayWidth < 1) c->arrayWidth = 1;
        } else if (key == "ArrayHeight") {
            c->arrayHeight = (int)parseFloatOrVar(val, *this);
            if (c->arrayHeight < 1) c->arrayHeight = 1;
        } else if (key.size() >= 10 && key.compare(0, 10, "Materials[") == 0) {
            int idx = 0;
            if (std::sscanf(key.c_str(), "Materials[%d]", &idx) == 1) {
                int mid = (int)parseFloatOrVar(val, *this);
                if ((int)c->materials.size() <= idx) c->materials.resize((size_t)idx + 1, 0);
                c->materials[(size_t)idx] = mid;
            }
        } else if (key == "Materials.Size") {
            int n = (int)parseFloatOrVar(val, *this);
            if (n > 0) c->materials.resize((size_t)n, 0);
        } else if (key == "Texture" || key == "Bitmap" || key == "BitmapFileName") {
            c->textureName = val;
        }
        else if (key == "Visible") c->visible = (val != "0" && val != "false");
        else if (key == "TextAlign") {
            // Binary SetProperty → +0x80/+0x84; "Left"/"Right"/"Center" or float
            if (strcasecmp(val.c_str(), "Left") == 0) { c->textAlignX = 0.f; }
            else if (strcasecmp(val.c_str(), "Right") == 0) { c->textAlignX = 1.f; }
            else if (strcasecmp(val.c_str(), "Center") == 0) { c->textAlignX = 0.5f; }
            else {
                float a = 0.f, b = 0.5f;
                // may be single float or pair
                int n = std::sscanf(val.c_str(), "%f %f", &a, &b);
                if (n >= 1) c->textAlignX = a;
                if (n >= 2) c->textAlignY = b;
            }
        } else if (key == "TextPosition") {
            float a = 0, b = 0;
            parseTwoFloats(val, *this, a, b);
            c->textPosX = a; c->textPosY = b;
        } else if (key == "XAlign") c->xAlign = std::strtof(val.c_str(), nullptr);
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
        } else if (key.find("BackgroundFrame[0].Color") != std::string::npos) {
            uint32_t col = parseColorOrVar(val, *this);
            if (key.find("State[1]") != std::string::npos && key.find("Pressed") != std::string::npos)
                c->styleSelectedPressed.frame0Color = col;
            else if (key.find("State[1]") != std::string::npos)
                c->styleSelected.frame0Color = col;
            else if (key.find("Pressed") != std::string::npos)
                c->stylePressed.frame0Color = col;
            else
                c->styleDefault.frame0Color = col;
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
        // LineWidth (+0x1e8) is metrics-scaled in binary LoadUIObjects via $sliderLineWidth.
        // After design parse, scale it the same as other sizes.
        if (e.lineWidth > 0.f)
            e.lineWidth *= s;
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
    // After scale: rewrite menu_mtl_sets posY from metrics (height-correct), not
    // from design -203 * widthScale. Binary GameUIMetrics substitutes $mtlSetsPos.
    if (UIElement* sets = Find("menu_mtl_sets")) {
        sets->posY = (float)m.mtlSetsPosY;
        sets->posX = (float)m.mtlSetsPosX;
        if (sets->sizeW > 0.f) sets->sizeW = (float)m.mtlSetsW;
        if (sets->sizeH > 0.f) sets->sizeH = (float)m.mtlSetsH;
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

    // ---- Palette strip pin (menu.txt design relation) ----
    // Design 800×480:
    //   menu_mtl_sets bottom == menu_mtl_0 top (top row of 3×3 slot buttons)
    //   y_sets = (frameH-75)*0.5 + (-203) = frameTop
    //   y_mtl0 = (frameH-110)*0.5 + (-110) = frameTop+75
    // Metrics posY based on width scale breaks when frame.h ≠ designH*scale.
    // Previous pin forced newY=parentTop on both sides of a comparison → no-op
    // whenever ResolveLayout already produced that, and never used mtl0.
    // Fix: set posY so sets.bottom == mtl0.top, then re-ResolveLayout the subtree.
    {
        UIElement* sets = Find("menu_mtl_sets");
        UIElement* mtl0 = Find("menu_mtl_0");
        if (sets && sets->screenRect.h > 0.f && mtl0 && mtl0->screenRect.h > 0.f) {
            float parentTop = sets->parent ? sets->parent->screenRect.y : 0.f;
            float parentH = sets->parent ? sets->parent->screenRect.h : screenH;
            float h = sets->screenRect.h;

            // Primary: bottom of page tabs = top of first material slot button
            float newY = mtl0->screenRect.y - h;

            // Also never overlap the GridModelMtl panel itself
            if (UIElement* grid = Find("menu_GM0_mtl")) {
                if (grid->screenRect.h > 0.f) {
                    float byGrid = grid->screenRect.y - h;
                    if (byGrid < newY) // smaller Y = higher on screen
                        newY = byGrid;
                }
            }

            // Clamp inside parent vertically
            if (newY < parentTop)
                newY = parentTop;
            if (newY + h > parentTop + parentH)
                newY = parentTop + parentH - h;

            // Always rewrite posY from the target screen Y so the next full
            // ResolveLayout of the document still lands correctly mid-frame.
            // y = parentTop + (parentH - h)*yAlign + posY
            sets->posY = newY - parentTop - (parentH - h) * sets->yAlign;

            // Apply immediately: parent rect + all page-tab children
            if (sets->screenRect.y != newY || true) {
                sets->screenRect.y = newY;
                sets->screenRect.h = h;
                // Children are laid out relative to parent screenRect
                for (auto* ch : sets->children) {
                    if (!ch) continue;
                    ch->ResolveLayout(screenW, screenH);
                }
            }
        }
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
    // Prefer hierarchical HitTestH (binary path used by UISceneObject)
    return HitTestH(x, y);
}


// Binary UIElement::HitTestH @ 0x107220:
//   for child in reverse: HitTestH(child); if hit return
//   else if visible && hitTest && point in rect → this
UIElement* UIDocument::HitTestH(float x, float y) {
    std::function<UIElement*(UIElement*)> rec = [&](UIElement* e) -> UIElement* {
        if (!e || !e->visible) return nullptr;
        for (int i = (int)e->children.size() - 1; i >= 0; --i) {
            if (UIElement* h = rec(e->children[i])) return h;
        }
        if (e->hitTest && e->screenRect.Contains(x, y))
            return e;
        return nullptr;
    };
    for (int i = (int)roots_.size() - 1; i >= 0; --i) {
        if (UIElement* h = rec(roots_[i])) return h;
    }
    return nullptr;
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
            // Vertical (binary): value 0 at TOP, 1 at BOTTOM.
            // With UpdateTerrainSize index=(1-v)*(n-1)+0.5 → top=largest size.
            float y = e.screenRect.y + v * range;
            float x = e.screenRect.x + (tw - kw) * 0.5f;
            thumb->screenRect.x = x;
            thumb->screenRect.y = y;
            thumb->screenRect.w = kw;
            thumb->screenRect.h = kh;
            thumb->posX = (tw - kw) * 0.5f;
            thumb->posY = v * range;
        }
    }
}


Texture* UIDocument::LoadUITexture(const char* path) {
    if (!path || !*path) return nullptr;
    std::string key = path;
    auto it = textureCache_.find(key);
    if (it != textureCache_.end() && it->second && it->second->Valid())
        return it->second.get();
    auto tex = std::unique_ptr<Texture>(new Texture());
    if (!tex->LoadPNGAsset(path)) {
        if (std::strncmp(path, "Screen/", 7) == 0)
            tex->LoadPNGAsset(path + 7);
    }
    if (!tex->Valid()) return nullptr;
    Texture* raw = tex.get();
    textureCache_[key] = std::move(tex);
    return raw;
}

// Binary UIElement::UpdateDragAction @ 0x107570
// Drag target may be the slider itself OR its Draggable thumb child.
// Value stored at slider+0x1e0 (currentValue); thumb pos follows via LayoutSliders.
void UIDocument::UpdateDragAction(UIElement* e, float touchX, float touchY) {
    if (!e || !e->isDragging) return;

    UIElement* slider = nullptr;
    UIElement* thumb = nullptr;
    if (e->type == UIElement::Type::Slider) {
        slider = e;
        if (!e->children.empty()) thumb = e->children[0];
    } else if (e->parent && e->parent->type == UIElement::Type::Slider) {
        slider = e->parent;
        thumb = e;
    }

    if (slider) {
        if (!thumb && !slider->children.empty())
            thumb = slider->children[0];
        float thumbW = thumb ? thumb->screenRect.w : 0.f;
        float thumbH = thumb ? thumb->screenRect.h : 0.f;
        // Prefer design size if screenRect not yet laid out
        if (thumb && thumbW < 1.f) thumbW = thumb->sizeW;
        if (thumb && thumbH < 1.f) thumbH = thumb->sizeH;
        float v = slider->currentValue;
        if (slider->horizontal) {
            float range = slider->screenRect.w - thumbW;
            if (range < 1.f) range = 1.f;
            v = (touchX - slider->screenRect.x - thumbW * 0.5f) / range;
        } else {
            float range = slider->screenRect.h - thumbH;
            if (range < 1.f) range = 1.f;
            // Binary vertical: 0 at TOP, 1 at BOTTOM (matches LayoutSliders).
            v = (touchY - slider->screenRect.y - thumbH * 0.5f) / range;
        }
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        slider->currentValue = v;
        LayoutSliders();
        return;
    }

    UIElement* parent = e->parent;
    if (parent) {
        // Free drag clamped to parent (binary AlignRect + delta clamp)
        float w = e->screenRect.w, h = e->screenRect.h;
        float nx = touchX - w * 0.5f;
        float ny = touchY - h * 0.5f;
        float minX = parent->screenRect.x;
        float minY = parent->screenRect.y;
        float maxX = parent->screenRect.x + parent->screenRect.w - w;
        float maxY = parent->screenRect.y + parent->screenRect.h - h;
        if (nx < minX) nx = minX;
        if (ny < minY) ny = minY;
        if (nx > maxX) nx = maxX;
        if (ny > maxY) ny = maxY;
        e->screenRect.x = nx;
        e->screenRect.y = ny;
    }
}


void UIDocument::Draw(float screenW, float screenH, FontResource& fonts) const {
#if !defined(__ANDROID__)
    (void)screenW; (void)screenH; (void)fonts;
    return;
#else
    // Binary UISceneObject::RenderUI → UIElement::DrawH from roots
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
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // 0x302 / 0x303

    // Hierarchical DrawH from roots (binary @ 0x107080)
    for (UIElement* root : roots_) {
        if (!root) continue;
        DrawH(root, 0.f, 0.f, screenW, screenH, 0.f, 0.f, screenW, screenH, fonts);
    }
    // Also draw orphan elements not linked into roots (safety)
    for (const auto& e : elements_) {
        if (e.parent || !e.visible) continue;
        bool inRoots = false;
        for (UIElement* r : roots_) if (r == &e) { inRoots = true; break; }
        if (!inRoots)
            DrawH(const_cast<UIElement*>(&e), 0.f, 0.f, screenW, screenH,
                  0.f, 0.f, screenW, screenH, fonts);
    }

    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glColor4f(1, 1, 1, 1);
#endif
}

// Binary UIElement::DrawH @ 0x107080:
//   AlignRect within parent → store pos @ +0x1c0/+0x1c4
//   CheckRect vs clip → if visible Draw (vtable)
//   for each child DrawH(child, this rect, clip)
void UIDocument::DrawH(UIElement* e, float parentX, float parentY, float parentW, float parentH,
                       float clipX, float clipY, float clipW, float clipH,
                       FontResource& fonts) const {
#if !defined(__ANDROID__)
    (void)e; (void)parentX; (void)parentY; (void)parentW; (void)parentH;
    (void)clipX; (void)clipY; (void)clipW; (void)clipH; (void)fonts;
    return;
#else
    if (!e || !e->visible) return;
    (void)parentX; (void)parentY; (void)parentW; (void)parentH;

    // screenRect already resolved by ResolveAll (AlignRect baked in)
    float x = e->screenRect.x, y = e->screenRect.y, w = e->screenRect.w, h = e->screenRect.h;
    if (w < 1.f || h < 1.f) {
        // still recurse children (layout containers)
        for (UIElement* c : e->children)
            DrawH(c, x, y, w, h, clipX, clipY, clipW, clipH, fonts);
        return;
    }

    // CheckRect: intersect element with clip
    float ix = x > clipX ? x : clipX;
    float iy = y > clipY ? y : clipY;
    float ix2 = (x + w) < (clipX + clipW) ? (x + w) : (clipX + clipW);
    float iy2 = (y + h) < (clipY + clipH) ? (y + h) : (clipY + clipH);
    float iw = ix2 - ix, ih = iy2 - iy;
    if (iw > 0.f && ih > 0.f) {
        // Scissor clip (binary FRect clip on mesh path)
        // GL scissor origin is bottom-left; our ortho is top-left → convert
        // For simplicity apply scissor in window Y-down by flipping using parent screenH from clip root
        DrawElement(*e, ix, iy, iw, ih, fonts);
    }

    // Children inherit this element's rect as parent; clip stays intersected
    float childClipX = (iw > 0.f) ? ix : clipX;
    float childClipY = (ih > 0.f) ? iy : clipY;
    float childClipW = (iw > 0.f) ? iw : clipW;
    float childClipH = (ih > 0.f) ? ih : clipH;
    for (UIElement* c : e->children)
        DrawH(c, x, y, w, h, childClipX, childClipY, childClipW, childClipH, fonts);
#endif
}

void UIDocument::DrawElement(const UIElement& e, float clipX, float clipY, float clipW, float clipH,
                             FontResource& fonts) const {
#if !defined(__ANDROID__)
    (void)e; (void)clipX; (void)clipY; (void)clipW; (void)clipH; (void)fonts;
    return;
#else
    (void)clipX; (void)clipY; (void)clipW; (void)clipH;

    // Binary: frame_background is a full-screen UIElement with
    //   BackgroundColor = $backgroundColor (menu.txt: B4000000 → A=0xB4 black).
    // That is the menu dim overlay. Previously skipped as "layout-only" — wrong.
    // "frame" is a transparent layout parent (no BackgroundColor in menu.txt) —
    // skip only the empty layout container, still draw children via DrawH.
    if (e.name == "frame") return;
    if (e.name == "menu_mtl_sets") return;

    auto splitARGB = [](uint32_t c, float& r, float& g, float& b, float& a) {
        a = ((c >> 24) & 0xff) / 255.f;
        r = ((c >> 16) & 0xff) / 255.f;
        g = ((c >>  8) & 0xff) / 255.f;
        b = ( c        & 0xff) / 255.f;
    };
    auto argbToRgbaPack = [](uint32_t argb) -> uint32_t {
        uint32_t a = (argb >> 24) & 0xff;
        uint32_t r = (argb >> 16) & 0xff;
        uint32_t g = (argb >>  8) & 0xff;
        uint32_t b =  argb        & 0xff;
        return r | (g << 8) | (b << 16) | (a << 24);
    };
    auto drawQuad = [&](float x, float y, float w, float h, uint32_t argb) {
        if (w <= 0.f || h <= 0.f) return;
        if (((argb >> 24) & 0xff) == 0) return;
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

    const UIStyle& st = e.CurrentStyle();
    float x = e.screenRect.x, y = e.screenRect.y, w = e.screenRect.w, h = e.screenRect.h;

    if (e.name.find("crosshair") != std::string::npos) {
        drawQuad(x, y, w, h, 0xFFFFFFFFu);
        return;
    }

    // ------------------------------------------------------------------
    // UISliderElement::DrawBackground @ 0x105350
    // if children==0 OR LineWidth(+0x1e8)==0 → fall through to UIElement::DrawBackground
    // else: do NOT call base DrawBackground; draw track only:
    //   color = style BackgroundColor (pfVar11[6]); if alpha==0 → return
    //   parentInset = sum of Frame[i].Width while Frame[i].Color alpha==0
    //   thumbInset  = same for child0 style
    //   thickness   = LineWidth
    //   horizontal: Y = center - LineWidth/2; X segments left/right of thumb
    // ------------------------------------------------------------------
    const bool sliderTrackPath = (e.type == UIElement::Type::Slider
                                  && e.lineWidth > 0.f
                                  && !e.children.empty());
    if (sliderTrackPath) {
        uint32_t lineCol = st.backgroundColor;
        if (((lineCol >> 24) & 0xff) == 0)
            return; // binary: no bg alpha → nothing drawn

        auto transparentFrameInset = [](const UIStyle& s) -> float {
            float sum = 0.f;
            const float ws[3] = { s.frame0Width, s.frame1Width, s.frame2Width };
            const uint32_t cs[3] = { s.frame0Color, s.frame1Color, s.frame2Color };
            for (int i = 0; i < 3; ++i) {
                if (((cs[i] >> 24) & 0xff) != 0) break;
                sum += ws[i];
            }
            return sum;
        };
        const UIStyle& thumbSt = e.children[0]->CurrentStyle();
        float parentInset = transparentFrameInset(st);
        float thumbInset = transparentFrameInset(thumbSt);
        float lw = e.lineWidth;
        UIElement* thumb = e.children[0];
        float tx = thumb->screenRect.x;
        float ty = thumb->screenRect.y;
        float tw = thumb->screenRect.w > 0.f ? thumb->screenRect.w : thumb->sizeW;
        float th = thumb->screenRect.h > 0.f ? thumb->screenRect.h : thumb->sizeH;

        if (e.horizontal) {
            // Binary: Y = elem.y + H*0.5 - LineWidth*0.5
            float ly = y + h * 0.5f - lw * 0.5f;
            float left = x + parentInset;
            float right = x + w - parentInset;
            float thumbL = tx + thumbInset;
            float thumbR = tx + tw - thumbInset;
            if (left < thumbL)
                drawQuad(left, ly, thumbL - left, lw, lineCol);
            if (thumbR < right)
                drawQuad(thumbR, ly, right - thumbR, lw, lineCol);
        } else {
            float lx = x + w * 0.5f - lw * 0.5f;
            float top = y + parentInset;
            float bot = y + h - parentInset;
            float thumbT = ty + thumbInset;
            float thumbB = ty + th - thumbInset;
            if (top < thumbT)
                drawQuad(lx, top, lw, thumbT - top, lineCol);
            if (thumbB < bot)
                drawQuad(lx, thumbB, lw, bot - thumbB, lineCol);
        }
        return; // track path replaces base DrawBackground entirely
    }

    // ------------------------------------------------------------------
    // UIElement::DrawBackground @ 0x103b60
    // Early-out: bg alpha==0 AND all frame widths 0 → return.
    // ------------------------------------------------------------------
    {
        float widths[3] = { st.frame0Width, st.frame1Width, st.frame2Width };
        uint32_t colors[3] = { st.frame0Color, st.frame1Color, st.frame2Color };
        const bool bgVisible = ((st.backgroundColor >> 24) & 0xff) != 0;
        const bool anyFrameW = (widths[0] != 0.f || widths[1] != 0.f || widths[2] != 0.f);
        if (bgVisible || anyFrameW) {
            float fx = x, fy = y, fw = w, fh = h;
            for (int i = 0; i < 3; ++i) {
                float bw = widths[i];
                if (bw == 0.f) continue;
                float ox = fx, oy = fy, ow = fw, oh = fh;
                fx += bw; fy += bw;
                fw -= bw * 2.f; fh -= bw * 2.f;
                if (fw < 0.f) fw = 0.f;
                if (fh < 0.f) fh = 0.f;
                uint32_t col = colors[i];
                if (((col >> 24) & 0xff) == 0) continue;
                if (ow > 0.f && oh > 0.f) {
                    drawQuad(ox, oy, ow, bw, col);
                    drawQuad(ox, oy + oh - bw, ow, bw, col);
                    float midH = oh - 2.f * bw;
                    if (midH > 0.f) {
                        drawQuad(ox, oy + bw, bw, midH, col);
                        drawQuad(ox + ow - bw, oy + bw, bw, midH, col);
                    }
                }
            }
            if (bgVisible && fw > 0.f && fh > 0.f)
                drawQuad(fx, fy, fw, fh, st.backgroundColor);
        }
    }

    // ------------------------------------------------------------------
    // UIGridModelMtl::DrawBackground @ 0x108a10
    // base BG done; RenderGridModelMtlArray into element rect
    // ------------------------------------------------------------------
    // UIGridModelMtl::DrawBackground — RenderGridModelMtlArray into this rect.
    // materials[] are local slot indices 0..8; absolute S-Index = page*9 + slot.
    // menu_mtl_N are HitTest-only overlays (HitTest targets), not a second draw path.
    if (e.type == UIElement::Type::GridModelMtl && gridForMtl_ && blocksAtlas_
        && blocksAtlas_->GLId() && !e.materials.empty()) {
        int aw = e.arrayWidth > 0 ? e.arrayWidth : 3;
        int ah = e.arrayHeight > 0 ? e.arrayHeight : 3;
        int n = (int)e.materials.size();
        if (n < aw * ah) n = aw * ah;
        float cellW = w / (float)aw;
        float cellH = h / (float)ah;
        blocksAtlas_->Bind();
        glEnable(GL_TEXTURE_2D);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glColor4f(1, 1, 1, 1);
        // GridData::GetBlockIcon @ 0x8e3a0 — MeshBuffer + PushQuads.
        // Solid branch (BMT==4 || BMT<2 || BMT==3 || BMT==7):
        //   height = mat[+0x00]; if height != 0, vertex Y shorts that were non-zero
        //   become (height << 3) in 0..128 mesh space, and UV V1 = tileV*-0x70 - height.
        // Full BH (0x10) → Y=0x80; slab BH=8 → Y=0x40; snow BH=1 → Y=8.
        auto drawTexQuadUV = [&](const GLfloat* verts, float u0, float v0, float u1, float v1) {
            const GLfloat uvs[] = { u0,v0, u1,v0, u1,v1, u0,v1 };
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glVertexPointer(2, GL_FLOAT, 0, verts);
            glTexCoordPointer(2, GL_FLOAT, 0, uvs);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
        };
        for (int i = 0; i < n; ++i) {
            int mx = i % aw, my = i / aw;
            int localSlot = e.materials[(size_t)i];
            int sIndex = palettePage_ * 9 + localSlot;
            unsigned char cell = gridForMtl_->MtlSelIndexToCell(sIndex);
            if (cell == 0) continue;
            const Material* mat = gridForMtl_->GetMaterial((int)cell);
            if (!mat) continue;
            float pad = cellW * 0.10f;
            float x0 = x + (float)mx * cellW + pad;
            float y0 = y + (float)my * cellH + pad;
            float x1 = x + (float)(mx + 1) * cellW - pad;
            float y1 = y + (float)(my + 1) * cellH - pad;
            x0 -= 1.f; y0 -= 1.f; x1 += 1.f; y1 += 1.f;
            float cw = x1 - x0, ch = y1 - y0;
            float cx = (x0 + x1) * 0.5f;
            int bmt = (int)mat->prop01();
            // UV tile for solid-branch icons: mat[+0x0e]/[+0x14] (ZP U/V), same as binary.
            int tu = (int)(signed char)mat->raw[0x0e];
            int tv = (int)(signed char)mat->raw[0x14];
            if (tu < 0) tu = 0;
            if (tv < 0) tv = 0;
            float inset = cellW * 0.06f;
            float xx0 = x0 + inset, xx1 = x1 - inset;
            float yy0 = y0 + inset, yy1 = y1 - inset;

            // height units after LoadMaterials (1..16). Binary: if (*mat != 0) scale mesh.
            int ht = (int)mat->height();
            const bool solidBranch = (bmt == 4 || bmt < 2 || bmt == 3 || bmt == 7);

            if (bmt == 2) {
                tu = (int)mat->uFace(0);
                tv = (int)mat->vFace(0);
                xx0 = cx - cellW * 0.12f;
                xx1 = cx + cellW * 0.12f;
                yy0 = y0 + ch * 0.10f;
                yy1 = y1 - ch * 0.06f;
                float u0, v0, u1, v1;
                Texture::TileUV(tu, tv, u0, v0, u1, v1);
                const GLfloat verts[] = { xx0, yy0, xx1, yy0, xx1, yy1, xx0, yy1 };
                drawTexQuadUV(verts, u0, v0, u1, v1);
                continue;
            }

            if (solidBranch) {
                // Isometric 3-face icon in cell, mesh space analogous to 0..128 with
                // Y extent = (ht << 3) when ht != 0 (full 0x10 → 0x80).
                if (ht <= 0) ht = 16; // height 0 uses unscaled template; treat as full for UI solid
                float hFrac = (float)ht / 16.f;
                if (hFrac > 1.f) hFrac = 1.f;
                // Bottom-align partial blocks in the cell (world BH sits on floor).
                float bodyH = (yy1 - yy0) * hFrac;
                float base = yy1;
                float top = yy1 - bodyH;
                float midY = (top + base) * 0.5f;
                float halfW = (xx1 - xx0) * 0.5f;
                // Diamond top + two side faces (classic inventory isometric).
                float u0, v0, u1, v1;
                // Top face uses YP tiles when available; sides use XP/ZP.
                int tuTop = (int)mat->uFace(1), tvTop = (int)mat->vFace(1);
                int tuR = (int)mat->uFace(0), tvR = (int)mat->vFace(0);
                int tuL = (int)mat->uFace(2), tvL = (int)mat->vFace(2);
                Texture::TileUV(tuTop, tvTop, u0, v0, u1, v1);
                // Top diamond (full tile — top of slab is full footprint).
                {
                    float tc = top;
                    float peak = top - (yy1 - yy0) * 0.18f * hFrac; // slight lift for iso peak
                    // flatter top for readability
                    peak = top - (base - top) * 0.35f;
                    if (peak > top) peak = top;
                    // Use parallelogram top: left, top, right, bottom of diamond at `top` plane
                    float tMid = top;
                    float drop = (base - top) * 0.22f;
                    GLfloat verts[] = {
                        cx,           tMid - drop,           // top
                        xx1,          tMid + (base - top) * 0.15f, // right
                        cx,           tMid + (base - top) * 0.30f, // bottom
                        xx0,          tMid + (base - top) * 0.15f  // left
                    };
                    // reorder as fan from top-left style
                    GLfloat vfan[] = {
                        verts[6], verts[7],
                        verts[0], verts[1],
                        verts[2], verts[3],
                        verts[4], verts[5]
                    };
                    drawTexQuadUV(vfan, u0, v0, u1, v1);
                }
                // Right face (XP): UV V cropped to bottom hFrac of tile (binary V1 -= height units).
                Texture::TileUV(tuR, tvR, u0, v0, u1, v1);
                {
                    float sv0 = v1 + (v0 - v1) * hFrac; // keep bottom of tile, shrink toward top
                    float rTop = top + (base - top) * 0.15f;
                    float rBot = base;
                    float rPeak = top + (base - top) * 0.30f;
                    GLfloat verts[] = {
                        cx,  rPeak,
                        xx1, rTop,
                        xx1, rBot,
                        cx,  rBot
                    };
                    drawTexQuadUV(verts, u0, sv0, u1, v1);
                }
                // Left face (ZP)
                Texture::TileUV(tuL, tvL, u0, v0, u1, v1);
                {
                    float sv0 = v1 + (v0 - v1) * hFrac;
                    float lTop = top + (base - top) * 0.15f;
                    float lBot = base;
                    float lPeak = top + (base - top) * 0.30f;
                    GLfloat verts[] = {
                        xx0, lTop,
                        cx,  lPeak,
                        cx,  lBot,
                        xx0, lBot
                    };
                    drawTexQuadUV(verts, u0, sv0, u1, v1);
                }
                continue;
            }

            // BMT 5/6/8/9/10: face0 tile until specialized icon templates ported.
            tu = (int)mat->uFace(0);
            tv = (int)mat->vFace(0);
            {
                float u0, v0, u1, v1;
                Texture::TileUV(tu, tv, u0, v0, u1, v1);
                if (ht > 0 && ht < 16) {
                    float hFrac = (float)ht / 16.f;
                    float bodyH = (yy1 - yy0) * hFrac;
                    yy0 = yy1 - bodyH;
                    v0 = v1 + (v0 - v1) * hFrac;
                }
                const GLfloat verts[] = { xx0, yy0, xx1, yy0, xx1, yy1, xx0, yy1 };
                drawTexQuadUV(verts, u0, v0, u1, v1);
            }
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    // ------------------------------------------------------------------
    // UIElement::DrawImage @ 0x106310 / TextureView::DrawTexture
    // SpriteButton also may use image; texture from style or textureName
    // ------------------------------------------------------------------
    {
        Texture* tex = st.texture;
        if (!tex && !e.textureName.empty())
            tex = const_cast<UIDocument*>(this)->LoadUITexture(e.textureName.c_str());
        if (tex && tex->Valid() && tex->GLId()) {
            float iw = (float)tex->Width();
            float ih = (float)tex->Height();
            // AlignRect for image: imageAlign (+0x6c/+0x70) default 0.5
            float tx = x + std::floor((w - iw) * e.imageAlignX);
            float ty = y + std::floor((h - ih) * e.imageAlignY);
            if (e.type == UIElement::Type::TextureView || e.type == UIElement::Type::SpriteButton) {
                tx = x; ty = y; iw = w; ih = h;
            }
            tex->Bind();
            glEnable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(1, 1, 1, 1);
            const GLfloat verts[] = { tx, ty, tx+iw, ty, tx+iw, ty+ih, tx, ty+ih };
            const GLfloat uvs[] = { 0,0, 1,0, 1,1, 0,1 };
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glVertexPointer(2, GL_FLOAT, 0, verts);
            glTexCoordPointer(2, GL_FLOAT, 0, uvs);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
        }
    }

    // ------------------------------------------------------------------
    // UIElement::DrawText @ 0x106be0 (+ SpriteButton::DrawText path)
    // ------------------------------------------------------------------
    Font* textFont = fonts.FindOrLoad(e.fontName.empty() ? "Sans" : e.fontName.c_str(),
                                      metricsFontSize_ > 0 ? metricsFontSize_ : 16);
    std::string label = e.DisplayText();
    if (!label.empty() && textFont && textFont->IsLoaded()) {
        std::vector<std::string> lines;
        {
            std::string cur;
            for (char ch : label) {
                if (ch == '\n') { lines.push_back(cur); cur.clear(); }
                else if (ch != '\r') cur.push_back(ch);
            }
            lines.push_back(cur);
        }
        float scale = 1.f;
        float maxLineW = 0.f;
        for (const auto& ln : lines)
            maxLineW = std::max(maxLineW, textFont->MeasureWidth(ln.c_str(), 1.f));
        float lineH = textFont->LineHeight();
        float blockH = lineH * (float)lines.size();
        // Binary draws at metrics font size; only shrink when text truly overflows the
        // element (leave more headroom than before — 16px on 40px tabs looked tiny).
        if (maxLineW > 1.f && maxLineW * scale > w * 0.98f) scale = (w * 0.98f) / maxLineW;
        if (blockH * scale > h * 0.95f && blockH > 0.f) scale = (h * 0.95f) / blockH;
        if (scale < 0.55f) scale = 0.55f;
        float textW = maxLineW * scale;
        float textH = blockH * scale;
        // AlignRect @ 0x106250 — uses TEXT align (+0x80/+0x84), NOT layout XAlign:
        //   pos = elemOrigin + textPos + floor((elemSize - textSize) * textAlign)
        // Defaults from UIElement ctor: textAlign 0.5/0.5, textPos 0/0
        // AlignRect: pos = origin + textPos + floor((size - textSize) * align)
        float tx0 = x + e.textPosX + std::floor((w - textW) * e.textAlignX);
        float ty0 = y + e.textPosY + std::floor((h - textH) * e.textAlignY);
        // Keep glyphs inside element (page tabs are short 40px cells)
        if (ty0 < y) ty0 = y;
        if (ty0 + textH > y + h) ty0 = y + h - textH;
        if (tx0 < x) tx0 = x;

        MeshBuffer textMesh;
        uint32_t shadowArgb = st.textShadowColor;
        uint32_t col = argbToRgbaPack(st.textColor);
        if (((st.textColor >> 24) & 0xff) == 0) col = 0;
        else if (((st.textColor >> 24) & 0xff) < 0x40) col = argbToRgbaPack(0xFFDCDCDCu);
        for (size_t li = 0; li < lines.size(); ++li) {
            const std::string& ln = lines[li];
            if (ln.empty()) continue;
            float tw = textFont->MeasureWidth(ln.c_str(), 1.f) * scale;
            // Per-line X: AlignRect with this line width (binary measures whole string once;
            // multi-line labels use block align; each line inherits textAlignX)
            float tx = x + e.textPosX + std::floor((w - tw) * e.textAlignX);
            float ty = ty0 + (float)li * lineH * scale;
            if ((shadowArgb & 0xff000000) != 0) {
                textFont->AddTextToMesh(textMesh, Vector(tx + 1.f, ty + 1.f, 0), ln.c_str(),
                                        scale, argbToRgbaPack(shadowArgb), TextAlign::Left);
            }
            if (col != 0) {
                textFont->AddTextToMesh(textMesh, Vector(tx, ty, 0), ln.c_str(),
                                        scale, col, TextAlign::Left);
            }
        }
        if (textMesh.IndexCount() > 0) {
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
    }
#endif
}


