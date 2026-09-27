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
    void Close();
    bool IsOpen() const { return _window != nullptr; }
    // Renders one frame; closes itself if the user closed the window.
    void Frame();

private:
    void DrawSidebar();
    void DrawGeneral();
    void DrawAppearance();
    void DrawActions();
    void DrawAgents();
    void DrawAbout();
    void SectionTitle(const char* title, const char* caption);
    void Row(const char* label, const char* hint = nullptr);
    void RefreshHookStatus();
    // Host calls that may touch the pet's GL context run after this frame.
    void Later(std::function<void()> fn) { _pending.push_back(std::move(fn)); }

    AppHost& _host;
    GLFWwindow* _window = nullptr;
    ImGuiContext* _imgui = nullptr;
    ImFont* _titleFont = nullptr;
    Platform::MappedFile _fontFile;
    unsigned int _iconTexture = 0;
    int _page = 0;
    std::vector<std::function<void()>> _pending;

    // Agents page
    std::vector<HookStatus> _hookStatus;
    double _hookStatusTime = -100;
    std::string _hookMessage;
    bool _hookMessageOk = true;
    int _portEdit = 0;
    char _modelDirEdit[1024] = {};
};
