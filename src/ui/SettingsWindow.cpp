#include "ui/SettingsWindow.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include "core/App.h"
#include "core/PetModel.h"
#include "platform/Platform.h"
#include "stb_image.h"
#include "ui/Theme.h"

namespace
{
constexpr int kWidth = 960;
constexpr int kHeight = 620;
constexpr float kLabelColumn = 400.0f;

const char* kPages[] = { "常规", "外观与动画", "气泡与台词", "状态映射", "Agent 接入", "关于" };

bool NameCombo(const char* id, std::string& value, const std::vector<std::string>& names, const char* noneLabel)
{
    bool changed = false;
    const std::string preview = value.empty() ? noneLabel : value;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge))
    {
        if (ImGui::Selectable(noneLabel, value.empty()))
        {
            value.clear();
            changed = true;
        }
        for (const auto& name : names)
        {
            const bool selected = name == value;
            if (ImGui::Selectable(name.c_str(), selected))
            {
                value = name;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Label for an expression / motion: the name the model's author gave it, if any.
std::string Label(const PetModel* model, const std::string& name)
{
    return model ? model->DisplayName(name) : name;
}

// Combo with a checkbox per name; several can be picked.
bool MultiNameCombo(const char* id, std::vector<std::string>& values, const std::vector<std::string>& names,
                    const PetModel* model, const char* noneLabel)
{
    bool changed = false;
    std::string preview;
    for (const auto& v : values)
    {
        if (!preview.empty()) preview += "、";
        preview += Label(model, v);
    }
    if (preview.empty()) preview = noneLabel;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge))
    {
        if (ImGui::Selectable(noneLabel, values.empty(), ImGuiSelectableFlags_NoAutoClosePopups))
        {
            values.clear();
            changed = true;
        }
        for (const auto& name : names)
        {
            auto it = std::find(values.begin(), values.end(), name);
            bool on = it != values.end();
            ImGui::PushID(name.c_str());
            if (ImGui::Checkbox(Label(model, name).c_str(), &on))
            {
                if (on) values.push_back(name);
                else values.erase(it);
                changed = true;
            }
            if (Label(model, name) != name && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Loads the model icon as a texture for the sidebar and as the window icon.
unsigned int LoadIcon(GLFWwindow* window, const fs::path& path)
{
    auto bytes = FileUtil::ReadText(path);
    if (!bytes) return 0;
    int w, h, n;
    unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes->data()),
                                              static_cast<int>(bytes->size()), &w, &h, &n, 4);
    if (!px) return 0;
    GLFWimage image{ w, h, px };
    glfwSetWindowIcon(window, 1, &image);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    return tex;
}
}

SettingsWindow::SettingsWindow(AppHost& host)
    : _host(host)
{
}

SettingsWindow::~SettingsWindow()
{
    Close();
}

void SettingsWindow::OpenModelPage()
{
    _page = 1;
    Open();
}

void SettingsWindow::Open()
{
    _activeUntil = glfwGetTime() + 1.0;
    if (_window)
    {
        glfwShowWindow(_window);
        glfwFocusWindow(_window);
        return;
    }

    GLFWwindow* previous = glfwGetCurrentContext();
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    _window = glfwCreateWindow(kWidth, kHeight, "鲸鱼娘 · 设置", nullptr, nullptr);
    if (!_window)
    {
        glfwMakeContextCurrent(previous);
        return;
    }
    glfwSetWindowSizeLimits(_window, 720, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(_window);
    glfwSwapInterval(0);

    IMGUI_CHECKVERSION();
    _imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(_imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    _scale = 0;
    ApplyScale();

    // Glyphs are loaded on demand, so any Chinese text (including expression names) renders.
    ImFont* regular = nullptr;
    for (const auto& [path, index] : Platform::CjkFontCandidates())
    {
        std::error_code ec;
        if (!fs::exists(path, ec)) continue;
        // Map the system font instead of reading it: a CJK font is ~20 MB, and mapped
        // pages are shared with the OS rather than counted as our memory.
        _fontFile = Platform::MapFile(path);
        if (!_fontFile.data) continue;
        ImFontConfig cfg;
        cfg.FontNo = index;
        cfg.FontDataOwnedByAtlas = false;
        // FreeType with light hinting: snaps strokes vertically, crisper small text at 100% scale.
        cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        regular = io.Fonts->AddFontFromMemoryTTF(_fontFile.data, static_cast<int>(_fontFile.size), 17.0f, &cfg);
        if (regular)
        {
            // Titles use the same face at a larger size; a separate bold file would cost another ~16 MB.
            _titleFont = regular;
            break;
        }
        Platform::UnmapFile(_fontFile);
    }
    if (!regular)
    {
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplGlfw_InitForOpenGL(_window, true);
    ImGui_ImplOpenGL3_Init(nullptr);

    if (const PetModel* model = _host.Model())
    {
        _iconTexture = LoadIcon(_window, model->Directory() / "icon.png");
    }
    const Config& config = _host.GetConfig();
    _portEdit = config.port;
    std::snprintf(_modelDirEdit, sizeof(_modelDirEdit), "%s", config.modelDir.c_str());
    _hookStatusTime = -100;
    _hookMessage.clear();
    _dialogueLoaded = -1;

    glfwMakeContextCurrent(previous);
}

float SettingsWindow::ContentScale() const
{
#ifdef __APPLE__
    return 1.0f;  // sizes are in points; the framebuffer scale handles Retina
#else
    float sx = 1, sy = 1;
    glfwGetWindowContentScale(_window, &sx, &sy);
    return sy > 0 ? sy : 1.0f;
#endif
}

void SettingsWindow::ApplyScale()
{
    const float scale = ContentScale();
    if (scale == _scale) return;
    _scale = scale;
    // Rebuild from the defaults: ScaleAllSizes multiplies whatever is there.
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    Theme::Apply();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
}

void SettingsWindow::Close()
{
    if (!_window) return;
    GLFWwindow* previous = glfwGetCurrentContext();
    glfwMakeContextCurrent(_window);
    ImGui::SetCurrentContext(_imgui);
    if (_iconTexture)
    {
        GLuint tex = _iconTexture;
        glDeleteTextures(1, &tex);
        _iconTexture = 0;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(_imgui);
    _imgui = nullptr;
    _titleFont = nullptr;
    Platform::UnmapFile(_fontFile);
    GLFWwindow* self = _window;
    glfwDestroyWindow(_window);
    _window = nullptr;
    glfwMakeContextCurrent(previous == self ? nullptr : previous);
}

void SettingsWindow::SectionTitle(const char* title, const char* caption)
{
    ImGui::PushFont(_titleFont, 22.0f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", caption);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void SettingsWindow::Row(const char* label, const char* hint)
{
    const float column = kLabelColumn * ImGui::GetStyle().FontScaleDpi;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (hint)
    {
        ImGui::SameLine();
        // Keep the hint out of the control column: cut it with an ellipsis and show it whole on hover.
        const float room = column - ImGui::GetCursorPosX() - 12 * ImGui::GetStyle().FontScaleDpi;
        std::string shown = hint;
        if (ImGui::CalcTextSize(hint).x > room)
        {
            size_t len = shown.size();
            while (len > 0)
            {
                do { --len; } while (len > 0 && (static_cast<unsigned char>(shown[len]) & 0xC0) == 0x80);
                if (ImGui::CalcTextSize((shown.substr(0, len) + "…").c_str()).x <= room) break;
            }
            shown = shown.substr(0, len) + "…";
        }
        ImGui::TextDisabled("%s", shown.c_str());
        if (shown != hint && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint);
    }
    ImGui::SameLine(column);
    ImGui::SetNextItemWidth(-1);
}

void SettingsWindow::Frame()
{
    if (!_window) return;
    if (glfwWindowShouldClose(_window))
    {
        Close();
        return;
    }

    // Mouse, keyboard and focus events queued by the GLFW backend since the last
    // frame; a resize changes the framebuffer size.
    const double now = glfwGetTime();
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(_window, &fbW, &fbH);
    if (_imgui->InputEventsQueue.Size > 0 || fbW != _lastFbW || fbH != _lastFbH) _activeUntil = now + 0.5;
    if (now >= _activeUntil && now - _lastDraw < 0.25) return;
    _lastDraw = now;
    _lastFbW = fbW;
    _lastFbH = fbH;

    glfwMakeContextCurrent(_window);
    ImGui::SetCurrentContext(_imgui);
    ApplyScale();  // moved to a monitor with another scaling (GLFW resizes the window itself)
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);

    const float s = ImGui::GetStyle().FontScaleDpi;
    ImGui::BeginChild("##side", ImVec2(200 * s, 0));
    DrawSidebar();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (_page)
    {
    case 0: DrawGeneral(); break;
    case 1: DrawAppearance(); break;
    case 2: DrawDialogue(); break;
    case 3: DrawActions(); break;
    case 4: DrawAgents(); break;
    default: DrawAbout(); break;
    }
    ImGui::EndChild();
    ImGui::End();

    ImGui::Render();
    int fbw, fbh;
    glfwGetFramebufferSize(_window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    const ImVec4 bg = Theme::Hex(Theme::kBackground);
    glClearColor(bg.x, bg.y, bg.z, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(_window);

    auto pending = std::move(_pending);
    _pending.clear();
    for (auto& fn : pending) fn();
}

void SettingsWindow::DrawSidebar()
{
    const float s = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetCursorPos(ImVec2(16 * s, 18 * s));
    if (_iconTexture)
    {
        ImGui::Image(static_cast<ImTextureID>(_iconTexture), ImVec2(44 * s, 44 * s));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::PushFont(_titleFont, 20.0f);
    ImGui::TextUnformatted("鲸鱼娘");
    ImGui::PopFont();
    ImGui::TextDisabled("Kujira %s", KUJIRA_VERSION);
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, 10 * s));

    for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(kPages)); ++i)
    {
        ImGui::SetCursorPosX(10 * s);
        if (ImGui::Selectable(kPages[i], _page == i, 0, ImVec2(180 * s, 34 * s))) _page = i;
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 58 * s);
    ImGui::SetCursorPosX(16 * s);
    const EventServer& server = _host.Server();
    if (server.IsRunning())
    {
        ImGui::TextColored(Theme::Hex(Theme::kOk), "● 事件服务运行中");
    }
    else
    {
        ImGui::TextColored(Theme::Hex(Theme::kError), "● 事件服务未启动");
    }
    ImGui::SetCursorPosX(16 * s);
    ImGui::TextDisabled("%.0f MB · %.0f fps", Platform::ProcessMemoryBytes() / (1024.0 * 1024.0), _host.CurrentFps());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("内存占用 · 当前帧率");
}

void SettingsWindow::DrawGeneral()
{
    Config& c = _host.GetConfig();
    SectionTitle("常规", "启动方式与窗口行为");

    bool changed = false;
    if (Platform::AutostartSupported())
    {
        Row("开机自动启动");
        changed |= ImGui::Checkbox("##autostart", &c.autostart);
    }
    Row("窗口始终置顶");
    changed |= ImGui::Checkbox("##topmost", &c.topmost);
#if defined(_WIN32)
    ImGui::BeginDisabled(!c.topmost);
    Row("保持在任务栏上方", "（点任务栏后自动回来）");
    changed |= ImGui::Checkbox("##keepontop", &c.keepOnTop);
    ImGui::EndDisabled();
    Row("全屏程序时隐藏", "（看视频、玩游戏时）");
    changed |= ImGui::Checkbox("##fullscreen", &c.hideForFullscreen);
#elif defined(__APPLE__)
    ImGui::BeginDisabled(!c.topmost);
    Row("保持在程序坞上方", "（可以站在 Dock 上）");
    changed |= ImGui::Checkbox("##keepontop", &c.keepOnTop);
    ImGui::EndDisabled();
    Row("全屏程序时隐藏", "（全屏应用里不显示）");
    changed |= ImGui::Checkbox("##fullscreen", &c.hideForFullscreen);
#endif
    Row("透明区域点击穿透", "（空白处不挡鼠标）");
    changed |= ImGui::Checkbox("##passthrough", &c.clickThrough);
    Row("视线跟随鼠标");
    changed |= ImGui::Checkbox("##look", &c.lookAtMouse);
    Row("进入睡眠", "（无事件时）");
    ImGui::SliderInt("##sleep", &c.sleepMinutes, 1, 60, "%d 分钟后");
    changed |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::Separator();
    Row("位置");
    if (Theme::PrimaryButton("重置到屏幕右下角"))
    {
        Later([this] { _host.ResetWindowPosition(); _host.ConfigChanged(); });
    }
    ImGui::TextDisabled("左键拖动她可以移动位置，单击她会有反应，右键打开这个窗口。");

    ImGui::Separator();
    if (Theme::DangerButton("退出鲸鱼娘"))
    {
        Later([this] { _host.Quit(); });
    }

    if (changed) Later([this] { _host.ConfigChanged(); });
}

void SettingsWindow::DrawAppearance()
{
    Config& c = _host.GetConfig();
    SectionTitle("外观与动画", "大小、帧率与模型");

    bool changed = false;
    Row("大小", "（窗口高度）");
    ImGui::SliderInt("##height", &c.windowHeight, 160, 900, "%d 像素");
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        Later([this] { _host.WindowSizeChanged(); });
    }
    Row("活跃帧率", "（有事件、互动时）");
    ImGui::SliderInt("##afps", &c.activeFps, 5, 120, "%d fps");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    Row("空闲帧率");
    ImGui::SliderInt("##ifps", &c.idleFps, 5, 120, "%d fps");
    changed |= ImGui::IsItemDeactivatedAfterEdit();

    const PetModel* model = _host.Model();
    static const std::vector<std::string> kNoNames;
    Row("待机动画");
    changed |= NameCombo("##idle", c.idleMotion, model ? model->MotionNames() : kNoNames, "（无）");

    ImGui::Separator();
    Row("模型文件夹", "（留空自动查找）");
    ImGui::InputText("##modeldir", _modelDirEdit, sizeof(_modelDirEdit));
    // Two buttons do not always fit in the control column (wide fonts, narrow window);
    // then they start at the left edge instead of running off the page.
    {
        const ImGuiStyle& st = ImGui::GetStyle();
        const float buttons = ImGui::CalcTextSize("重新加载模型").x + ImGui::CalcTextSize("打开 models 文件夹").x +
                              st.FramePadding.x * 4 + st.ItemSpacing.x;
        const float column = kLabelColumn * st.FontScaleDpi;
        if (column + buttons <= ImGui::GetContentRegionMax().x) ImGui::SetCursorPosX(column);
    }
    if (Theme::PrimaryButton("重新加载模型"))
    {
        c.modelDir = _modelDirEdit;
        Later([this] { _host.ConfigChanged(); _host.ReloadModel(); });
    }
    ImGui::SameLine();
    if (model)
    {
        if (ImGui::Button("打开模型文件夹"))
        {
            Platform::OpenFolder(model->Directory());
        }
    }
    else if (ImGui::Button("打开 models 文件夹"))
    {
        // Made on demand so there is somewhere to drop the model.
        const fs::path dir = ModelRoots().front();
        std::error_code ec;
        fs::create_directories(dir, ec);
        Platform::OpenFolder(dir);
    }
    if (model)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("当前：%s（%d 个表情，%d 个动画）", FileUtil::ToUtf8(model->Directory()).c_str(),
                           static_cast<int>(model->ExpressionNames().size()), static_cast<int>(model->MotionNames().size()));
        ImGui::PopStyleColor();
    }
    if (!_host.ModelError().empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Hex(Theme::kError));
        ImGui::TextWrapped("%s", _host.ModelError().c_str());
        ImGui::PopStyleColor();
    }

    if (changed) Later([this] { _host.ConfigChanged(); });
}

void SettingsWindow::DrawDialogue()
{
    Config& c = _host.GetConfig();
    SectionTitle("气泡与台词", "她头顶的对话气泡：Agent 在做什么、需要你处理什么，以及闲聊和互动时说的话");

    bool changed = false;
    static const char* kModes[] = { "all", "important", "off" };
    static const char* kModeLabels[] = { "全部显示", "只显示重要的（等你处理、完成时）", "关闭" };
    int mode = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (c.bubbleMode == kModes[i]) mode = i;
    }
    Row("显示气泡");
    if (ImGui::Combo("##bubblemode", &mode, kModeLabels, 3))
    {
        c.bubbleMode = kModes[mode];
        changed = true;
    }
    ImGui::BeginDisabled(c.bubbleMode == "off");
    Row("每句停留");
    ImGui::SliderFloat("##bubblesec", &c.bubbleSeconds, 2.0f, 15.0f, "%.0f 秒");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    Row("闲聊", "（空闲时偶尔说一句）");
    ImGui::SliderInt("##chat", &c.chatMinutes, 0, 60, c.chatMinutes == 0 ? "不闲聊" : "约每 %d 分钟");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    Row("互动台词", "（悬停、点击、拖动时）");
    changed |= ImGui::Checkbox("##interact", &c.interactionText);
    Row("安静模式", "（只说 Agent 相关的话）");
    changed |= ImGui::Checkbox("##quiet", &c.quietMode);
    ImGui::EndDisabled();
    Row("悬停时显示小按钮", "（设置、安静模式、退出）");
    changed |= ImGui::Checkbox("##toolbar", &c.showToolbar);
    if (changed) Later([this] { _host.ConfigChanged(); });

    // Lines, one group at a time; one line per row in the editor.
    ImGui::Separator();
    Dialogue& dialogue = _host.GetDialogue();
    const auto& categories = Dialogue::Categories();
    _dialogueCategory = std::clamp(_dialogueCategory, 0, static_cast<int>(categories.size()) - 1);
    const Dialogue::Category& category = categories[_dialogueCategory];
    Row("台词分组");
    if (ImGui::BeginCombo("##category", category.label, ImGuiComboFlags_HeightLarge))
    {
        for (int i = 0; i < static_cast<int>(categories.size()); ++i)
        {
            if (ImGui::Selectable(categories[i].label, i == _dialogueCategory)) _dialogueCategory = i;
        }
        ImGui::EndCombo();
    }
    if (_dialogueLoaded != _dialogueCategory)
    {
        std::string joined;
        for (const auto& line : dialogue.Lines(categories[_dialogueCategory].key))
        {
            if (!joined.empty()) joined += '\n';
            joined += line;
        }
        std::snprintf(_dialogueEdit, sizeof(_dialogueEdit), "%s", joined.c_str());
        _dialogueLoaded = _dialogueCategory;
    }
    const Dialogue::Category& current = categories[_dialogueCategory];
    ImGui::TextDisabled("用在：%s。每行一句，随机挑一句说；留空就不说。", current.hint);

    const float s = ImGui::GetStyle().FontScaleDpi;
    ImGui::InputTextMultiline("##lines", _dialogueEdit, sizeof(_dialogueEdit),
                              ImVec2(-1, std::max(120 * s, ImGui::GetContentRegionAvail().y - 80 * s)));
    auto saveEdit = [&] {
        std::vector<std::string> lines;
        std::string text = _dialogueEdit;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(start, end - start);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            while (!line.empty() && line.front() == ' ') line.erase(0, 1);
            if (!line.empty()) lines.push_back(line);
            start = end + 1;
        }
        dialogue.SetLines(current.key, std::move(lines));
        _host.DialogueChanged();
    };
    if (ImGui::IsItemDeactivatedAfterEdit()) saveEdit();

    if (Theme::PrimaryButton("试一句"))
    {
        saveEdit();
        const std::string line = dialogue.Pick(current.key, "App.cpp");
        if (!line.empty()) _host.SayPreview(line);
    }
    ImGui::SameLine();
    if (ImGui::Button("恢复这一组的默认台词"))
    {
        dialogue.ResetToDefault(current.key);
        _host.DialogueChanged();
        _dialogueLoaded = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("打开台词文件夹"))
    {
        Platform::OpenFolder(Platform::ConfigDir());
    }
}

void SettingsWindow::DrawActions()
{
    Config& c = _host.GetConfig();
    SectionTitle("状态映射", "Claude Code 处于每种状态时，她显示哪些表情、播放哪个动画。表情可以多选叠加，试播可以直接预览。");

    const PetModel* model = _host.Model();
    static const std::vector<std::string> kNoNames;
    const auto& expressions = model ? model->ExpressionNames() : kNoNames;
    const auto& motions = model ? model->MotionNames() : kNoNames;
    const float s = ImGui::GetStyle().FontScaleDpi;

    bool changed = false;
    if (ImGui::BeginTable("##map", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX |
                                          ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("表情");
        ImGui::TableSetupColumn("动画");
        ImGui::TableSetupColumn("持续", ImGuiTableColumnFlags_WidthFixed, 90 * s);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 64 * s);
        ImGui::TableHeadersRow();

        auto row = [&](const char* key, const char* label, bool transient) {
            StateAction& a = c.actions[key];
            ImGui::TableNextRow(0, 40 * s);
            ImGui::PushID(key);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            if (std::strcmp(key, StateMachine::Key(StateMachine::State::Farewell)) == 0 && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("由 Claude Code 启动时，最后一个会话结束后播完这一项就关闭");
            }
            ImGui::TableNextColumn();
            changed |= MultiNameCombo("##exp", a.expressions, expressions, model, "（无）");
            ImGui::TableNextColumn();
            {
                const float loopWidth = ImGui::GetFrameHeight() + ImGui::CalcTextSize("循环").x + ImGui::GetStyle().ItemInnerSpacing.x;
                ImGui::PushItemWidth(-(loopWidth + ImGui::GetStyle().ItemSpacing.x));
                std::string preview = a.motion.empty() ? "（无）" : Label(model, a.motion);
                if (ImGui::BeginCombo("##mot", preview.c_str(), ImGuiComboFlags_HeightLarge))
                {
                    if (ImGui::Selectable("（无）", a.motion.empty()))
                    {
                        a.motion.clear();
                        changed = true;
                    }
                    for (const auto& name : motions)
                    {
                        if (ImGui::Selectable(Label(model, name).c_str(), name == a.motion))
                        {
                            a.motion = name;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::BeginDisabled(a.motion.empty());
                changed |= ImGui::Checkbox("循环", &a.loop);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("勾选：状态持续期间一直重复；不勾：播放一次");
            }
            ImGui::TableNextColumn();
            if (transient)
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##hold", &a.holdSeconds, 0.1f, 0.5f, 30.0f, "%.1f 秒");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("持续");
            }
            ImGui::TableNextColumn();
            if (Theme::PrimaryButton("试播", true))
            {
                std::string k = key;
                Later([this, k] { _host.Preview(k); });
            }
            ImGui::PopID();
        };

        for (const auto& info : StateMachine::States())
        {
            row(info.key, info.label, info.transient);
        }
        row(StateMachine::PokeKey(), "被点一下", true);
        row("drag", "被拖动", true);
        ImGui::EndTable();
    }
    if (changed) Later([this] { _host.ConfigChanged(); });
}

void SettingsWindow::RefreshHookStatus()
{
    _hookStatus.clear();
    for (const auto& adapter : AgentRegistry::All())
    {
        _hookStatus.push_back(adapter->Status(_host.GetConfig()));
    }
    _hookStatusTime = glfwGetTime();
}

void SettingsWindow::DrawAgents()
{
    Config& c = _host.GetConfig();
    SectionTitle("Agent 接入", "通过各 Agent 官方的 hook 机制接收事件。只观察，不影响 Agent 的行为；鲸鱼娘没运行时 Agent 照常工作。");

    if (glfwGetTime() - _hookStatusTime > 2.0) RefreshHookStatus();

    const auto& adapters = AgentRegistry::All();
    for (size_t i = 0; i < adapters.size(); ++i)
    {
        const AgentAdapter& a = *adapters[i];
        const HookStatus& st = _hookStatus[i];
        ImGui::PushID(a.Id());

        ImGui::PushFont(_titleFont, 18.0f);
        ImGui::TextUnformatted(a.DisplayName());
        ImGui::PopFont();
        ImGui::SameLine();
        switch (st.state)
        {
        case HookStatus::State::Installed: ImGui::TextColored(Theme::Hex(Theme::kOk), "已安装 hook"); break;
        case HookStatus::State::NotInstalled: ImGui::TextDisabled("未安装"); break;
        case HookStatus::State::Outdated: ImGui::TextColored(Theme::Hex(Theme::kWarn), "需要更新"); break;
        case HookStatus::State::Unreadable: ImGui::TextColored(Theme::Hex(Theme::kError), "配置文件无法解析"); break;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("配置文件：%s", FileUtil::ToUtf8(a.ConfigFile()).c_str());
        ImGui::PopStyleColor();
        if (!st.message.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Hex(Theme::kWarn));
            ImGui::TextWrapped("%s", st.message.c_str());
            ImGui::PopStyleColor();
        }

        Row("接入方式");
        int mode = c.claudeHookMode == "command" ? 1 : 0;
        const char* modes[] = { "鲸鱼娘转发（推荐）", "命令 + curl（兼容旧版）" };
        if (ImGui::Combo("##mode", &mode, modes, 2))
        {
            c.claudeHookMode = mode == 1 ? "command" : "app";
            Later([this] { _host.ConfigChanged(); });
            _hookStatusTime = -100;
        }
        if (c.claudeHookMode != "command")
        {
            Row("跟随 Claude Code 启动", "（开始新会话时自动打开她，所有会话结束后自动关闭）");
            if (ImGui::Checkbox("##launch", &c.launchWithAgent)) Later([this] { _host.ConfigChanged(); });
        }

        const bool installed = st.state == HookStatus::State::Installed || st.state == HookStatus::State::Outdated;
        if (Theme::PrimaryButton(st.state == HookStatus::State::Outdated ? "更新 hook" : "安装 hook"))
        {
            std::string msg;
            _hookMessageOk = a.Install(c, &msg);
            _hookMessage = msg;
            _hookStatusTime = -100;
        }
        if (installed)
        {
            ImGui::SameLine();
            if (ImGui::Button("卸载 hook"))
            {
                std::string msg;
                _hookMessageOk = a.Uninstall(c, &msg);
                _hookMessage = msg;
                _hookStatusTime = -100;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("打开配置所在文件夹"))
        {
            Platform::OpenFolder(a.ConfigFile().parent_path());
        }
        ImGui::PopID();
        ImGui::Separator();
    }

    if (!_hookMessage.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Hex(_hookMessageOk ? Theme::kOk : Theme::kError));
        ImGui::TextWrapped("%s", _hookMessage.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::TextDisabled("需要其它 Agent 请联系 Kujira 作者");
    ImGui::Separator();

    const EventServer& server = _host.Server();
    Row("本地端口", "（仅 127.0.0.1）");
    ImGui::SetNextItemWidth(160 * ImGui::GetStyle().FontScaleDpi);
    ImGui::InputInt("##port", &_portEdit, 0);
    ImGui::SameLine();
    if (ImGui::Button("应用"))
    {
        _portEdit = std::clamp(_portEdit, 1024, 65535);
        c.port = _portEdit;
        Later([this] { _host.ConfigChanged(); _host.RestartServer(); });
        _hookMessage = c.claudeHookMode == "command" ? "端口已更改。已安装的 hook 需要点「更新 hook」。" : "端口已更改。";
        _hookMessageOk = true;
        _hookStatusTime = -100;
    }
    if (!server.IsRunning() && !server.LastError().empty())
    {
        ImGui::TextColored(Theme::Hex(Theme::kError), "%s", server.LastError().c_str());
    }
    const StateMachine& states = _host.States();
    if (server.ReceivedCount() == 0)
    {
        ImGui::TextDisabled("还没有收到事件。安装 hook 后，在 Claude Code 里发一条消息试试。");
    }
    else
    {
        ImGui::TextDisabled("已收到 %llu 个事件，最近一个在 %.0f 秒前：%s", static_cast<unsigned long long>(server.ReceivedCount()),
                            glfwGetTime() - states.LastEventTime(), states.LastEventDetail().c_str());
    }
}

void SettingsWindow::DrawAbout()
{
    SectionTitle("关于", "鲸鱼娘（Kujira）：陪你写代码的桌面看板娘。");
    ImGui::TextWrapped("非官方的个人工具，与 Anthropic 及任何 Agent 厂商无关。支持 Claude Code，后续会接入更多 Agent。");
    ImGui::Spacing();
    if (ImGui::Button("模型作者（B 站）"))
    {
        Platform::OpenUrl("https://space.bilibili.com/11272072");
    }
    ImGui::SameLine();
    if (ImGui::Button("Kujira 作者（B 站）"))
    {
        Platform::OpenUrl("https://space.bilibili.com/25308604");
    }
    ImGui::Spacing();
    ImGui::TextWrapped("This application contains Live2D Cubism SDK developed by Live2D Inc.");
    ImGui::Spacing();
    ImGui::TextWrapped("模型版权归原作者所有，请遵守模型附带的使用须知。");
    ImGui::Spacing();
    ImGui::TextWrapped("使用的开源组件：GLFW、GLEW、Dear ImGui、FreeType、cpp-httplib、nlohmann/json。");
    ImGui::TextWrapped("Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.");
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("配置文件：%s", FileUtil::ToUtf8(Config::FilePath()).c_str());
    ImGui::PopStyleColor();
    if (ImGui::Button("打开配置文件夹"))
    {
        Platform::OpenFolder(Config::FilePath().parent_path());
    }
}
