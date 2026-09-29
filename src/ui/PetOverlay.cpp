#include "ui/PetOverlay.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_freetype.h>
#if defined(__APPLE__) || defined(KUJIRA_OVERLAY_GL2)
// The pet window has a legacy OpenGL 2.1 context on macOS (the Cubism OpenGL
// renderer needs GLSL 1.20), which the OpenGL 3 backend does not support there.
#include <imgui_impl_opengl2.h>
#define OverlayBackendInit() ImGui_ImplOpenGL2_Init()
#define OverlayBackendShutdown() ImGui_ImplOpenGL2_Shutdown()
#define OverlayBackendNewFrame() ImGui_ImplOpenGL2_NewFrame()
#define OverlayBackendRender(data) ImGui_ImplOpenGL2_RenderDrawData(data)
#else
#include <imgui_impl_opengl3.h>
#define OverlayBackendInit() ImGui_ImplOpenGL3_Init(nullptr)
#define OverlayBackendShutdown() ImGui_ImplOpenGL3_Shutdown()
#define OverlayBackendNewFrame() ImGui_ImplOpenGL3_NewFrame()
#define OverlayBackendRender(data) ImGui_ImplOpenGL3_RenderDrawData(data)
#endif

#include "ui/Theme.h"

namespace
{
constexpr float kFontSize = 15.0f;        // logical pixels
constexpr double kMinStatusSeconds = 1.2;  // agent status lines stay at least this long

ImU32 Color(unsigned rgb, float alpha)
{
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255));
}

float Approach(float value, float target, float step)
{
    return value < target ? std::min(target, value + step) : std::max(target, value - step);
}

// Longest prefix of `text` (on a UTF-8 boundary) that fits in maxHeight when wrapped, plus an ellipsis.
std::string FitText(ImFont* font, float size, const std::string& text, float wrap, float maxHeight)
{
    auto height = [&](const std::string& s) { return font->CalcTextSizeA(size, FLT_MAX, wrap, s.c_str(), s.c_str() + s.size()).y; };
    if (height(text) <= maxHeight) return text;
    size_t lo = 0, hi = text.size();
    while (lo < hi)
    {
        size_t mid = (lo + hi + 1) / 2;
        while (mid > 0 && (static_cast<unsigned char>(text[mid]) & 0xC0) == 0x80) --mid;
        if (mid <= lo)
        {
            break;
        }
        if (height(text.substr(0, mid) + "…") <= maxHeight) lo = mid;
        else hi = mid - 1;
    }
    return text.substr(0, lo) + "…";
}
}

PetOverlay::~PetOverlay()
{
    Shutdown();
}

bool PetOverlay::Init(GLFWwindow* window)
{
    _window = window;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    _imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(_imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // Same mapped system font as the settings window; glyphs are baked on demand.
    for (const auto& [path, index] : Platform::CjkFontCandidates())
    {
        std::error_code ec;
        if (!fs::exists(path, ec)) continue;
        _fontFile = Platform::MapFile(path);
        if (!_fontFile.data) continue;
        ImFontConfig cfg;
        cfg.FontNo = index;
        cfg.FontDataOwnedByAtlas = false;
        // FreeType with light hinting: snaps strokes vertically, crisper small text at 100% scale.
        cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        if (io.Fonts->AddFontFromMemoryTTF(_fontFile.data, static_cast<int>(_fontFile.size), kFontSize, &cfg)) break;
        Platform::UnmapFile(_fontFile);
    }
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();

    const bool ok = OverlayBackendInit();
    ImGui::SetCurrentContext(previous);
    if (!ok) Shutdown();
    return ok;
}

void PetOverlay::Shutdown()
{
    if (!_imgui) return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* self = _imgui;
    ImGui::SetCurrentContext(self);
    OverlayBackendShutdown();
    ImGui::DestroyContext(self);
    _imgui = nullptr;
    ImGui::SetCurrentContext(previous == self ? nullptr : previous);
    Platform::UnmapFile(_fontFile);
}

void PetOverlay::Say(const std::string& text, float seconds, Priority priority, double now)
{
    if (text.empty()) return;
    const bool visible = BubbleVisible(now);
    if (visible && priority < _priority) return;
    // Agent status changes quickly (one line per tool call); don't flash each one.
    if (visible && priority == Priority::Status && _priority == Priority::Status && now - _shownAt < kMinStatusSeconds)
    {
        _pendingText = text;
        _pendingSeconds = seconds;
        return;
    }
    _pendingText.clear();
    _text = text;
    _priority = priority;
    _shownAt = now;
    _sticky = seconds <= 0;
    _until = now + seconds;
}

void PetOverlay::ClearSticky()
{
    if (_sticky)
    {
        _sticky = false;
        _until = 0;
    }
}

void PetOverlay::Dismiss()
{
    _sticky = false;
    _until = 0;
    _pendingText.clear();
}

bool PetOverlay::BubbleVisible(double now) const
{
    return !_text.empty() && (_sticky || now < _until);
}

PetOverlay::Rect PetOverlay::ButtonRect(const Layout& layout, int index) const
{
    const float s = layout.scale;
    const float d = 28 * s;
    Rect r;
    r.x = layout.model.x + layout.model.w + 4 * s;
    r.y = layout.model.y + layout.model.h * 0.30f + index * (d + 8 * s);
    r.w = d;
    r.h = d;
    return r;
}

PetOverlay::Button PetOverlay::ButtonAt(double x, double y) const
{
    if (_toolbarAlpha < 0.3f) return Button::None;
    for (int i = 0; i < static_cast<int>(Button::Count); ++i)
    {
        if (_buttonRects[i].Contains(x, y)) return static_cast<Button>(i);
    }
    return Button::None;
}

void PetOverlay::Render(const Layout& layout, double now, bool showToolbar, bool quiet, double cursorX, double cursorY)
{
    if (!_imgui) return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(_imgui);

    int ww, wh, fbw, fbh;
    glfwGetWindowSize(_window, &ww, &wh);
    glfwGetFramebufferSize(_window, &fbw, &fbh);
    if (ww <= 0 || wh <= 0)
    {
        ImGui::SetCurrentContext(previous);
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(ww), static_cast<float>(wh));
    io.DisplayFramebufferScale = ImVec2(static_cast<float>(fbw) / ww, static_cast<float>(fbh) / wh);
    const float dt = static_cast<float>(std::clamp(now - _lastRender, 1e-4, 0.1));
    io.DeltaTime = dt;
    _lastRender = now;

    if (!_pendingText.empty() && now - _shownAt >= kMinStatusSeconds)
    {
        std::string text = std::move(_pendingText);
        _pendingText.clear();
        Say(text, _pendingSeconds, Priority::Status, now);
    }

    const bool bubbleOn = BubbleVisible(now);
    const float bubbleTarget = bubbleOn ? 1.0f : 0.0f;
    _bubbleAlpha = Approach(_bubbleAlpha, bubbleTarget, dt / (bubbleOn ? 0.18f : 0.35f));
    if (!bubbleOn && _bubbleAlpha <= 0) _text.clear();
    const float toolbarTarget = (showToolbar && layout.toolbar) ? 1.0f : 0.0f;
    _toolbarAlpha = Approach(_toolbarAlpha, toolbarTarget, dt / 0.2f);
    _animating = _bubbleAlpha != bubbleTarget || _toolbarAlpha != toolbarTarget;

    OverlayBackendNewFrame();
    ImGui::NewFrame();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
#if defined(__APPLE__) || defined(KUJIRA_OVERLAY_GL2)
    // The OpenGL 2 backend blends alpha like colour (alpha ends up squared). In a
    // transparent window the compositor reads alpha as coverage, so soft edges and
    // shadows came out wrong and the bubble looked blurry. Blend alpha the way the
    // OpenGL 3 backend does; the backend restores the previous blend state afterwards.
    dl->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }, nullptr);
#endif
    ImFont* font = io.Fonts->Fonts[0];
    const float s = layout.scale;
    const float fontSize = kFontSize * s;

    // Speech bubble: above her head, tail pointing down.
    _bubbleRect = {};
    if (_bubbleAlpha > 0.01f && !_text.empty())
    {
        const float a = _bubbleAlpha;
        const float pad = 10 * s;
        const float tail = 8 * s;
        const float tip = layout.model.y + layout.model.h * 0.06f;
        const float maxTextH = std::max(fontSize, tip - tail - layout.bubbleTop - 2 * pad);
        const float wrap = std::max(60 * s, layout.bubbleMaxW - 2 * pad);
        const std::string shown = FitText(font, fontSize, _text, wrap, maxTextH);
        const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, wrap, shown.c_str(), shown.c_str() + shown.size());
        const float bw = std::ceil(size.x) + 2 * pad;
        const float bh = std::ceil(size.y) + 2 * pad;
        const float cx = layout.model.x + layout.model.w * 0.5f;
        float x = std::clamp(cx - bw * 0.5f, 4 * s, std::max(4 * s, ww - 4 * s - bw));
        const float y = tip - tail - bh;
        const float rounding = 10 * s;
        const unsigned border = _priority == Priority::Attention ? Theme::kWarn : Theme::kAccent;

        dl->AddRectFilled(ImVec2(x + 1 * s, y + 2 * s), ImVec2(x + bw + 1 * s, y + bh + 2 * s), Color(0x28406E, 0.12f * a), rounding);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + bh), Color(0xFAFCFF, 0.92f * a), rounding);
        const float tx = std::clamp(cx, x + rounding + 6 * s, x + bw - rounding - 6 * s);
        const ImVec2 t0(tx - 7 * s, y + bh - 0.5f), t1(tx + 7 * s, y + bh - 0.5f), t2(tx, tip);
        dl->AddTriangleFilled(t0, t1, t2, Color(0xFAFCFF, 0.92f * a));
        dl->AddRect(ImVec2(x, y), ImVec2(x + bw, y + bh), Color(border, 0.45f * a), rounding, 0, 1.2f * s);
        dl->AddLine(t0, t2, Color(border, 0.45f * a), 1.2f * s);
        dl->AddLine(t1, t2, Color(border, 0.45f * a), 1.2f * s);
        dl->AddLine(ImVec2(t0.x + 1.2f * s, t0.y), ImVec2(t1.x - 1.2f * s, t1.y), Color(0xFAFCFF, 0.92f * a), 2.0f * s);
        dl->AddText(font, fontSize, ImVec2(x + pad, y + pad), Color(0x2D3A5C, a), shown.c_str(), shown.c_str() + shown.size(), wrap);
        _bubbleRect = { x, y, bw, bh };
    }

    // Toolbar: settings, quiet mode, quit.
    for (auto& r : _buttonRects) r = {};
    if (_toolbarAlpha > 0.01f)
    {
        const float a = _toolbarAlpha;
        static const char* kLabels[] = { "设置", "安静模式", "退出" };
        for (int i = 0; i < static_cast<int>(Button::Count); ++i)
        {
            const Rect r = ButtonRect(layout, i);
            _buttonRects[i] = r;
            const bool hovered = r.Contains(cursorX, cursorY);
            // Snap the centre to a pixel so the icons anti-alias evenly on every side.
            const ImVec2 c(std::round(r.x + r.w * 0.5f), std::round(r.y + r.h * 0.5f));
            const float radius = r.w * 0.5f;
            const unsigned accent = i == static_cast<int>(Button::Quit) ? Theme::kError : Theme::kAccent;
            dl->AddCircleFilled(c, radius, hovered ? Color(accent, 0.95f * a) : Color(0xFFFFFF, 0.88f * a), 32);
            dl->AddCircle(c, radius, Color(accent, 0.35f * a), 32, 1.0f * s);
            const ImU32 ink = hovered ? Color(0xFFFFFF, a) : Color(accent, a);
            const float t = 1.8f * s;
            // AddLine() nudges endpoints by half a pixel; stroke paths directly
            // so every stroke shares the same centre as the circles.
            const auto line = [&](ImVec2 p1, ImVec2 p2, ImU32 col, float th) {
                dl->PathLineTo(p1);
                dl->PathLineTo(p2);
                dl->PathStroke(col, 0, th);
            };
            switch (static_cast<Button>(i))
            {
            case Button::Settings:
            {
                // Solid gear: a thick ring plus 8 identical filled teeth, all
                // centred on c so it stays symmetric at any scale.
                dl->AddCircle(c, 4.1f * s, ink, 32, 3.0f * s);
                const float kPi = 3.14159265f;
                const float r0 = 5.0f * s, r1 = 7.8f * s, base = 1.5f * s, top = 1.1f * s;
                for (int k = 0; k < 8; ++k)
                {
                    const float ang = k * kPi / 4;
                    const ImVec2 d(std::cos(ang), std::sin(ang)), n(-d.y, d.x);
                    const ImVec2 tooth[4] = {
                        ImVec2(c.x + d.x * r0 - n.x * base, c.y + d.y * r0 - n.y * base),
                        ImVec2(c.x + d.x * r1 - n.x * top,  c.y + d.y * r1 - n.y * top),
                        ImVec2(c.x + d.x * r1 + n.x * top,  c.y + d.y * r1 + n.y * top),
                        ImVec2(c.x + d.x * r0 + n.x * base, c.y + d.y * r0 + n.y * base),
                    };
                    dl->AddConvexPolyFilled(tooth, 4, ink);
                }
                break;
            }
            case Button::Quiet:
            {
                const float hw = 7 * s, hh = 5 * s;
                dl->AddRect(ImVec2(c.x - hw, c.y - hh - 1 * s), ImVec2(c.x + hw, c.y + hh - 1 * s), ink, 3 * s, 0, t);
                dl->AddTriangleFilled(ImVec2(c.x - 4 * s, c.y + hh - 1.5f * s), ImVec2(c.x + 1 * s, c.y + hh - 1.5f * s),
                                      ImVec2(c.x - 4 * s, c.y + hh + 3 * s), ink);
                if (quiet)
                {
                    line(ImVec2(c.x - 8 * s, c.y + 7.5f * s), ImVec2(c.x + 8 * s, c.y - 7.5f * s),
                         hovered ? Color(0xFFFFFF, a) : Color(Theme::kError, a), 2.2f * s);
                }
                break;
            }
            default:
            {
                const float k = 4.5f * s;
                line(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), ink, 2.2f * s);
                line(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), ink, 2.2f * s);
                break;
            }
            }
            if (hovered)
            {
                // Label to the left of the button.
                const char* label = (i == static_cast<int>(Button::Quiet) && quiet) ? "取消安静" : kLabels[i];
                const float ls = 13 * s;
                const ImVec2 ts = font->CalcTextSizeA(ls, FLT_MAX, 0, label);
                const float lx = r.x - ts.x - 18 * s, ly = c.y - ts.y * 0.5f - 4 * s;
                dl->AddRectFilled(ImVec2(lx, ly), ImVec2(lx + ts.x + 12 * s, ly + ts.y + 8 * s), Color(0x2D3A5C, 0.85f * a), 6 * s);
                dl->AddText(font, ls, ImVec2(lx + 6 * s, ly + 4 * s), Color(0xFFFFFF, a), label);
            }
        }
    }

    ImGui::Render();
#if defined(__APPLE__) || defined(KUJIRA_OVERLAY_GL2)
    // The fixed-function backend does not reset shader state; clear what the
    // Cubism renderer leaves bound so its vertex arrays are not reused.
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    GLint attribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attribs);
    for (GLint i = 0; i < attribs; ++i) glDisableVertexAttribArray(static_cast<GLuint>(i));
    glActiveTexture(GL_TEXTURE0);
#endif
    OverlayBackendRender(ImGui::GetDrawData());
    ImGui::SetCurrentContext(previous);
}
