#include "core/App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <CubismFramework.hpp>
#include <ICubismAllocator.hpp>
#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>

#include <nlohmann/json.hpp>

#include "core/PetModel.h"
#include "platform/Platform.h"
#include "ui/SettingsWindow.h"

namespace
{
// Started by an agent: after its last session ends she closes once the farewell
// action's hold time is over, but never sooner than this, so a session that ends
// and immediately starts again (/clear) doesn't close and restart her.
constexpr double kMinFollowQuitDelay = 1.0;
// Pseudo action key shown as an overlay while she is being dragged.
const char kDragKey[] = "drag";

class Allocator : public Csm::ICubismAllocator
{
public:
    void* Allocate(const Csm::csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void* memory) override { std::free(memory); }
    void* AllocateAligned(const Csm::csmSizeType size, const Csm::csmUint32 alignment) override
    {
        const size_t offset = alignment - 1 + sizeof(void*);
        void* raw = std::malloc(size + offset);
        if (!raw) return nullptr;
        size_t aligned = reinterpret_cast<size_t>(raw) + sizeof(void*);
        aligned = (aligned + alignment - 1) / alignment * alignment;
        reinterpret_cast<void**>(aligned)[-1] = raw;
        return reinterpret_cast<void*>(aligned);
    }
    void DeallocateAligned(void* aligned) override
    {
        if (aligned) std::free(static_cast<void**>(aligned)[-1]);
    }
};

Allocator g_allocator;
Csm::CubismFramework::Option g_cubismOption;

void CubismLog(const Csm::csmChar* message)
{
    std::fprintf(stderr, "%s", message);
}

// Used by the Framework for its shader files; relative paths resolve next to the exe.
Csm::csmByte* CubismLoadFile(const std::string path, Csm::csmSizeInt* outSize)
{
    fs::path p = FileUtil::FromUtf8(path);
    if (p.is_relative()) p = FileUtil::ExecutableDir() / p;
    auto text = FileUtil::ReadText(p);
    if (!text)
    {
        *outSize = 0;
        return nullptr;
    }
    auto* buf = static_cast<Csm::csmByte*>(std::malloc(text->size() + 1));
    std::memcpy(buf, text->data(), text->size());
    buf[text->size()] = 0;
    *outSize = static_cast<Csm::csmSizeInt>(text->size());
    return buf;
}

void CubismReleaseBytes(Csm::csmByte* data)
{
    std::free(data);
}

float WindowScale(GLFWwindow* window)
{
#ifdef __APPLE__
    (void)window;
    return 1.0f;  // screen coordinates are already logical points
#else
    float sx = 1, sy = 1;
    glfwGetWindowContentScale(window, &sx, &sy);
    return sy > 0 ? sy : 1.0f;
#endif
}

// Final replies are Markdown; the bubble shows plain text on as few lines as possible.
std::string PlainText(const std::string& markdown)
{
    std::string out;
    bool lineStart = true;
    for (size_t i = 0; i < markdown.size(); ++i)
    {
        const char c = markdown[i];
        if (c == '\n' || c == '\r' || c == '\t')
        {
            if (!out.empty() && out.back() != ' ') out += ' ';
            lineStart = true;
            continue;
        }
        if (lineStart && (c == '#' || c == '>' || ((c == '-' || c == '*') && i + 1 < markdown.size() && markdown[i + 1] == ' ')))
        {
            continue;
        }
        if (c == '`' || (c == '*' && i + 1 < markdown.size() && markdown[i + 1] == '*'))
        {
            if (c == '*') ++i;
            continue;
        }
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
        lineStart = false;
        out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string StripSuffix(std::string name, const std::string& suffix)
{
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        name.resize(name.size() - suffix.size());
    }
    return name;
}
}

App::App(bool launchedByAgent) : _launchedByAgent(launchedByAgent) {}

App::~App()
{
    if (_settings) _settings->Close();
    if (_server) _server->Stop();
    if (_window)
    {
        glfwMakeContextCurrent(_window);
        _overlay.Shutdown();
        _model.reset();
        Csm::Rendering::CubismOffscreenManager_OpenGLES2::ReleaseInstance();
        Csm::CubismFramework::Dispose();
        glfwDestroyWindow(_window);
    }
    glfwTerminate();
}

bool App::InitWindow()
{
    if (!glfwInit())
    {
        std::fprintf(stderr, "glfwInit failed\n");
        return false;
    }

    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, _config.topmost ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    // No GLFW_SCALE_TO_MONITOR: the window is sized from the model, and a scaling
    // change (another monitor) is handled by ContentScaleCallback + FitWindowToModel.

    const int h = _config.windowHeight;
    _window = glfwCreateWindow(h, h, "Kujira", nullptr, nullptr);
    if (!_window)
    {
        std::fprintf(stderr, "Cannot create a transparent OpenGL window\n");
        return false;
    }
    glfwSetWindowUserPointer(_window, this);
    glfwSetMouseButtonCallback(_window, MouseButtonCallback);
    glfwSetWindowContentScaleCallback(_window, ContentScaleCallback);
    glfwMakeContextCurrent(_window);
    glfwSwapInterval(0);

    glewExperimental = GL_TRUE;
    GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK)
    {
        std::fprintf(stderr, "glewInit: %s\n", reinterpret_cast<const char*>(glewGetErrorString(glewStatus)));
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // Size in logical pixels -> screen coordinates.
    const float scale = WindowScale(_window);
    glfwSetWindowSize(_window, static_cast<int>(h * scale), static_cast<int>(h * scale));
    // Restore the saved position only if it is still on a connected monitor.
    const int savedX = _config.windowX, savedY = _config.windowY;
    bool onScreen = false;
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    for (int i = 0; i < monitorCount; ++i)
    {
        int ax, ay, aw, ah;
        glfwGetMonitorWorkarea(monitors[i], &ax, &ay, &aw, &ah);
        if (savedX >= ax - 40 && savedY >= ay - 40 && savedX < ax + aw - 40 && savedY < ay + ah - 40) onScreen = true;
    }
    ResetWindowPosition();
    if ((savedX != -1 || savedY != -1) && onScreen)
    {
        glfwSetWindowPos(_window, savedX, savedY);
        _config.windowX = savedX;
        _config.windowY = savedY;
    }

    _overlay.Init(_window);
    _layout.scale = scale;
    Platform::MakeToolWindow(_window);
    glfwShowWindow(_window);
    Platform::SetStayOnTop(_window, _config.topmost && _config.keepOnTop, _config.hideForFullscreen);
    return true;
}

void App::InitCubism()
{
    g_cubismOption.LogFunction = CubismLog;
    g_cubismOption.LoggingLevel = Csm::CubismFramework::Option::LogLevel_Warning;
    g_cubismOption.LoadFileFunction = CubismLoadFile;
    g_cubismOption.ReleaseBytesFunction = CubismReleaseBytes;
    Csm::CubismFramework::StartUp(&g_allocator, &g_cubismOption);
    Csm::CubismFramework::Initialize();
}

fs::path App::ResolveModelDir(std::string* error) const
{
    auto hasModel = [](const fs::path& dir) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return false;
        for (const auto& e : fs::directory_iterator(dir, ec))
        {
            const std::string name = FileUtil::ToUtf8(e.path().filename());
            if (name.size() > 12 && name.compare(name.size() - 12, 12, ".model3.json") == 0) return true;
        }
        return false;
    };

    // A folder picked in settings wins; a relative one is next to the exe, not the
    // working directory, so the whole folder can be moved around.
    std::string manualError;
    if (!_config.modelDir.empty())
    {
        fs::path dir = FileUtil::FromUtf8(_config.modelDir);
        if (dir.is_relative()) dir = FileUtil::ExecutableDir() / dir;
        if (hasModel(dir)) return dir;
        manualError = "设置里填写的模型文件夹找不到模型（没有 .model3.json）：" + FileUtil::ToUtf8(dir);
    }

    // Then models/ next to the exe (release layout), then assets/models/ (older
    // builds). Either the folder holds model folders, or the model files directly.
    std::vector<fs::path> candidates;
    std::error_code ec;
    for (const fs::path& root : ModelRoots())
    {
        if (hasModel(root))
        {
            candidates.push_back(root);
            break;
        }
        std::vector<fs::path> found;
        for (const auto& e : fs::directory_iterator(root, ec))
        {
            if (hasModel(e.path())) found.push_back(e.path());
        }
        if (!found.empty())
        {
            std::sort(found.begin(), found.end());
            candidates.push_back(found.front());
            break;
        }
    }
    if (!candidates.empty())
    {
        if (error && !manualError.empty())
            *error = manualError + "\n已改用自动找到的模型：" + FileUtil::ToUtf8(candidates.front());
        return candidates.front();
    }

    if (error)
    {
        std::string text = manualError.empty() ? "没有找到模型。" : manualError + "\n自动查找也没有找到模型。";
        text += "\n请把模型文件夹（里面有 .model3.json 的那个）放到：\n  " + FileUtil::ToUtf8(ModelRoots().front()) +
                "\n然后点「重新加载模型」；也可以在上面填写模型文件夹的路径。";
        *error = text;
    }
    return fs::path();
}

std::vector<fs::path> ModelRoots()
{
    const fs::path exe = FileUtil::ExecutableDir();
    return { exe / "models", exe / "assets" / "models" };
}

void App::LoadModel()
{
    _modelError.clear();
    _fitted = false;
    const fs::path dir = ResolveModelDir(&_modelError);
    if (dir.empty()) return;
    auto model = std::make_unique<PetModel>();
    std::string error;
    if (!model->Load(dir, &error))
    {
        _modelError = error;
        return;
    }

    // Idle loop: configured name, else what the VTube Studio config names.
    std::string idle = _config.idleMotion;
    const auto& motions = model->MotionNames();
    if (std::find(motions.begin(), motions.end(), idle) == motions.end())
    {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec))
        {
            const std::string name = FileUtil::ToUtf8(e.path().filename());
            if (name.size() < 11 || name.compare(name.size() - 11, 11, ".vtube.json") != 0) continue;
            auto text = FileUtil::ReadText(e.path());
            auto j = nlohmann::json::parse(text ? *text : "", nullptr, false);
            if (j.is_object() && j.contains("FileReferences") && j["FileReferences"].contains("IdleAnimation") &&
                j["FileReferences"]["IdleAnimation"].is_string())
            {
                idle = StripSuffix(j["FileReferences"]["IdleAnimation"].get<std::string>(), ".motion3.json");
            }
        }
    }
    model->SetIdleMotion(idle);
    _model = std::move(model);
    ApplyAction(_states->CurrentKey());
}

void App::ApplyHolds()
{
    for (const auto& [key, action] : _config.actions)
    {
        _states->SetHoldSeconds(key, action.holdSeconds);
    }
}

void App::ApplyAction(const std::string& key)
{
    if (!_model) return;
    auto it = _config.actions.find(key);
    const StateAction action = it != _config.actions.end() ? it->second : StateAction{};
    std::vector<std::string> expressions = _config.outfit;
    expressions.insert(expressions.end(), action.expressions.begin(), action.expressions.end());
    _model->SetExpressions(expressions);
    if (action.motion.empty() || !_model->PlayMotion(action.motion, action.loop))
    {
        _model->StopMotion();
    }
}

void App::Preview(const std::string& key)
{
    auto it = _config.actions.find(key);
    float hold = (it != _config.actions.end() && it->second.holdSeconds > 0) ? it->second.holdSeconds : 3.0f;
    _states->ShowOverlay(key, hold, glfwGetTime());
}

void App::MouseButtonCallback(GLFWwindow* window, int button, int action, int)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    double cx, cy;
    glfwGetCursorPos(window, &cx, &cy);
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        // Toolbar buttons and the bubble take the click; everything else is the model.
        app->_pressedButton = app->_overlay.ButtonAt(cx, cy);
        app->_pressedBubble = app->_pressedButton == PetOverlay::Button::None && app->_overlay.BubbleContains(cx, cy);
        if (app->_pressedButton != PetOverlay::Button::None || app->_pressedBubble) return;
        glfwGetWindowPos(window, &app->_pressWinX, &app->_pressWinY);
        app->_pressScreenX = app->_pressWinX + cx;
        app->_pressScreenY = app->_pressWinY + cy;
        app->_pressed = true;
        app->_dragging = false;
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        if (app->_pressedButton != PetOverlay::Button::None)
        {
            if (app->_overlay.ButtonAt(cx, cy) == app->_pressedButton) app->OnToolbarButton(app->_pressedButton);
            app->_pressedButton = PetOverlay::Button::None;
            return;
        }
        if (app->_pressedBubble)
        {
            app->_pressedBubble = false;
            app->_overlay.Dismiss();
            return;
        }
        if (app->_pressed && !app->_dragging)
        {
            app->Preview(StateMachine::PokeKey());
            app->Say(app->_dialogue.Pick("click"), 3.0f, PetOverlay::Priority::Interaction);
        }
        if (app->_dragging)
        {
            app->SaveWindowPosition();
            app->Say(app->_dialogue.Pick("drag"), 3.0f, PetOverlay::Priority::Interaction);
        }
        app->_pressed = false;
        app->_dragging = false;
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
    {
        app->_openSettingsRequested = true;
    }
}

void App::ContentScaleCallback(GLFWwindow* window, float, float)
{
    static_cast<App*>(glfwGetWindowUserPointer(window))->_scaleChanged = true;
}

void App::UpdateInput(double)
{
    double cx, cy;
    glfwGetCursorPos(_window, &cx, &cy);
    int wx, wy, ww, wh;
    glfwGetWindowPos(_window, &wx, &wy);
    glfwGetWindowSize(_window, &ww, &wh);

    if (_pressed)
    {
        if (glfwGetMouseButton(_window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS && !_dragging)
        {
            _pressed = false;  // release happened outside our window
        }
        const double sx = wx + cx, sy = wy + cy;
        const double dx = sx - _pressScreenX, dy = sy - _pressScreenY;
        if (!_dragging && dx * dx + dy * dy > 16.0)
        {
            _dragging = true;
        }
        if (_dragging)
        {
            auto it = _config.actions.find(kDragKey);
            const float hold = it != _config.actions.end() ? it->second.holdSeconds : 0.8f;
            _states->ShowOverlay(kDragKey, hold, glfwGetTime());
            glfwSetWindowPos(_window, _pressWinX + static_cast<int>(std::lround(dx)), _pressWinY + static_cast<int>(std::lround(dy)));
        }
    }

    if (_model)
    {
        if (_config.lookAtMouse && wh > 0)
        {
            // Relative to her face (upper third of the model), saturating a model height away.
            const PetOverlay::Rect& m = _layout.model;
            const double mh = m.h > 0 ? m.h : wh;
            const double mx = m.w > 0 ? m.x + m.w * 0.5 : ww * 0.5;
            const double my = m.h > 0 ? m.y + m.h * 0.35 : wh * 0.35;
            const float nx = static_cast<float>((cx - mx) / mh);
            const float ny = static_cast<float>(-(cy - my) / mh);
            _model->LookAt(std::clamp(nx, -1.0f, 1.0f), std::clamp(ny, -1.0f, 1.0f));
        }
        else
        {
            _model->LookAt(0, 0);
        }
    }
}

void App::UpdateHitTest(int fbWidth, int fbHeight)
{
    bool wantPassthrough = false;
    if (_config.clickThrough && !_pressed)
    {
        double cx, cy;
        int ww, wh;
        glfwGetCursorPos(_window, &cx, &cy);
        glfwGetWindowSize(_window, &ww, &wh);
        bool hit = false;
        if (cx >= 0 && cy >= 0 && cx < ww && cy < wh && ww > 0 && wh > 0)
        {
            const int px = std::clamp(static_cast<int>(cx * fbWidth / ww), 0, fbWidth - 1);
            const int py = std::clamp(fbHeight - 1 - static_cast<int>(cy * fbHeight / wh), 0, fbHeight - 1);
            unsigned char rgba[4] = { 0, 0, 0, 0 };
            glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            hit = rgba[3] > 24;
        }
        // A little hysteresis so the edge does not flicker between states.
        if (hit)
        {
            _hovering = true;
            _hoverMisses = 0;
        }
        else if (++_hoverMisses >= 3)
        {
            _hovering = false;
        }
        wantPassthrough = !_hovering;
        _hoverModel = hit && (_layout.model.w <= 0 || _layout.model.Contains(cx, cy)) &&
                      _overlay.ButtonAt(cx, cy) == PetOverlay::Button::None && !_overlay.BubbleContains(cx, cy);
    }
    else
    {
        _hoverModel = false;
    }
    if (wantPassthrough != _passthrough)
    {
        _passthrough = wantPassthrough;
        glfwSetWindowAttrib(_window, GLFW_MOUSE_PASSTHROUGH, _passthrough ? GLFW_TRUE : GLFW_FALSE);
    }
}

void App::FitWindowToModel()
{
    if (!_model || !_model->BoundsReady()) return;
    // Model at the bottom, room for the speech bubble above it and for the
    // toolbar beside it (mirrored on the other side so she stays centred).
    const float scale = WindowScale(_window);
    const float modelH = std::round(_config.windowHeight * scale);
    const float modelW = std::max(40.0f, std::round(modelH * _model->Aspect()));
    const bool bubbles = _config.bubbleMode != "off";
    const float side = _config.showToolbar ? std::round(40 * scale) : 0.0f;
    const float bubbleH = bubbles ? std::round(120 * scale) : 0.0f;
    float fw = modelW + 2 * side;
    if (bubbles) fw = std::max(fw, std::round(260 * scale));
    const int w = static_cast<int>(fw);
    const int h = static_cast<int>(modelH + bubbleH);

    int x, y, oldW, oldH;
    glfwGetWindowPos(_window, &x, &y);
    glfwGetWindowSize(_window, &oldW, &oldH);
    // Keep the bottom centre where it was.
    if (w != oldW || h != oldH)
    {
        int nx = x + (oldW - w) / 2, ny = y + (oldH - h);
        // Growing can push the window past the screen edge; keep it on the monitor's work area.
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        const int cx = x + oldW / 2, cy = y + oldH / 2;
        for (int i = 0; i < count; ++i)
        {
            int ax, ay, aw, ah;
            glfwGetMonitorWorkarea(monitors[i], &ax, &ay, &aw, &ah);
            if (cx >= ax && cx < ax + aw && cy >= ay && cy < ay + ah)
            {
                nx = std::clamp(nx, ax, std::max(ax, ax + aw - w));
                ny = std::clamp(ny, ay, std::max(ay, ay + ah - h));
                break;
            }
        }
        glfwSetWindowSize(_window, w, h);
        glfwSetWindowPos(_window, nx, ny);
    }
    _layout.model = { std::round((w - modelW) * 0.5f), bubbleH, modelW, modelH };
    _layout.bubbleTop = 4 * scale;
    _layout.bubbleMaxW = std::min(w - 8 * scale, 300 * scale);
    _layout.scale = scale;
    _layout.toolbar = _config.showToolbar;
    _fitted = true;
}

void App::RenderPet()
{
    glfwMakeContextCurrent(_window);
    int fbw, fbh;
    glfwGetFramebufferSize(_window, &fbw, &fbh);
    if (fbw <= 0 || fbh <= 0) return;

    const double now = glfwGetTime();
    const float dt = static_cast<float>(std::min(now - _lastFrame, 0.25));
    _lastFrame = now;

    glViewport(0, 0, fbw, fbh);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (_model)
    {
        static int lastW = 0, lastH = 0;
        if (fbw != lastW || fbh != lastH)
        {
            _model->SetRenderTargetSize(fbw, fbh);
            lastW = fbw;
            lastH = fbh;
        }
        _model->Update(dt);
        if (_fitted)
        {
            int ww, wh;
            glfwGetWindowSize(_window, &ww, &wh);
            const float rx = static_cast<float>(fbw) / std::max(1, ww), ry = static_cast<float>(fbh) / std::max(1, wh);
            const PetOverlay::Rect& m = _layout.model;
            _model->Draw(fbw, fbh, m.x * rx, fbh - (m.y + m.h) * ry, m.w * rx, m.h * ry);
        }
        else
        {
            _model->Draw(fbw, fbh);
        }
        if (!_fitted && _model->BoundsReady())
        {
            FitWindowToModel();
        }
    }
    if (_fitted)
    {
        double cx, cy;
        glfwGetCursorPos(_window, &cx, &cy);
        const bool overToolbar = _overlay.ButtonAt(cx, cy) != PetOverlay::Button::None;
        if (_hoverModel || overToolbar) _toolbarUntil = now + 1.2;
        _overlay.Render(_layout, now, now < _toolbarUntil && !_dragging, _config.quietMode, cx, cy);
    }
    UpdateHitTest(fbw, fbh);
    glfwSwapBuffers(_window);

    ++_fpsFrames;
    if (now - _fpsWindowStart >= 1.0)
    {
        _fps = static_cast<float>(_fpsFrames / (now - _fpsWindowStart));
        _fpsFrames = 0;
        _fpsWindowStart = now;
    }
}

void App::SaveWindowPosition()
{
    glfwGetWindowPos(_window, &_config.windowX, &_config.windowY);
    _config.Save();
}

void App::ResetWindowPosition()
{
    int ww, wh;
    glfwGetWindowSize(_window, &ww, &wh);
    int ax = 0, ay = 0, aw = 1280, ah = 720;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
    {
        glfwGetMonitorWorkarea(monitor, &ax, &ay, &aw, &ah);
    }
    const int margin = static_cast<int>(24 * WindowScale(_window));
    glfwSetWindowPos(_window, ax + aw - ww - margin, ay + ah - wh);
    _config.windowX = -1;
    _config.windowY = -1;
}

void App::ConfigChanged()
{
    glfwSetWindowAttrib(_window, GLFW_FLOATING, _config.topmost ? GLFW_TRUE : GLFW_FALSE);
    Platform::SetStayOnTop(_window, _config.topmost && _config.keepOnTop, _config.hideForFullscreen);
    if (Platform::AutostartSupported() && Platform::IsAutostartEnabled() != _config.autostart)
    {
        Platform::SetAutostart(_config.autostart);
    }
    ApplyHolds();
    if (_model) _model->SetIdleMotion(_config.idleMotion);
    ApplyAction(_states->CurrentKey());
    if (_fitted) FitWindowToModel();  // bubble / toolbar space may have changed
    _config.Save();
}

void App::DialogueChanged()
{
    _dialogue.Save();
}

void App::SayPreview(const std::string& text)
{
    if (_fitted && _config.bubbleMode != "off")
    {
        _overlay.Say(text, _config.bubbleSeconds, PetOverlay::Priority::Result, glfwGetTime());
    }
}

void App::Say(const std::string& text, float seconds, PetOverlay::Priority priority)
{
    using P = PetOverlay::Priority;
    if (text.empty() || !_fitted) return;
    if (_config.bubbleMode == "off") return;
    if (_config.bubbleMode == "important" && priority < P::Result) return;
    if (_config.quietMode && priority <= P::Interaction) return;
    if (!_config.interactionText && priority == P::Interaction) return;
    _overlay.Say(text, seconds, priority, glfwGetTime());
}

void App::SayForEvent(const PetEvent& e)
{
    using K = PetEvent::Kind;
    using P = PetOverlay::Priority;
    const float seconds = _config.bubbleSeconds;
    switch (e.kind)
    {
    case K::SessionStart: Say(_dialogue.Pick("agentStart"), seconds, P::Status); break;
    case K::SessionEnd: Say(_dialogue.Pick("farewell"), seconds, P::Status); break;
    case K::ToolRead:
        if (!e.text.empty()) Say(_dialogue.Pick("reading", e.text), seconds, P::Status);
        break;
    case K::ToolWrite:
        if (!e.text.empty()) Say(_dialogue.Pick("writing", e.text), seconds, P::Status);
        break;
    case K::ToolRun:
        if (!e.text.empty()) Say(_dialogue.Pick("running", e.text), seconds, P::Status);
        break;
    case K::Attention:
        // Stays up until she stops waiting (or the bubble is clicked).
        Say(e.text.empty() ? _dialogue.Pick("attention") : e.text, 0, P::Attention);
        break;
    case K::Stop:
    {
        std::string reply = PlainText(e.text);
        Say(reply.empty() ? _dialogue.Pick("done") : reply, std::max(8.0f, seconds * 1.6f), P::Result);
        break;
    }
    case K::PromptSubmit:
    case K::ToolDone:
        if (_overlay.CurrentPriority() == P::Attention) _overlay.ClearSticky();  // the user answered
        break;
    default:
        break;
    }
}

void App::UpdateTalk(double now)
{
    using P = PetOverlay::Priority;
    using S = StateMachine::State;
    if (!_fitted) return;

    if (!_greeted)
    {
        _greeted = true;
        Say(_dialogue.Pick("greeting"), _config.bubbleSeconds, P::Chatter);
    }

    // Falling asleep / waking up, and the end of waiting for the user.
    const S state = _states->CurrentState();
    if (state != _shownState)
    {
        if (_shownState == S::Attention && _overlay.CurrentPriority() == P::Attention) _overlay.ClearSticky();
        if (state == S::Sleeping) Say(_dialogue.Pick("sleep"), _config.bubbleSeconds, P::Chatter);
        else if (_shownState == S::Sleeping) Say(_dialogue.Pick("wake"), _config.bubbleSeconds, P::Chatter);
        _shownState = state;
    }

    // Hovering over her for a moment.
    if (_hoverModel && !_pressed)
    {
        if (_hoverStart == 0) _hoverStart = now;
        if (now - _hoverStart > 1.5 && now - _lastHoverLine > 30)
        {
            _lastHoverLine = now;
            Say(_dialogue.Pick("hover"), 3.0f, P::Interaction);
        }
    }
    else
    {
        _hoverStart = 0;
    }

    // Small talk every ~chatMinutes while nothing else is going on.
    if (_config.chatMinutes <= 0)
    {
        _nextChat = 0;
        return;
    }
    const double interval = _config.chatMinutes * 60.0;
    if (_nextChat == 0)
    {
        _nextChat = now + interval * std::uniform_real_distribution<double>(0.7, 1.3)(_rng);
    }
    if (now < _nextChat) return;
    if (_states->IsBusy() || state == S::Sleeping || _overlay.BubbleVisible(now) || _pressed)
    {
        _nextChat = now + 60;  // try again in a minute
        return;
    }
    std::time_t t = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    std::string key = std::uniform_real_distribution<double>(0, 1)(_rng) < 0.65 ? Dialogue::TimeOfDayKey(local.tm_hour) : "idle";
    std::string line = _dialogue.Pick(key);
    if (line.empty()) line = _dialogue.Pick(key == "idle" ? Dialogue::TimeOfDayKey(local.tm_hour) : "idle");
    Say(line, _config.bubbleSeconds, P::Chatter);
    _nextChat = now + interval * std::uniform_real_distribution<double>(0.7, 1.3)(_rng);
}

void App::TrackAgentSession(const PetEvent& event, double now)
{
    if (!_launchedByAgent) return;
    const std::string id = event.agent + ":" + event.sessionId;
    if (event.kind == PetEvent::Kind::SessionEnd)
    {
        _agentSessions.erase(id);
        if (_agentSessions.empty())
        {
            auto it = _config.actions.find(StateMachine::Key(StateMachine::State::Farewell));
            const double hold = it != _config.actions.end() ? it->second.holdSeconds : 0.0;
            _followQuitAt = now + std::max(hold, kMinFollowQuitDelay);
        }
    }
    else
    {
        // Any event counts: sessions that began before she was started show up here too.
        _agentSessions.insert(id);
        _followQuitAt = -1;
    }
}

void App::UpdateFollowQuit(double now)
{
    if (_followQuitAt < 0 || now < _followQuitAt) return;
    if (!_config.launchWithAgent)
    {
        _followQuitAt = -1;  // turned off since she was started: stay
        return;
    }
    // Never close under an open settings window.
    if (_settings->IsOpen()) return;
    _quit = true;
}

void App::OnToolbarButton(PetOverlay::Button button)
{
    switch (button)
    {
    case PetOverlay::Button::Settings:
        _openSettingsRequested = true;
        break;
    case PetOverlay::Button::Quiet:
        _config.quietMode = !_config.quietMode;
        _config.Save();
        if (_config.bubbleMode != "off")
        {
            _overlay.Say(_config.quietMode ? "好的，我安静一会儿" : "我回来啦～", 2.5f, PetOverlay::Priority::Result, glfwGetTime());
        }
        break;
    case PetOverlay::Button::Quit:
        _quit = true;
        break;
    default:
        break;
    }
}

void App::WindowSizeChanged()
{
    _fitted = false;
    FitWindowToModel();
    _config.Save();
}

void App::ReloadModel()
{
    glfwMakeContextCurrent(_window);
    _model.reset();
    LoadModel();
}

void App::RestartServer()
{
    _server->Start(_config.port, _config.token);
}

void App::Quit()
{
    _quit = true;
}

void App::OpenSettings()
{
    _settings->Open();
}

int App::Run()
{
    if (!Platform::AcquireSingleInstance())
    {
        std::fprintf(stderr, "Kujira is already running\n");
        return 0;
    }

    _config = Config::Load();
    _dialogue = Dialogue::Load();
    if (Platform::AutostartSupported())
    {
        _config.autostart = Platform::IsAutostartEnabled();
    }
    _config.Save();

    if (!InitWindow()) return 1;
    InitCubism();

    const double start = glfwGetTime();
    _lastFrame = start;
    _fpsWindowStart = start;
    _states = std::make_unique<StateMachine>(start);
    ApplyHolds();
    LoadModel();

    _server = std::make_unique<EventServer>(_queue);
    RestartServer();

    _settings = std::make_unique<SettingsWindow>(*this);
    if (!_modelError.empty())
    {
        std::fprintf(stderr, "%s\n", _modelError.c_str());
        _settings->OpenModelPage();
    }

    while (!_quit && !glfwWindowShouldClose(_window))
    {
        const double frameStart = glfwGetTime();

        for (auto& event : _queue.Drain())
        {
            _states->OnEvent(event, frameStart);
            SayForEvent(event);
            TrackAgentSession(event, frameStart);
        }
        UpdateFollowQuit(frameStart);
        _states->Update(frameStart, _config.sleepMinutes);
        if (_states->ConsumeChanged())
        {
            ApplyAction(_states->CurrentKey());
        }
        UpdateTalk(frameStart);

        if (frameStart - _lastStayOnTopCheck >= 1.0)
        {
            _lastStayOnTopCheck = frameStart;
            Platform::UpdateStayOnTop();
        }

        // Moved to a monitor with other scaling: resize once she is let go, not mid-drag.
        if (_scaleChanged && !_pressed && _fitted)
        {
            _scaleChanged = false;
            FitWindowToModel();
            SaveWindowPosition();
        }

        const bool visible = glfwGetWindowAttrib(_window, GLFW_VISIBLE) && !glfwGetWindowAttrib(_window, GLFW_ICONIFIED);
        if (visible)
        {
            UpdateInput(frameStart);
            RenderPet();
        }

        if (_openSettingsRequested)
        {
            _openSettingsRequested = false;
            OpenSettings();
        }
        if (_settings->IsOpen())
        {
            _settings->Frame();
        }

        // Frame pacing: full speed while something happens, slow when idle, slower asleep.
        int fps = _config.idleFps;
        if (_states->IsBusy() || _states->CurrentKey() != StateMachine::Key(_states->CurrentState()) || _pressed ||
            _hovering || _settings->IsInteracting(frameStart) || _overlay.BubbleVisible(frameStart) || _overlay.Animating() ||
            frameStart < _toolbarUntil)
        {
            fps = _config.activeFps;
        }
        else if (_states->CurrentState() == StateMachine::State::Sleeping)
        {
            fps = std::max(5, _config.idleFps / 2);
        }
        if (!visible) fps = 4;
        // Deadlines advance by exactly one period, so a late wake-up (coarse
        // timers, vsync in the compositor) is made up on the next frame instead
        // of lowering the rate. Too far behind (or rate changed): restart.
        const double period = 1.0 / fps;
        _nextFrame += period;
        if (_nextFrame < frameStart || _nextFrame > frameStart + period) _nextFrame = frameStart + period;
        const double frameEnd = _nextFrame;
        for (double now = glfwGetTime(); now < frameEnd && !_quit; now = glfwGetTime())
        {
            Platform::WaitEvents(frameEnd - now);
        }
        glfwPollEvents();
    }

    if (_config.windowX != -1 || _config.windowY != -1)
    {
        SaveWindowPosition();
    }
    return 0;
}
