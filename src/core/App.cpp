#include "core/App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

std::string StripSuffix(std::string name, const std::string& suffix)
{
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        name.resize(name.size() - suffix.size());
    }
    return name;
}
}

App::App() = default;

App::~App()
{
    if (_settings) _settings->Close();
    if (_server) _server->Stop();
    if (_window)
    {
        glfwMakeContextCurrent(_window);
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
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    const int h = _config.windowHeight;
    _window = glfwCreateWindow(h, h, "Kujira", nullptr, nullptr);
    if (!_window)
    {
        std::fprintf(stderr, "Cannot create a transparent OpenGL window\n");
        return false;
    }
    glfwSetWindowUserPointer(_window, this);
    glfwSetMouseButtonCallback(_window, MouseButtonCallback);
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

    Platform::MakeToolWindow(_window);
    glfwShowWindow(_window);
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

fs::path App::ResolveModelDir() const
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

    if (!_config.modelDir.empty())
    {
        fs::path dir = FileUtil::FromUtf8(_config.modelDir);
        if (hasModel(dir)) return dir;
    }
    std::vector<fs::path> candidates;
    std::error_code ec;
    for (const auto& root : { FileUtil::ExecutableDir() / "assets" / "models" })
    {
        for (const auto& e : fs::directory_iterator(root, ec))
        {
            if (hasModel(e.path())) candidates.push_back(e.path());
        }
    }
    std::sort(candidates.begin(), candidates.end());
    return candidates.empty() ? fs::path() : candidates.front();
}

void App::LoadModel()
{
    _modelError.clear();
    _fitted = false;
    const fs::path dir = ResolveModelDir();
    if (dir.empty())
    {
        _modelError = "没有找到模型。请把模型文件夹放到 assets/models/ 下，或在设置里填写模型文件夹路径。";
        return;
    }
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
    _model->SetExpressionByName(action.expression);
    if (!action.motion.empty())
    {
        _model->PlayMotion(action.motion);
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
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        double cx, cy;
        glfwGetCursorPos(window, &cx, &cy);
        glfwGetWindowPos(window, &app->_pressWinX, &app->_pressWinY);
        app->_pressScreenX = app->_pressWinX + cx;
        app->_pressScreenY = app->_pressWinY + cy;
        app->_pressed = true;
        app->_dragging = false;
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        if (app->_pressed && !app->_dragging)
        {
            app->Preview(StateMachine::PokeKey());
        }
        if (app->_dragging)
        {
            app->SaveWindowPosition();
        }
        app->_pressed = false;
        app->_dragging = false;
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
    {
        app->_openSettingsRequested = true;
    }
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
            glfwSetWindowPos(_window, _pressWinX + static_cast<int>(std::lround(dx)), _pressWinY + static_cast<int>(std::lround(dy)));
        }
    }

    if (_model)
    {
        if (_config.lookAtMouse && wh > 0)
        {
            // Relative to her face (upper third of the window), saturating a window height away.
            const float nx = static_cast<float>((cx - ww * 0.5) / wh);
            const float ny = static_cast<float>(-(cy - wh * 0.35) / wh);
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
    const float scale = WindowScale(_window);
    const int h = static_cast<int>(_config.windowHeight * scale);
    const int w = std::max(40, static_cast<int>(std::lround(h * _model->Aspect())));
    int x, y, oldW, oldH;
    glfwGetWindowPos(_window, &x, &y);
    glfwGetWindowSize(_window, &oldW, &oldH);
    // Keep the bottom centre where it was.
    glfwSetWindowSize(_window, w, h);
    glfwSetWindowPos(_window, x + (oldW - w) / 2, y + (oldH - h));
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
        _model->Draw(fbw, fbh);
        if (!_fitted && _model->BoundsReady())
        {
            FitWindowToModel();
        }
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
    if (Platform::AutostartSupported() && Platform::IsAutostartEnabled() != _config.autostart)
    {
        Platform::SetAutostart(_config.autostart);
    }
    ApplyHolds();
    if (_model) _model->SetIdleMotion(_config.idleMotion);
    ApplyAction(_states->CurrentKey());
    _config.Save();
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
        OpenSettings();
    }

    while (!_quit && !glfwWindowShouldClose(_window))
    {
        const double frameStart = glfwGetTime();

        for (auto& event : _queue.Drain())
        {
            _states->OnEvent(event, frameStart);
        }
        _states->Update(frameStart, _config.sleepMinutes);
        if (_states->ConsumeChanged())
        {
            ApplyAction(_states->CurrentKey());
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
            _hovering || _settings->IsOpen())
        {
            fps = _config.activeFps;
        }
        else if (_states->CurrentState() == StateMachine::State::Sleeping)
        {
            fps = std::max(5, _config.idleFps / 2);
        }
        if (!visible) fps = 4;
        const double frameEnd = frameStart + 1.0 / fps;
        for (double now = glfwGetTime(); now < frameEnd && !_quit; now = glfwGetTime())
        {
            glfwWaitEventsTimeout(frameEnd - now);
        }
        glfwPollEvents();
    }

    if (_config.windowX != -1 || _config.windowY != -1)
    {
        SaveWindowPosition();
    }
    return 0;
}
