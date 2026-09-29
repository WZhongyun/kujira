#pragma once

#include <memory>
#include <random>
#include <string>

#include "core/Config.h"
#include "core/Dialogue.h"
#include "core/EventQueue.h"
#include "core/StateMachine.h"
#include "events/EventServer.h"
#include "events/PetEvent.h"
#include "ui/PetOverlay.h"

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
    virtual Dialogue& GetDialogue() = 0;
    virtual void DialogueChanged() = 0;         // save dialogue.json
    virtual void SayPreview(const std::string& text) = 0;
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
    Dialogue& GetDialogue() override { return _dialogue; }
    void DialogueChanged() override;
    void SayPreview(const std::string& text) override;

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

    // Speech bubble, filtered by the bubble settings.
    void Say(const std::string& text, float seconds, PetOverlay::Priority priority);
    void SayForEvent(const PetEvent& event);
    void UpdateTalk(double now);
    void OnToolbarButton(PetOverlay::Button button);

    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void ContentScaleCallback(GLFWwindow* window, float xscale, float yscale);

    Config _config;
    GLFWwindow* _window = nullptr;
    std::unique_ptr<PetModel> _model;
    std::string _modelError;
    std::unique_ptr<StateMachine> _states;
    EventQueue<PetEvent> _queue;
    std::unique_ptr<EventServer> _server;
    std::unique_ptr<SettingsWindow> _settings;
    PetOverlay _overlay;
    PetOverlay::Layout _layout;
    Dialogue _dialogue;
    std::mt19937 _rng{ std::random_device{}() };

    bool _quit = false;
    bool _fitted = false;
    bool _scaleChanged = false;
    bool _passthrough = false;
    bool _hovering = false;
    int _hoverMisses = 0;
    bool _hoverModel = false;       // cursor over the model itself (not the bubble or toolbar)
    double _hoverStart = 0;
    double _lastHoverLine = -100;
    double _toolbarUntil = 0;
    double _nextChat = 0;
    bool _greeted = false;
    StateMachine::State _shownState = StateMachine::State::Idle;
    PetOverlay::Button _pressedButton = PetOverlay::Button::None;
    bool _pressedBubble = false;

    // Drag state (screen coordinates)
    bool _pressed = false;
    bool _dragging = false;
    double _pressScreenX = 0, _pressScreenY = 0;
    int _pressWinX = 0, _pressWinY = 0;
    bool _openSettingsRequested = false;

    double _lastFrame = 0;
    double _lastStayOnTopCheck = 0;
    double _fpsWindowStart = 0;
    double _nextFrame = 0;  // frame deadlines on a fixed timeline, so wait overshoot doesn't lower the rate
    int _fpsFrames = 0;
    float _fps = 0;
};
