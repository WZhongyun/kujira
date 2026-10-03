#pragma once

#include <functional>
#include <string>
#include <vector>

#include "agents/AgentAdapter.h"
#include "platform/Platform.h"

struct GLFWwindow;
struct ImGuiContext;
struct ImFont;
class AppHost;

// Settings window: its own GLFW window, GL context and ImGui context.
// Created when opened and destroyed when closed, so it costs no memory otherwise.
class SettingsWindow
{
public:
    explicit SettingsWindow(AppHost& host);
    ~SettingsWindow();

    void Open();
    // Opens on 外观与动画, where the model folder and its error are.
    void OpenModelPage();
    void Close();
    bool IsOpen() const { return _window != nullptr; }
    // The user is working in the window right now (input in the last 0.5 s).
    bool IsInteracting(double now) const { return _window && now < _activeUntil; }
    // Renders one frame; closes itself if the user closed the window.
    void Frame();

private:
    void DrawSidebar();
    void DrawGeneral();
    void DrawAppearance();
    void DrawDialogue();
    void DrawActions();
    void DrawAssets();
    void DrawAgents();
    void DrawAbout();
    void SectionTitle(const char* title, const char* caption);
    void Row(const char* label, const char* hint = nullptr);
    void RefreshHookStatus();
    // Host calls that may touch the pet's GL context run after this frame.
    void Later(std::function<void()> fn) { _pending.push_back(std::move(fn)); }

    float ContentScale() const;
    void ApplyScale();

    AppHost& _host;
    GLFWwindow* _window = nullptr;
    float _scale = 0;
    ImGuiContext* _imgui = nullptr;
    ImFont* _titleFont = nullptr;
    Platform::MappedFile _fontFile;
    unsigned int _iconTexture = 0;
    int _page = 0;
    std::vector<std::function<void()>> _pending;

    // Agents page
    std::vector<HookStatus> _hookStatus;
    double _hookStatusTime = -100;
    // Redraw only while the user interacts (plus a short tail for animations);
    // otherwise a few times a second, enough for the memory / fps readout.
    double _activeUntil = 0;
    double _lastDraw = -100;
    int _lastFbW = 0, _lastFbH = 0;
    std::string _hookMessage;
    bool _hookMessageOk = true;
    int _portEdit = 0;
    char _modelDirEdit[1024] = {};

    // Assets page: expressions picked for preview
    std::vector<std::string> _previewExpressions;

    // Dialogue page
    int _dialogueCategory = 0;
    int _dialogueLoaded = -1;
    char _dialogueEdit[8192] = {};
};
