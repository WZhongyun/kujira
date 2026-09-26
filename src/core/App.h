#pragma once

#include <memory>
#include <string>

#include "core/Config.h"
#include "core/EventQueue.h"
#include "core/StateMachine.h"
#include "events/EventServer.h"
#include "events/PetEvent.h"

struct GLFWwindow;
class PetModel;
class SettingsWindow;

// What the settings window may read and change.
class AppHost
{
public:
    virtual ~AppHost() = default;
    virtual Config& GetConfig() = 0;
    virtual void ConfigChanged() = 0;           // save + apply window flags, fps, autostart...
    virtual void WindowSizeChanged() = 0;       // windowHeight changed
    virtual const PetModel* Model() const = 0;
    virtual const std::string& ModelError() const = 0;
    virtual void ReloadModel() = 0;
    virtual const StateMachine& States() const = 0;
    virtual const EventServer& Server() const = 0;
    virtual void RestartServer() = 0;
    virtual void Preview(const std::string& actionKey) = 0;
    virtual void ResetWindowPosition() = 0;
    virtual float CurrentFps() const = 0;
    virtual void Quit() = 0;
};

class App : public AppHost
{
public:
    App();
    ~App() override;

    int Run();

    // AppHost
    Config& GetConfig() override { return _config; }
    void ConfigChanged() override;
    void WindowSizeChanged() override;
    const PetModel* Model() const override { return _model.get(); }
    const std::string& ModelError() const override { return _modelError; }
    void ReloadModel() override;
    const StateMachine& States() const override { return *_states; }
    const EventServer& Server() const override { return *_server; }
    void RestartServer() override;
    void Preview(const std::string& actionKey) override;
    void ResetWindowPosition() override;
    float CurrentFps() const override { return _fps; }
    void Quit() override;

private:
    bool InitWindow();
    void InitCubism();
    void LoadModel();
    void ApplyAction(const std::string& key);
    void ApplyHolds();
    void UpdateInput(double now);
    void UpdateHitTest(int fbWidth, int fbHeight);
    void RenderPet();
    void FitWindowToModel();
    void SaveWindowPosition();
    void OpenSettings();
    fs::path ResolveModelDir() const;

    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

    Config _config;
    GLFWwindow* _window = nullptr;
    std::unique_ptr<PetModel> _model;
    std::string _modelError;
    std::unique_ptr<StateMachine> _states;
    EventQueue<PetEvent> _queue;
    std::unique_ptr<EventServer> _server;
    std::unique_ptr<SettingsWindow> _settings;

    bool _quit = false;
    bool _fitted = false;
    bool _passthrough = false;
    bool _hovering = false;
    int _hoverMisses = 0;

    // Drag state (screen coordinates)
    bool _pressed = false;
    bool _dragging = false;
    double _pressScreenX = 0, _pressScreenY = 0;
    int _pressWinX = 0, _pressWinY = 0;
    bool _openSettingsRequested = false;

    double _lastFrame = 0;
    double _fpsWindowStart = 0;
    int _fpsFrames = 0;
    float _fps = 0;
};
